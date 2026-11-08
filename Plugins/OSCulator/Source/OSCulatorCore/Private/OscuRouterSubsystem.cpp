// Copyright Baron Lanteigne. All Rights Reserved.

#include "OscuRouterSubsystem.h"

#include "OSCulatorCore.h"
#include "OscuMarshal.h"
#include "OscuSettings.h"

#include "Components/ActorComponent.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "UObject/UnrealType.h"

#if WITH_EDITOR
// Only forward-declared by UObjectGlobals.h, and the recompile hook takes one by reference.
#include "UObject/ObjectCompileContext.h"
#endif

namespace
{
	/**
	 * Decides whether a function is exposed at all, and under what name.
	 *
	 * ExtraAllowedFunctions wins over everything; then FunctionPrefix, which also
	 * strips itself from the address; then the blanket bExposeAllFunctions, which is
	 * reported through bOutBlanket because components honour it more narrowly.
	 */
	bool PassesExposurePolicy(const UFunction* Function, const UOscuSettings& Settings, FName& OutAddressName, bool& bOutBlanket)
	{
		bOutBlanket = false;

		// Delegate signatures live in the function map but are not callable.
		if (Function->HasAnyFunctionFlags(FUNC_Delegate))
		{
			return false;
		}

		const FString Name = Function->GetName();
		if (Name.EndsWith(TEXT("__DelegateSignature")))
		{
			return false;
		}

		// ExecuteUbergraph_<Blueprint> is the entire compiled event graph sitting
		// behind a single int32 "resume at this bytecode offset" parameter. It
		// classifies as one perfectly ordinary int argument, which is exactly what
		// makes it dangerous: /laser/ExecuteUbergraph_BP_Laser 4172 would jump into
		// the middle of the graph at an arbitrary instruction. Never expose it.
		//
		// Checked two ways on purpose. FUNC_UbergraphFunction is documented as being
		// set "only when using the persistent ubergraph frame", so a simple Blueprint
		// can carry the function without the flag; the name is what the Blueprint
		// compiler always generates.
		if (Function->HasAnyFunctionFlags(FUNC_UbergraphFunction) || Name.StartsWith(TEXT("ExecuteUbergraph")))
		{
			return false;
		}

		// This filter is about safety rather than convenience, so it sits ahead of
		// ExtraAllowedFunctions -- there is no opting back in.
		if (Settings.ExtraAllowedFunctions.Contains(Function->GetFName()))
		{
			OutAddressName = Function->GetFName();
			return true;
		}

		if (!Settings.FunctionPrefix.IsEmpty())
		{
			if (!Name.StartsWith(Settings.FunctionPrefix, ESearchCase::IgnoreCase))
			{
				return false;
			}
			const FString Stripped = Name.RightChop(Settings.FunctionPrefix.Len());
			if (Stripped.IsEmpty())
			{
				return false;
			}
			OutAddressName = FName(*Stripped);
			return true;
		}

		if (!Settings.bExposeAllFunctions)
		{
			return false;
		}

		OutAddressName = Function->GetFName();
		bOutBlanket = true;
		return true;
	}

	/**
	 * Whether a function exposed on a component's class may be called through its
	 * owner's tag.
	 *
	 * Narrower than the actor rule on purpose. The blanket bExposeAllFunctions already
	 * hands the network every inherited engine function on a tagged actor; extending it
	 * to components would add hundreds more per actor -- SetVisibility, Activate, every
	 * PhysicsControl call -- all answering under the owner's tag. So on a component the
	 * blanket reaches Blueprint-authored functions only. FunctionPrefix and
	 * ExtraAllowedFunctions are explicit opt-ins and apply as written, native or not.
	 */
	bool IsExposedOnComponent(const FOscuFunctionBinding& Binding)
	{
		return !Binding.bBlanketExposure || Binding.Info.bBlueprintAuthored;
	}

	/**
	 * Splits an address into its tag and function name. False for fewer than two
	 * segments.
	 *
	 * Segments after the tag join with '_' into one function name, so /laser/Beam/Width
	 * calls Beam_Width. That keeps a sender's grouped layout -- the prefixed parameter
	 * pages a TouchDesigner OSC Out builds -- while the receiving side stays one function
	 * per leaf. '_' because '/' cannot appear in a function name; the flat
	 * /laser/Beam_Width reaches the same function.
	 *
	 * FNAME_Find, never FNAME_Add. Addresses arrive from the network, and minting a
	 * new FName per unique string would let a sender grow the global name table
	 * without bound. Every tag and function we could legitimately match was already
	 * interned at registration, so a name that does not exist comes back None and
	 * cannot be a hit.
	 */
	bool ParseAddress(const FString& Address, FName& OutTag, FName& OutFunction, FString* OutJoined = nullptr)
	{
		TArray<FString> Segments;
		Address.ParseIntoArray(Segments, TEXT("/"), true);
		if (Segments.Num() < 2)
		{
			return false;
		}

		OutTag = FName(*Segments[0], FNAME_Find);
		Segments.RemoveAt(0);
		FString Joined = FString::Join(Segments, TEXT("_"));
		OutFunction = FName(*Joined, FNAME_Find);
		if (OutJoined != nullptr)
		{
			*OutJoined = MoveTemp(Joined);
		}
		return true;
	}

	/**
	 * True when the first parameter a message fills is text -- a Name, String or Text.
	 * Only such a function can take the end of an address as an argument; see
	 * FindLeafFallback.
	 */
	bool TakesLeadingText(const UFunction* Function)
	{
		for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			const FProperty* Param = *It;
			if (Param->HasAnyPropertyFlags(CPF_ReturnParm) || OscuMarshal::IsPureOutputParam(Param))
			{
				continue;
			}
			return Param->IsA<FNameProperty>() || Param->IsA<FStrProperty>() || Param->IsA<FTextProperty>();
		}
		return false;
	}

	/**
	 * The address-leaf fallback: what to call when nothing answers the full name.
	 *
	 * The joined name is shortened one '_' piece at a time from the right, and the first
	 * shorter name Answers accepts wins, with everything peeled off as the leaf. So
	 * mat_Emissive tries mat with "Emissive", and mat_Base_Color tries mat_Base with
	 * "Color" before mat with "Base_Color". Longest first, so the most specific function
	 * that exists is the one called.
	 *
	 * Answers is expected to accept only functions whose first parameter is text. That is
	 * the guard that keeps a typo from being "rescued" into a call with a word landing in
	 * a float: /laser/Beam/Width/extra does not become Beam_Width(0.0).
	 *
	 * FNAME_Find for the same reason ParseAddress uses it, and it is also what keeps a
	 * miss cheap: a shortened name that was never interned is skipped without walking a
	 * single actor.
	 */
	bool FindLeafFallback(const FString& Joined, TFunctionRef<bool(FName)> Answers, FName& OutFunction, FString& OutLeaf)
	{
		int32 Split = Joined.Len();
		for (;;)
		{
			Split = Joined.Find(TEXT("_"), ESearchCase::CaseSensitive, ESearchDir::FromEnd, Split);
			if (Split <= 0)
			{
				// No '_' left, or only a leading one with no function name before it.
				return false;
			}
			if (Split == Joined.Len() - 1)
			{
				// A trailing '_' leaves an empty leaf, which names nothing.
				continue;
			}

			const FName Candidate(*Joined.Left(Split), FNAME_Find);
			if (!Candidate.IsNone() && Answers(Candidate))
			{
				OutFunction = Candidate;
				OutLeaf = Joined.Mid(Split + 1);
				return true;
			}
		}
	}

	/**
	 * Walks a function's parameters and fills in everything introspection and
	 * dispatch need to know. Returns false if any parameter cannot be marshalled,
	 * leaving a human-readable reason in OutReject.
	 */
	bool DescribeFunction(const UFunction* Function, FOscuExposedFunctionInfo& Out, FString& OutReject)
	{
		Out.FunctionName = Function->GetFName();

		const UClass* OuterClass = Function->GetOuterUClass();
		// Is the owning UClass object itself a Blueprint-generated one -- not whether
		// it derives from some class, which is what IsChildOf would answer.
		Out.bBlueprintAuthored = Cast<UBlueprintGeneratedClass>(OuterClass) != nullptr;

		bool bSeenVariadic = false;

		for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			const FProperty* Param = *It;
			if (Param->HasAnyPropertyFlags(CPF_ReturnParm))
			{
				continue;
			}

			FOscuExposedParamInfo ParamInfo;
			ParamInfo.Name = Param->GetFName();

			const FOscuParamClass Classified = OscuMarshal::ClassifyParam(Param);

			// ProcessEvent writes back through ANY out parameter, including a
			// UPARAM(ref) one that also takes a value in. Either kind means the frame
			// cannot be shared across actors, so record it before splitting the cases.
			if (Param->HasAnyPropertyFlags(CPF_OutParm))
			{
				Out.bHasOutParams = true;
			}

			if (OscuMarshal::IsPureOutputParam(Param))
			{
				// An output pin is written by the call, never read from the message,
				// so its type does not have to be marshallable and it costs no argument.
				ParamInfo.bOutputOnly = true;
				ParamInfo.ArgCount = 0;
				ParamInfo.TypeLabel = FString::Printf(TEXT("out %s"),
					Classified.bMarshallable ? *Classified.TypeLabel : *Param->GetCPPType());
				Out.Params.Add(MoveTemp(ParamInfo));
				continue;
			}

			if (!Classified.bMarshallable)
			{
				OutReject = FString::Printf(TEXT("param '%s' %s"), *Param->GetName(), *Classified.RejectReason);
				return false;
			}

			if (bSeenVariadic)
			{
				OutReject = FString::Printf(
					TEXT("param '%s' follows an array parameter, which makes the argument split ambiguous"),
					*Param->GetName());
				return false;
			}

			if (Classified.ArgCount == OscuVariadicArgCount)
			{
				bSeenVariadic = true;
				Out.bVariadic = true;
			}
			else
			{
				Out.TotalArgCount += Classified.ArgCount;
			}

			ParamInfo.ArgCount = Classified.ArgCount;
			ParamInfo.TypeLabel = Classified.TypeLabel;
			ParamInfo.bContinuous = Classified.bContinuous;
			Out.Params.Add(MoveTemp(ParamInfo));
		}

		return true;
	}
}

// Covers address parsing, actor gathering, frame marshalling AND the ProcessEvent
// itself -- so whatever the Blueprint event does is included here. If this is high
// while the drain is low, the cost is in the called event, not in routing.
DECLARE_CYCLE_STAT(TEXT("Dispatch (incl. called event)"), STAT_OscuDispatch, STATGROUP_OSCulator);

namespace
{
	/** Zero, then construct every parameter. Both halves are required before a
	 *  frame can be filled or passed to ProcessEvent. */
	void InitialiseFrame(const UFunction* Function, uint8* Frame)
	{
		if (Function->ParmsSize > 0)
		{
			FMemory::Memzero(Frame, Function->ParmsSize);
		}
		for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			It->InitializeValue_InContainer(Frame);
		}
	}

	/**
	 * Mandatory, not optional. Any FString, FName-adjacent, FText or TArray
	 * parameter leaks without this, and dispatch runs at message rate.
	 */
	void DestroyFrame(const UFunction* Function, uint8* Frame)
	{
		for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			It->DestroyValue_InContainer(Frame);
		}
	}

	/** Refuses to stack-allocate a frame for a signature this large. Nothing that
	 *  passes registration comes close; this is here so a pathological one cannot
	 *  blow the stack. */
	constexpr int32 GMaxFrameBytes = 16 * 1024;

	/** One class's share of a dispatch, resolved and validated before anything is
	 *  called. Everything is held by value so that dropping a stale cache entry
	 *  cannot dangle a pointer into the exposure it owned. */
	struct FOscuResolvedTarget
	{
		UFunction* Function = nullptr;
		bool bHasOutParams = false;
		const TArray<UObject*>* Objects = nullptr;
	};

	/**
	 * Calls one function on every object of one class -- tagged actors, or the
	 * components answering for them.
	 *
	 * The frame is allocated here rather than in the caller's loop so that each
	 * class group's stack allocation is reclaimed when this function returns --
	 * alloca inside a loop would accumulate until the whole dispatch finished.
	 */
	FORCENOINLINE int32 DispatchToClass(
		UFunction* Function,
		bool bHasOutParams,
		const TArray<UObject*>& Objects,
		const TArray<FOscuValue>& Args,
		EOscuArgPolicy Policy)
	{
		const int32 ParmsSize = Function->ParmsSize;
		if (ParmsSize > GMaxFrameBytes)
		{
			UE_LOG(LogOSCulator, Warning, TEXT("%s has a %d byte parameter frame, which is too large to dispatch. Ignored."),
				*Function->GetName(), ParmsSize);
			return 0;
		}

		uint8* Frame = (ParmsSize > 0)
			? static_cast<uint8*>(FMemory_Alloca_Aligned(ParmsSize, Function->GetMinAlignment()))
			: nullptr;

		int32 CallCount = 0;

		if (!bHasOutParams)
		{
			// No out parameters means ProcessEvent only reads the frame, so one
			// frame serves every object of this class.
			InitialiseFrame(Function, Frame);
			OscuMarshal::FillFrame(Function, Args, Policy, Frame);

			for (UObject* Object : Objects)
			{
				Object->ProcessEvent(Function, Frame);
				++CallCount;
			}

			DestroyFrame(Function, Frame);
		}
		else
		{
			// ProcessEvent writes back through out parameters, so a reused frame
			// would carry one object's outputs into the next object's call.
			for (UObject* Object : Objects)
			{
				InitialiseFrame(Function, Frame);
				OscuMarshal::FillFrame(Function, Args, Policy, Frame);
				Object->ProcessEvent(Function, Frame);
				DestroyFrame(Function, Frame);
				++CallCount;
			}
		}

		return CallCount;
	}
}

FString FOscuExposedFunctionInfo::GetSignatureString() const
{
	TArray<FString> Parts;
	Parts.Reserve(Params.Num());
	for (const FOscuExposedParamInfo& Param : Params)
	{
		if (Param.bOutputOnly)
		{
			continue;
		}
		Parts.Add(Param.TypeLabel);
	}
	return FString::Join(Parts, TEXT(", "));
}

bool UOscuRouterSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	if (World == nullptr)
	{
		return false;
	}

	// Game worlds route messages. The editor world gets a registry too, but only so
	// that MIDI auto-populate can ask what a level exposes without entering play --
	// nothing dispatches there, because OnWorldBeginPlay never runs and no transport
	// opens. Preview and thumbnail worlds are excluded entirely.
	return World->IsGameWorld() || World->WorldType == EWorldType::Editor;
}

void UOscuRouterSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

#if WITH_EDITOR
	// Belt and braces alongside ScanWorld. A button press is not the only moment the
	// answer changes -- a Blueprint can be recompiled while a map asset sits open, or
	// mid-PIE -- and a cache that is only correct just after a button press is a cache
	// nobody can trust.
	CDOCompiledHandle = FCoreUObjectDelegates::OnObjectPostCDOCompiled.AddUObject(
		this, &UOscuRouterSubsystem::HandleClassRecompiled);

	ObjectsReinstancedHandle = FCoreUObjectDelegates::OnObjectsReinstanced.AddUObject(
		this, &UOscuRouterSubsystem::HandleObjectsReinstanced);
#endif
}

void UOscuRouterSubsystem::Deinitialize()
{
#if WITH_EDITOR
	if (CDOCompiledHandle.IsValid())
	{
		FCoreUObjectDelegates::OnObjectPostCDOCompiled.Remove(CDOCompiledHandle);
		CDOCompiledHandle.Reset();
	}
	if (ObjectsReinstancedHandle.IsValid())
	{
		FCoreUObjectDelegates::OnObjectsReinstanced.Remove(ObjectsReinstancedHandle);
		ObjectsReinstancedHandle.Reset();
	}
#endif

	if (ActorSpawnedHandle.IsValid())
	{
		if (UWorld* World = GetWorld())
		{
			World->RemoveOnActorSpawnedHandler(ActorSpawnedHandle);
		}
		ActorSpawnedHandle.Reset();
	}

	TagToActors.Reset();
	ClassExposureCache.Reset();
	ReportedBadAddresses.Reset();
	bReportedAddressCapHit = false;
	TriggerAddressCache.Reset();

	Super::Deinitialize();
}

void UOscuRouterSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

#if STATS
	// See the note in UOscuOSCSubsystem::OnWorldBeginPlay -- stat groups register
	// lazily, so the group has to be touched to exist before any traffic arrives.
	(void)GET_STATID(STAT_OscuDispatch);
#endif

	ScanWorld();

	// Actors spawned after this point join the registry as they appear.
	ActorSpawnedHandle = InWorld.AddOnActorSpawnedHandler(
		FOnActorSpawned::FDelegate::CreateUObject(this, &UOscuRouterSubsystem::OnActorSpawned));

	TSet<TWeakObjectPtr<AActor>> UniqueActors;
	for (const TPair<FName, TArray<TWeakObjectPtr<AActor>>>& Pair : TagToActors)
	{
		UniqueActors.Append(Pair.Value);
	}

	UE_LOG(LogOSCulator, Log, TEXT("Registry ready: %d tag(s) across %d actor(s). Run 'OSCulator.List Custom' to see the callable surface."),
		TagToActors.Num(), UniqueActors.Num());
}

UOscuRouterSubsystem* UOscuRouterSubsystem::Get(const UWorld* World)
{
	return World != nullptr ? World->GetSubsystem<UOscuRouterSubsystem>() : nullptr;
}

void UOscuRouterSubsystem::ScanWorld()
{
	TagToActors.Reset();
	TriggerAddressCache.Reset();

	// And the signatures. This is a RE-scan: the honest meaning of it is "forget what
	// you think this level exposes and look again", and for a long time it did not
	// include the one cache that answers what a class exposes. Both editor buttons --
	// Auto-Map From Level and Validate Against Level -- come through here, so without
	// this there was nothing a user could run to pick up a function they had just added
	// or deleted. Rebuilding costs one reflection walk per class, on next use.
	ClassExposureCache.Reset();

	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		RegisterActor(*It);
	}
}

void UOscuRouterSubsystem::RegisterActor(AActor* Actor)
{
	if (!IsValid(Actor))
	{
		return;
	}

	const UOscuSettings& Settings = *UOscuSettings::Get();
	const FString& Prefix = Settings.TagPrefix;

	for (const FName& Tag : Actor->Tags)
	{
		const FString TagString = Tag.ToString();
		if (!Prefix.IsEmpty() && !TagString.StartsWith(Prefix, ESearchCase::IgnoreCase))
		{
			continue;
		}

		const FString Remainder = Prefix.IsEmpty() ? TagString : TagString.RightChop(Prefix.Len());
		if (Remainder.IsEmpty())
		{
			UE_LOG(LogOSCulator, Warning, TEXT("%s carries the bare tag '%s' with nothing after the prefix. Ignored."),
				*Actor->GetName(), *TagString);
			continue;
		}

		// An actor may carry several prefixed tags and answer under all of them.
		// FName comparison is case-insensitive, so OSC_Laser and OSC_laser agree.
		TagToActors.FindOrAdd(FName(*Remainder)).AddUnique(Actor);
	}
}

void UOscuRouterSubsystem::OnActorSpawned(AActor* Actor)
{
	RegisterActor(Actor);
}

void UOscuRouterSubsystem::GatherActors(FName Tag, TArray<AActor*>& OutActors)
{
	OutActors.Reset();

	TArray<TWeakObjectPtr<AActor>>* Found = TagToActors.Find(Tag);
	if (Found == nullptr)
	{
		return;
	}

	for (int32 Index = 0; Index < Found->Num(); )
	{
		AActor* Actor = (*Found)[Index].Get();
		if (!IsValid(Actor))
		{
			Found->RemoveAt(Index);
			continue;
		}
		OutActors.Add(Actor);
		++Index;
	}
}

const FOscuClassExposure& UOscuRouterSubsystem::GetClassExposure(UClass* Class) const
{
	const TWeakObjectPtr<UClass> Key(Class);

	if (const TSharedRef<FOscuClassExposure>* Existing = ClassExposureCache.Find(Key))
	{
		return Existing->Get();
	}

	const TSharedRef<FOscuClassExposure> Built = BuildClassExposure(Class);
	return ClassExposureCache.Add(Key, Built).Get();
}

TSharedRef<FOscuClassExposure> UOscuRouterSubsystem::BuildClassExposure(UClass* Class) const
{
	++ClassExposureBuilds;

	TSharedRef<FOscuClassExposure> Exposure = MakeShared<FOscuClassExposure>();
	if (Class == nullptr)
	{
		return Exposure;
	}

	const UOscuSettings& Settings = *UOscuSettings::Get();

	// Collect names first, then resolve each through FindFunctionByName. The field
	// iterator yields an overridden function once per level of the hierarchy;
	// resolving by name gives the same UFunction dispatch will find later.
	TSet<FName> SeenNames;
	for (TFieldIterator<UFunction> It(Class, EFieldIteratorFlags::IncludeSuper); It; ++It)
	{
		SeenNames.Add(It->GetFName());
	}

	TArray<FName> OrderedNames = SeenNames.Array();
	OrderedNames.Sort([](const FName& A, const FName& B) { return A.Compare(B) < 0; });

	int32 SkippedCount = 0;

	for (const FName& FunctionName : OrderedNames)
	{
		UFunction* Function = Class->FindFunctionByName(FunctionName);
		if (Function == nullptr)
		{
			continue;
		}

		FName AddressName;
		bool bBlanket = false;
		if (!PassesExposurePolicy(Function, Settings, AddressName, bBlanket))
		{
			continue;
		}

		FOscuExposedFunctionInfo Info;
		FString RejectReason;
		if (!DescribeFunction(Function, Info, RejectReason))
		{
			++SkippedCount;

			// With bExposeAllFunctions on, every actor drags in a few hundred
			// inherited engine functions, plenty of which take object references.
			// Logging all of those at startup would bury the one line the user
			// actually needs, so native rejects go to Verbose and the user's own
			// Blueprint functions -- the ones they expected to be able to call --
			// stay at Log.
			const ELogVerbosity::Type Verbosity = Info.bBlueprintAuthored ? ELogVerbosity::Log : ELogVerbosity::Verbose;
			if (Verbosity == ELogVerbosity::Log)
			{
				UE_LOG(LogOSCulator, Log, TEXT("%s::%s skipped: %s"), *Class->GetName(), *FunctionName.ToString(), *RejectReason);
			}
			else
			{
				UE_LOG(LogOSCulator, Verbose, TEXT("%s::%s skipped: %s"), *Class->GetName(), *FunctionName.ToString(), *RejectReason);
			}
			continue;
		}

		FOscuFunctionBinding Binding;
		Binding.Function = Function;
		Binding.Info = MoveTemp(Info);
		Binding.bBlanketExposure = bBlanket;
		Binding.bTakesAddressLeaf = TakesLeadingText(Function);
		Exposure->ByAddressName.Add(AddressName, MoveTemp(Binding));
	}

	UE_LOG(LogOSCulator, Verbose, TEXT("%s exposes %d function(s); %d skipped as unmarshallable."),
		*Class->GetName(), Exposure->ByAddressName.Num(), SkippedCount);

	return Exposure;
}

bool UOscuRouterSubsystem::IsTriggerAddress(const FString& Address) const
{
	if (const bool* Cached = TriggerAddressCache.Find(Address))
	{
		return *Cached;
	}

	FName TagName;
	FName FunctionName;
	if (!ParseAddress(Address, TagName, FunctionName) || TagName.IsNone() || FunctionName.IsNone())
	{
		return false;
	}

	const TArray<TWeakObjectPtr<AActor>>* Found = TagToActors.Find(TagName);
	if (Found == nullptr)
	{
		return false;
	}

	for (const TWeakObjectPtr<AActor>& Weak : *Found)
	{
		const AActor* Actor = Weak.Get();
		if (!IsValid(Actor))
		{
			continue;
		}

		if (const FOscuFunctionBinding* Binding = FindBindingOnActor(Actor, FunctionName))
		{
			const bool bIsTrigger = Binding->Info.TotalArgCount == 0 && !Binding->Info.bVariadic;
			TriggerAddressCache.Add(Address, bIsTrigger);
			return bIsTrigger;
		}
	}

	return false;
}

bool UOscuRouterSubsystem::ShouldLogRejection(const FString& Address)
{
	// The "/_" namespace is reserved for control and metadata rather than actors.
	// /_describe is ours, and TouchDesigner's OSC Out CHOP emits /_samplerate
	// alongside its channel data every frame. Neither is a mistake, so neither
	// deserves a warning -- but they are still visible at Verbose.
	if (Address.StartsWith(TEXT("/_")))
	{
		UE_LOG(LogOSCulator, Verbose, TEXT("%s: reserved '/_' address, not routed."), *Address);
		return false;
	}

	if (ReportedBadAddresses.Contains(Address))
	{
		return false;
	}

	// Bounded on purpose. A sender emitting random addresses must not be able to
	// grow this set without limit.
	constexpr int32 MaxReportedAddresses = 64;
	if (ReportedBadAddresses.Num() >= MaxReportedAddresses)
	{
		if (!bReportedAddressCapHit)
		{
			bReportedAddressCapHit = true;
			UE_LOG(LogOSCulator, Warning, TEXT("More than %d distinct unroutable addresses have arrived; no further ones will be reported."),
				MaxReportedAddresses);
		}
		return false;
	}

	ReportedBadAddresses.Add(Address);
	return true;
}

int32 UOscuRouterSubsystem::DispatchMessage(const FOscuMessage& Message, EOscuArgPolicy Policy, FString* OutError)
{
	SCOPE_CYCLE_COUNTER(STAT_OscuDispatch);

	// When the caller supplied somewhere to put the reason, reporting is theirs and
	// the throttle does not apply -- a console user typing one command wants the
	// answer every time.
	auto Reject = [this, &Message, OutError](FString&& Reason) -> int32
	{
		if (OutError != nullptr)
		{
			*OutError = MoveTemp(Reason);
		}
		else if (ShouldLogRejection(Message.Address))
		{
			UE_LOG(LogOSCulator, Warning, TEXT("%s Further messages to this address will not be reported."), *Reason);
		}
		return 0;
	};

	FName TagName;
	FName FunctionName;
	FString JoinedName;
	if (!ParseAddress(Message.Address, TagName, FunctionName, &JoinedName))
	{
		return Reject(FString::Printf(TEXT("%s is not a /tag/function address. Ignored."), *Message.Address));
	}

	TArray<AActor*> Actors;
	GatherActors(TagName, Actors);
	if (Actors.Num() == 0)
	{
		// Not a fault. A tag whose actors are gone is a normal state mid-show.
		if (OutError != nullptr)
		{
			*OutError = FString::Printf(TEXT("%s: no actors tagged '%s'."), *Message.Address, *TagName.ToString());
		}
		else
		{
			UE_LOG(LogOSCulator, Verbose, TEXT("%s: no actors tagged '%s'."), *Message.Address, *TagName.ToString());
		}
		return 0;
	}

	// Decide what answers for each actor: the actor itself when its class has the
	// function, otherwise every component of it that does. Actor first is what lets a
	// child class override a component's behaviour by declaring the same function --
	// the component's version is then not called, like an override that does not call
	// its parent. Grouped by class so the function resolves once per class.
	//
	// bLeafOnly restricts it to bindings that take the address leaf, for the fallback.
	TMap<UClass*, TArray<UObject*>> ByClass;
	TArray<const AActor*> Unanswered;
	auto CollectTargets = [this, &Actors, &ByClass, &Unanswered](const FName Name, const bool bLeafOnly)
	{
		for (AActor* Actor : Actors)
		{
			UClass* ActorClass = Actor->GetClass();
			if (const FOscuFunctionBinding* Binding = GetClassExposure(ActorClass).ByAddressName.Find(Name))
			{
				if (!bLeafOnly || Binding->bTakesAddressLeaf)
				{
					ByClass.FindOrAdd(ActorClass).Add(Actor);
				}
				else
				{
					Unanswered.Add(Actor);
				}
				continue;
			}

			bool bComponentAnswered = false;
			for (UActorComponent* Component : Actor->GetComponents())
			{
				if (!IsValid(Component))
				{
					continue;
				}

				UClass* ComponentClass = Component->GetClass();
				const FOscuFunctionBinding* Binding = GetClassExposure(ComponentClass).ByAddressName.Find(Name);
				if (Binding != nullptr && IsExposedOnComponent(*Binding) && (!bLeafOnly || Binding->bTakesAddressLeaf))
				{
					ByClass.FindOrAdd(ComponentClass).Add(Component);
					bComponentAnswered = true;
				}
			}

			if (!bComponentAnswered)
			{
				Unanswered.Add(Actor);
			}
		}
	};

	// A name that was never interned cannot match anything, so it skips the walk --
	// which would otherwise visit every component of every tagged actor to find
	// nothing, at line rate, for a sender with a typo. It still gets the fallback.
	if (!FunctionName.IsNone())
	{
		CollectTargets(FunctionName, false);
	}

	// Nothing answers the full name: try the end of the address as an argument. Only a
	// miss gets here, so an address that resolves directly never pays for it.
	FString Leaf;
	if (ByClass.Num() == 0)
	{
		FName Shorter;
		const bool bFoundLeaf = FindLeafFallback(JoinedName, [this, &Actors](const FName Candidate)
		{
			for (const AActor* Actor : Actors)
			{
				const FOscuFunctionBinding* Binding = FindBindingOnActor(Actor, Candidate);
				if (Binding != nullptr && Binding->bTakesAddressLeaf)
				{
					return true;
				}
			}
			return false;
		}, Shorter, Leaf);

		if (bFoundLeaf)
		{
			FunctionName = Shorter;
			Unanswered.Reset();
			CollectTargets(FunctionName, true);
		}
	}

	if (ByClass.Num() > 0)
	{
		// Actors sharing a tag are assumed to be the same type, but nothing enforces
		// it. Only some of them having the function is not an error.
		for (const AActor* Actor : Unanswered)
		{
			UE_LOG(LogOSCulator, Verbose, TEXT("%s: %s does not expose '%s'."),
				*Message.Address, *Actor->GetClass()->GetName(), *FunctionName.ToString());
		}
	}

	// The leaf goes in front, where the function's leading text parameter reads it.
	TArray<FOscuValue> LeafArgs;
	const TArray<FOscuValue>* Args = &Message.Args;
	if (!Leaf.IsEmpty())
	{
		LeafArgs.Reserve(Message.Args.Num() + 1);
		LeafArgs.Add(FOscuValue::MakeString(Leaf));
		LeafArgs.Append(Message.Args);
		Args = &LeafArgs;
	}

	// Resolve and validate everything before calling anything, so a rejected
	// message really is ignored rather than half-delivered.
	TArray<FOscuResolvedTarget> Targets;
	TArray<TWeakObjectPtr<UClass>> StaleClasses;
	Targets.Reserve(ByClass.Num());

	for (const TPair<UClass*, TArray<UObject*>>& Pair : ByClass)
	{
		// Present by construction: a class only enters ByClass when its exposure had it.
		const FOscuFunctionBinding* Binding = GetClassExposure(Pair.Key).ByAddressName.Find(FunctionName);
		if (Binding == nullptr)
		{
			continue;
		}

		UFunction* Function = Binding->Function.Get();
		if (Function == nullptr)
		{
			// A Blueprint recompile replaced the UFunction. Drop the stale entry
			// after the loop so the next message rebuilds it.
			StaleClasses.Add(TWeakObjectPtr<UClass>(Pair.Key));
			continue;
		}

		FString CountError;
		if (!OscuMarshal::CheckArgCount(Message.Address, Binding->Info, Args->Num(), Policy, CountError))
		{
			for (const TWeakObjectPtr<UClass>& Stale : StaleClasses)
			{
				ClassExposureCache.Remove(Stale);
			}
			return Reject(MoveTemp(CountError));
		}

		FOscuResolvedTarget& Target = Targets.AddDefaulted_GetRef();
		Target.Function = Function;
		Target.bHasOutParams = Binding->Info.bHasOutParams;
		Target.Objects = &Pair.Value;
	}

	for (const TWeakObjectPtr<UClass>& Stale : StaleClasses)
	{
		ClassExposureCache.Remove(Stale);
		TriggerAddressCache.Reset();
	}

	if (Targets.Num() == 0)
	{
		// The joined name rather than FunctionName, which is None when the sender's
		// name was never interned.
		return Reject(FString::Printf(TEXT("%s: nothing tagged '%s' exposes '%s'. Ignored."),
			*Message.Address, *TagName.ToString(), *(Leaf.IsEmpty() ? JoinedName : FunctionName.ToString())));
	}

	int32 TotalCalls = 0;
	for (const FOscuResolvedTarget& Target : Targets)
	{
		TotalCalls += DispatchToClass(Target.Function, Target.bHasOutParams, *Target.Objects, *Args, Policy);
	}

	return TotalCalls;
}

namespace
{
	/**
	 * Running sum of ArgCount up to each parameter, matching FillFrame exactly.
	 *
	 * The correspondence has to be exact or a named parameter silently drives its
	 * neighbour. Output-only parameters carry ArgCount 0 and so fall out of the sum on
	 * their own. A variadic parameter swallows the rest of the message, so nothing can
	 * follow it -- registration already rejects that -- and the walk stops there.
	 */
	void AccumulateSlots(const TArray<FOscuExposedParamInfo>& Params, TArray<int32>& OutSlots)
	{
		OutSlots.Reset(Params.Num());

		int32 Slot = 0;
		for (const FOscuExposedParamInfo& Param : Params)
		{
			OutSlots.Add(Slot);

			if (Param.ArgCount == OscuVariadicArgCount)
			{
				break;
			}
			Slot += Param.ArgCount;
		}

		// A variadic break leaves the tail unfilled; nothing may follow one, so this
		// only matters for keeping the two arrays the same length.
		while (OutSlots.Num() < Params.Num())
		{
			OutSlots.Add(INDEX_NONE);
		}
	}

}

const FOscuFunctionBinding* UOscuRouterSubsystem::FindExposedFunction(const FName Tag, const FName FunctionName) const
{
	const TArray<TWeakObjectPtr<AActor>>* Found = TagToActors.Find(Tag);
	if (Found == nullptr)
	{
		return nullptr;
	}

	// Walks the registry's own array in place. Deliberately NOT GatherActors, which
	// copies every living actor into a caller array and purges dead entries as it goes:
	// this runs at MIDI message rate for a binding that names a parameter, and it stops
	// at the first class that has the function, so a copy of the whole tag was an
	// allocation per message to answer a question that usually ends on the first entry.
	//
	// Dead entries are skipped rather than removed, which is what keeps this const --
	// GatherActors still does the purging on the dispatch path.
	for (const TWeakObjectPtr<AActor>& Weak : *Found)
	{
		const AActor* Actor = Weak.Get();
		if (!IsValid(Actor))
		{
			continue;
		}

		// First class that has it wins -- the actor's own, else a component's, the same
		// order dispatch asks in. Actors sharing a tag are assumed to be the same type but
		// nothing enforces it, and a parameter name is a fact about a signature -- if two
		// classes disagree, dispatch already calls both and the author has a bigger
		// problem than slot numbering.
		if (const FOscuFunctionBinding* Binding = FindBindingOnActor(Actor, FunctionName))
		{
			return Binding;
		}
	}

	return nullptr;
}

const FOscuFunctionBinding* UOscuRouterSubsystem::FindExposedFunctionOrLeaf(const FName Tag, const FName FunctionName, bool& bOutLeaf) const
{
	bOutLeaf = false;

	if (const FOscuFunctionBinding* Exact = FindExposedFunction(Tag, FunctionName))
	{
		return Exact;
	}
	if (FunctionName.IsNone())
	{
		return nullptr;
	}

	const FOscuFunctionBinding* Found = nullptr;
	FName Shorter;
	FString Leaf;
	const bool bFoundLeaf = FindLeafFallback(FunctionName.ToString(), [this, Tag, &Found](const FName Candidate)
	{
		Found = FindExposedFunction(Tag, Candidate);
		return Found != nullptr && Found->bTakesAddressLeaf;
	}, Shorter, Leaf);

	if (!bFoundLeaf)
	{
		return nullptr;
	}

	bOutLeaf = true;
	return Found;
}

const FOscuFunctionBinding* UOscuRouterSubsystem::FindBindingOnActor(const AActor* Actor, const FName FunctionName) const
{
	if (const FOscuFunctionBinding* Binding = GetClassExposure(Actor->GetClass()).ByAddressName.Find(FunctionName))
	{
		return Binding;
	}

	for (const UActorComponent* Component : Actor->GetComponents())
	{
		if (!IsValid(Component))
		{
			continue;
		}

		const FOscuFunctionBinding* Binding = GetClassExposure(Component->GetClass()).ByAddressName.Find(FunctionName);
		if (Binding != nullptr && IsExposedOnComponent(*Binding))
		{
			return Binding;
		}
	}

	return nullptr;
}

int32 UOscuRouterSubsystem::FindParamSlot(
	const FName Tag, const FName FunctionName, const FName ParamName, int32* OutArgCount) const
{
	if (ParamName.IsNone())
	{
		return INDEX_NONE;
	}

	bool bLeaf = false;
	const FOscuFunctionBinding* Binding = FindExposedFunctionOrLeaf(Tag, FunctionName, bLeaf);
	if (Binding == nullptr)
	{
		return INDEX_NONE;
	}

	// The running sum is kept inline and abandoned the moment the name matches, rather
	// than building a slot for every parameter and then looking one up. This is called
	// per MIDI message for a binding that names a parameter, so an allocation here is an
	// allocation at message rate -- it used to make two, one of which was this.
	int32 Slot = 0;
	for (const FOscuExposedParamInfo& Param : Binding->Info.Params)
	{
		if (bLeaf && !Param.bOutputOnly)
		{
			// The leading text parameter, which the address fills. It has no slot, so
			// asking for it by name finds nothing, and it does not advance the sum.
			bLeaf = false;
			if (Param.Name == ParamName)
			{
				return INDEX_NONE;
			}
			continue;
		}

		if (Param.Name == ParamName)
		{
			if (Param.bOutputOnly)
			{
				// Addressable by name, but filled by the call rather than the message.
				// Reported as absent, because writing to it is not a thing that can
				// happen.
				return INDEX_NONE;
			}

			if (OutArgCount != nullptr)
			{
				*OutArgCount = Param.ArgCount;
			}
			return Slot;
		}

		if (Param.ArgCount == OscuVariadicArgCount)
		{
			// A trailing array swallows the rest of the message, so nothing after it has
			// a slot of its own -- registration already rejects such a signature.
			break;
		}
		Slot += Param.ArgCount;
	}

	return INDEX_NONE;
}

bool UOscuRouterSubsystem::DescribeParams(
	const FName Tag, const FName FunctionName,
	TArray<FOscuExposedParamInfo>& OutParams, TArray<int32>& OutSlots) const
{
	OutParams.Reset();
	OutSlots.Reset();

	bool bLeaf = false;
	const FOscuFunctionBinding* Binding = FindExposedFunctionOrLeaf(Tag, FunctionName, bLeaf);
	if (Binding == nullptr)
	{
		return false;
	}

	OutParams = Binding->Info.Params;
	if (bLeaf)
	{
		// Reached through the address-leaf fallback: the leading text parameter is
		// filled from the address, so it is not one an author can assign.
		const int32 LeafIndex = OutParams.IndexOfByPredicate(
			[](const FOscuExposedParamInfo& Param) { return !Param.bOutputOnly; });
		if (LeafIndex != INDEX_NONE)
		{
			OutParams.RemoveAt(LeafIndex);
		}
	}
	AccumulateSlots(OutParams, OutSlots);
	return true;
}

void UOscuRouterSubsystem::InvalidateClassExposure(const UClass* Class)
{
	if (Class == nullptr)
	{
		return;
	}

	// Descendants as well as the class itself. A child Blueprint's exposure is built
	// from everything it inherits, so a parent that gained or lost a function changes
	// what the child offers without the child being recompiled at all.
	TArray<TWeakObjectPtr<UClass>> Doomed;
	for (const TPair<TWeakObjectPtr<UClass>, TSharedRef<FOscuClassExposure>>& Pair : ClassExposureCache)
	{
		const UClass* Cached = Pair.Key.Get();

		// A key whose class has been collected is dropped on the way past: it can never
		// be hit again, and leaving it would grow the map for the life of the process.
		if (Cached == nullptr || Cached->IsChildOf(Class))
		{
			Doomed.Add(Pair.Key);
		}
	}

	for (const TWeakObjectPtr<UClass>& Key : Doomed)
	{
		ClassExposureCache.Remove(Key);
	}

	// Whether an address is a trigger is derived from a signature, so it goes too.
	TriggerAddressCache.Reset();
}

#if WITH_EDITOR

void UOscuRouterSubsystem::HandleClassRecompiled(UObject* CDO, const FObjectPostCDOCompiledContext& Context)
{
	if (CDO == nullptr)
	{
		return;
	}

	if (UOscuSettings::Get()->bPerformanceMode)
	{
		// Nothing recompiles during a show, and a cache rebuild triggered mid-show is a
		// hitch nobody asked for. Checked here rather than by skipping registration, so
		// the switch takes effect the moment it is flipped.
		return;
	}

	// The CDO's class IS the recompiled class. Note that a Blueprint recompile often
	// reuses the same UClass object with new contents -- which is exactly why a cache
	// keyed on the class pointer could not notice on its own, and why the staleness
	// survived until something explicitly forgot.
	InvalidateClassExposure(CDO->GetClass());
}

void UOscuRouterSubsystem::HandleObjectsReinstanced(const TMap<UObject*, UObject*>& ReplacementMap)
{
	if (ReplacementMap.Num() == 0)
	{
		return;
	}

	if (UOscuSettings::Get()->bPerformanceMode)
	{
		return;
	}

	for (const TPair<UObject*, UObject*>& Pair : ReplacementMap)
	{
		if (const UClass* OldClass = Cast<UClass>(Pair.Key))
		{
			InvalidateClassExposure(OldClass);
		}
	}

	// Follow the actors. Reinstancing destroys every instance and builds a replacement,
	// without going through the spawn path -- so the registry's weak pointers go null
	// and OnActorSpawned never hears about the new ones. Re-pointing here keeps a tag
	// populated across a recompile instead of emptying until the next rescan.
	int32 Followed = 0;
	for (TPair<FName, TArray<TWeakObjectPtr<AActor>>>& TagPair : TagToActors)
	{
		TArray<TWeakObjectPtr<AActor>>& Actors = TagPair.Value;

		for (int32 Index = 0; Index < Actors.Num(); )
		{
			AActor* Existing = Actors[Index].Get();

			// Still alive and not replaced: leave it be.
			if (IsValid(Existing) && !ReplacementMap.Contains(Existing))
			{
				++Index;
				continue;
			}

			UObject* const* Replacement = Existing != nullptr ? ReplacementMap.Find(Existing) : nullptr;
			AActor* AsActor = Replacement != nullptr ? Cast<AActor>(*Replacement) : nullptr;

			if (IsValid(AsActor))
			{
				Actors[Index] = AsActor;
				++Followed;
				++Index;
			}
			else
			{
				// Replaced by nothing, or by something that is not an actor. Either way
				// it can no longer be called.
				Actors.RemoveAt(Index);
			}
		}
	}

	if (Followed > 0)
	{
		UE_LOG(LogOSCulator, Verbose, TEXT("Registry followed %d reinstanced actor(s) after a recompile."), Followed);
	}
}

#endif // WITH_EDITOR

TArray<FOscuExposedTagInfo> UOscuRouterSubsystem::Introspect(EOscuIntrospectFilter Filter, FName TagFilter) const
{
	TArray<FOscuExposedTagInfo> Result;

	TArray<FName> Tags;
	TagToActors.GetKeys(Tags);
	Tags.Sort([](const FName& A, const FName& B) { return A.Compare(B) < 0; });

	for (const FName& Tag : Tags)
	{
		if (!TagFilter.IsNone() && Tag != TagFilter)
		{
			continue;
		}

		FOscuExposedTagInfo TagInfo;
		TagInfo.Tag = Tag;

		TArray<UClass*> Classes;
		TArray<UClass*> ComponentClasses;
		for (const TWeakObjectPtr<AActor>& Weak : TagToActors[Tag])
		{
			AActor* Actor = Weak.Get();
			if (!IsValid(Actor))
			{
				continue;
			}
			TagInfo.Actors.Add(Actor);
			Classes.AddUnique(Actor->GetClass());

			for (const UActorComponent* Component : Actor->GetComponents())
			{
				if (IsValid(Component))
				{
					ComponentClasses.AddUnique(Component->GetClass());
				}
			}
		}

		// A tag whose actors have all been destroyed is not worth reporting.
		if (TagInfo.Actors.Num() == 0)
		{
			continue;
		}

		if (Filter != EOscuIntrospectFilter::ActorsOnly)
		{
			const FString TagString = Tag.ToString();

			// Actors sharing a tag are assumed to be the same type, but nothing
			// enforces it, so the reported surface is the union across their classes --
			// and across their components' classes, which answer under the same tag.
			TMap<FName, FOscuExposedFunctionInfo> Merged;
			auto MergeClass = [this, Filter, &TagString, &Merged](UClass* Class, bool bComponent)
			{
				for (const TPair<FName, FOscuFunctionBinding>& Pair : GetClassExposure(Class).ByAddressName)
				{
					if (bComponent && !IsExposedOnComponent(Pair.Value))
					{
						continue;
					}
					if (Filter == EOscuIntrospectFilter::Custom && !Pair.Value.Info.bBlueprintAuthored)
					{
						continue;
					}
					if (Merged.Contains(Pair.Key))
					{
						continue;
					}

					FOscuExposedFunctionInfo Info = Pair.Value.Info;
					Info.Address = FString::Printf(TEXT("/%s/%s"), *TagString, *Pair.Key.ToString());
					Merged.Add(Pair.Key, MoveTemp(Info));
				}
			};

			for (UClass* Class : Classes)
			{
				MergeClass(Class, false);
			}

			// Components after actors, so a name both declare reports the actor's
			// signature -- the one dispatch calls.
			for (UClass* Class : ComponentClasses)
			{
				MergeClass(Class, true);
			}

			Merged.GenerateValueArray(TagInfo.Functions);
			TagInfo.Functions.Sort([](const FOscuExposedFunctionInfo& A, const FOscuExposedFunctionInfo& B)
			{
				return A.Address < B.Address;
			});
		}

		Result.Add(MoveTemp(TagInfo));
	}

	return Result;
}
