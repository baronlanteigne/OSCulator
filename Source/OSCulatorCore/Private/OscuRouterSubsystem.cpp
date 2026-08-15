// Copyright Baron Lanteigne. All Rights Reserved.

#include "OscuRouterSubsystem.h"

#include "OSCulatorCore.h"
#include "OscuMarshal.h"
#include "OscuSettings.h"

#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "UObject/UnrealType.h"

namespace
{
	/**
	 * Decides whether a function is exposed at all, and under what name.
	 *
	 * ExtraAllowedFunctions wins over everything; then FunctionPrefix, which also
	 * strips itself from the address; then the blanket bExposeAllFunctions.
	 */
	bool PassesExposurePolicy(const UFunction* Function, const UOscuSettings& Settings, FName& OutAddressName)
	{
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
		return true;
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
		const TArray<AActor*>* Actors = nullptr;
	};

	/**
	 * Calls one function on every actor of one class.
	 *
	 * The frame is allocated here rather than in the caller's loop so that each
	 * class group's stack allocation is reclaimed when this function returns --
	 * alloca inside a loop would accumulate until the whole dispatch finished.
	 */
	FORCENOINLINE int32 DispatchToClass(
		UFunction* Function,
		bool bHasOutParams,
		const TArray<AActor*>& Actors,
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
			// frame serves every actor of this class.
			InitialiseFrame(Function, Frame);
			OscuMarshal::FillFrame(Function, Args, Policy, Frame);

			for (AActor* Actor : Actors)
			{
				Actor->ProcessEvent(Function, Frame);
				++CallCount;
			}

			DestroyFrame(Function, Frame);
		}
		else
		{
			// ProcessEvent writes back through out parameters, so a reused frame
			// would carry one actor's outputs into the next actor's call.
			for (AActor* Actor : Actors)
			{
				InitialiseFrame(Function, Frame);
				OscuMarshal::FillFrame(Function, Args, Policy, Frame);
				Actor->ProcessEvent(Function, Frame);
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

void UOscuRouterSubsystem::Deinitialize()
{
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
		if (!PassesExposurePolicy(Function, Settings, AddressName))
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

	TArray<FString> Segments;
	Address.ParseIntoArray(Segments, TEXT("/"), true);
	if (Segments.Num() != 2)
	{
		return false;
	}

	const FName TagName(*Segments[0], FNAME_Find);
	const FName FunctionName(*Segments[1], FNAME_Find);
	if (TagName.IsNone() || FunctionName.IsNone())
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

		if (const FOscuFunctionBinding* Binding = GetClassExposure(Actor->GetClass()).ByAddressName.Find(FunctionName))
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

	TArray<FString> Segments;
	Message.Address.ParseIntoArray(Segments, TEXT("/"), true);
	if (Segments.Num() != 2)
	{
		return Reject(FString::Printf(TEXT("%s is not a /tag/function address. Ignored."), *Message.Address));
	}

	// FNAME_Find, never FNAME_Add. Addresses arrive from the network, and minting a
	// new FName per unique string would let a sender grow the global name table
	// without bound. Every tag and function we could legitimately match was already
	// interned at registration, so a name that does not exist cannot be a hit.
	const FName TagName(*Segments[0], FNAME_Find);
	const FName FunctionName(*Segments[1], FNAME_Find);

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

	// Group by class so the function resolves once per class rather than per actor.
	TMap<UClass*, TArray<AActor*>> ByClass;
	for (AActor* Actor : Actors)
	{
		ByClass.FindOrAdd(Actor->GetClass()).Add(Actor);
	}

	// Resolve and validate everything before calling anything, so a rejected
	// message really is ignored rather than half-delivered.
	TArray<FOscuResolvedTarget> Targets;
	TArray<TWeakObjectPtr<UClass>> StaleClasses;
	Targets.Reserve(ByClass.Num());

	for (const TPair<UClass*, TArray<AActor*>>& Pair : ByClass)
	{
		const FOscuFunctionBinding* Binding = GetClassExposure(Pair.Key).ByAddressName.Find(FunctionName);
		if (Binding == nullptr)
		{
			// Actors sharing a tag are assumed to be the same type, but nothing
			// enforces it. Only some of them having the function is not an error.
			UE_LOG(LogOSCulator, Verbose, TEXT("%s: %s does not expose '%s'."),
				*Message.Address, *Pair.Key->GetName(), *FunctionName.ToString());
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
		if (!OscuMarshal::CheckArgCount(Message.Address, Binding->Info, Message.Args.Num(), Policy, CountError))
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
		Target.Actors = &Pair.Value;
	}

	for (const TWeakObjectPtr<UClass>& Stale : StaleClasses)
	{
		ClassExposureCache.Remove(Stale);
		TriggerAddressCache.Reset();
	}

	if (Targets.Num() == 0)
	{
		return Reject(FString::Printf(TEXT("%s: nothing tagged '%s' exposes '%s'. Ignored."),
			*Message.Address, *TagName.ToString(), *FunctionName.ToString()));
	}

	int32 TotalCalls = 0;
	for (const FOscuResolvedTarget& Target : Targets)
	{
		TotalCalls += DispatchToClass(Target.Function, Target.bHasOutParams, *Target.Actors, Message.Args, Policy);
	}

	return TotalCalls;
}

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
		for (const TWeakObjectPtr<AActor>& Weak : TagToActors[Tag])
		{
			AActor* Actor = Weak.Get();
			if (!IsValid(Actor))
			{
				continue;
			}
			TagInfo.Actors.Add(Actor);
			Classes.AddUnique(Actor->GetClass());
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
			// enforces it, so the reported surface is the union across their classes.
			TMap<FName, FOscuExposedFunctionInfo> Merged;
			for (UClass* Class : Classes)
			{
				for (const TPair<FName, FOscuFunctionBinding>& Pair : GetClassExposure(Class).ByAddressName)
				{
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
