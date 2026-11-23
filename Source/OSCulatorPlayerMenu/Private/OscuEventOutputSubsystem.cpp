// Copyright Baron Lanteigne. All Rights Reserved.

#include "OscuEventOutputSubsystem.h"

#include "OSCulatorCore.h"
#include "OscuMIDINoteName.h"
#include "OscuMIDISubsystem.h"
#include "OscuOSCSubsystem.h"
#include "OscuSettings.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "UObject/UnrealType.h"

namespace
{
	/** True for the actor tags that mean Tag under the configured prefix. */
	bool HasOSCulatorTag(const AActor& Actor, const FName Tag, const FString& Prefix)
	{
		const FString Wanted = Prefix + Tag.ToString();
		for (const FName& Candidate : Actor.Tags)
		{
			if (Candidate.ToString().Equals(Wanted, ESearchCase::IgnoreCase))
			{
				return true;
			}
		}
		return false;
	}

	/**
	 * One dispatcher parameter as OSC arguments, the way input reads them back: numbers
	 * as numbers, text as a string, a vector as three floats, a rotator as pitch yaw roll,
	 * a colour as r g b a. Anything else is left out.
	 */
	void Encode(const FProperty* Property, const void* Value, TArray<FOscuValue>& OutArgs, double& OutFirstNumber, bool& bOutHaveNumber)
	{
		const auto Number = [&](const double In)
		{
			if (!bOutHaveNumber)
			{
				OutFirstNumber = In;
				bOutHaveNumber = true;
			}
		};

		if (const FBoolProperty* Bool = CastField<FBoolProperty>(Property))
		{
			const bool bValue = Bool->GetPropertyValue(Value);
			OutArgs.Add(FOscuValue::MakeBool(bValue));
			Number(bValue ? 1.0 : 0.0);
		}
		else if (const FEnumProperty* Enum = CastField<FEnumProperty>(Property))
		{
			const int64 Raw = Enum->GetUnderlyingProperty()->GetSignedIntPropertyValue(Value);
			OutArgs.Add(FOscuValue::MakeInt(Raw));
			Number(static_cast<double>(Raw));
		}
		else if (const FNumericProperty* Numeric = CastField<FNumericProperty>(Property))
		{
			if (Numeric->IsFloatingPoint())
			{
				const double D = Numeric->GetFloatingPointPropertyValue(Value);
				OutArgs.Add(FOscuValue::MakeFloat(D));
				Number(D);
			}
			else
			{
				const int64 I = Numeric->GetSignedIntPropertyValue(Value);
				OutArgs.Add(FOscuValue::MakeInt(I));
				Number(static_cast<double>(I));
			}
		}
		else if (const FNameProperty* Name = CastField<FNameProperty>(Property))
		{
			OutArgs.Add(FOscuValue::MakeString(Name->GetPropertyValue(Value).ToString()));
		}
		else if (const FStrProperty* Str = CastField<FStrProperty>(Property))
		{
			OutArgs.Add(FOscuValue::MakeString(Str->GetPropertyValue(Value)));
		}
		else if (const FTextProperty* Text = CastField<FTextProperty>(Property))
		{
			OutArgs.Add(FOscuValue::MakeString(Text->GetPropertyValue(Value).ToString()));
		}
		else if (const FStructProperty* Struct = CastField<FStructProperty>(Property))
		{
			const UScriptStruct* Type = Struct->Struct;
			if (Type == TBaseStructure<FVector>::Get())
			{
				const FVector& V = *static_cast<const FVector*>(Value);
				OutArgs.Add(FOscuValue::MakeVector(V));
				Number(V.X);
			}
			else if (Type == TBaseStructure<FVector2D>::Get())
			{
				const FVector2D& V = *static_cast<const FVector2D*>(Value);
				OutArgs.Add(FOscuValue::MakeFloat(V.X));
				OutArgs.Add(FOscuValue::MakeFloat(V.Y));
				Number(V.X);
			}
			else if (Type == TBaseStructure<FRotator>::Get())
			{
				const FRotator& R = *static_cast<const FRotator*>(Value);
				OutArgs.Add(FOscuValue::MakeFloat(R.Pitch));
				OutArgs.Add(FOscuValue::MakeFloat(R.Yaw));
				OutArgs.Add(FOscuValue::MakeFloat(R.Roll));
				Number(R.Pitch);
			}
			else if (Type == TBaseStructure<FLinearColor>::Get())
			{
				const FLinearColor& C = *static_cast<const FLinearColor*>(Value);
				OutArgs.Add(FOscuValue::MakeFloat(C.R));
				OutArgs.Add(FOscuValue::MakeFloat(C.G));
				OutArgs.Add(FOscuValue::MakeFloat(C.B));
				OutArgs.Add(FOscuValue::MakeFloat(C.A));
				Number(C.R);
			}
		}
	}

	bool IsSignatureParam(const FProperty* Property)
	{
		return Property->HasAnyPropertyFlags(CPF_Parm) && !Property->HasAnyPropertyFlags(CPF_ReturnParm);
	}
}

// ---- Listener ----

void UOscuEventListener::ProcessEvent(UFunction* Function, void* Parms)
{
	if (Function != nullptr && Function->GetFName() == GET_FUNCTION_NAME_CHECKED(UOscuEventListener, OnEvent))
	{
		UOscuEventOutputSubsystem* Subsystem = Owner.Get();
		const UFunction* Sig = Signature.Get();
		if (Subsystem != nullptr && Sig != nullptr)
		{
			Subsystem->HandleEvent(OutputIndex, Sig, Parms);
		}
		return;
	}
	Super::ProcessEvent(Function, Parms);
}

// ---- Send ----

FString FOscuEventSend::Describe() const
{
	TArray<FString> Parts;
	if (bOSC)
	{
		Parts.Add(Message.ToLogString());
	}
	if (MIDIKind == EOscuPlayerMIDIKind::Note || MIDIKind == EOscuPlayerMIDIKind::ControlChange)
	{
		Parts.Add(FString::Printf(TEXT("%s = %d"),
			*UOscuEventOutputSubsystem::DescribeMIDIOut(MIDIKind, MIDIChannel, MIDINumber), MIDIValue));
	}
	return Parts.Num() > 0 ? FString::Join(Parts, TEXT("   ")) : FString(TEXT("nothing (OSC and MIDI are both off for it)"));
}

// ---- Subsystem ----

bool UOscuEventOutputSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World != nullptr && World->IsGameWorld();
}

UOscuEventOutputSubsystem* UOscuEventOutputSubsystem::Get(const UWorld* World)
{
	return World != nullptr ? World->GetSubsystem<UOscuEventOutputSubsystem>() : nullptr;
}

void UOscuEventOutputSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	SettingsChangedHandle = UOscuSettings::OnSettingsChanged.AddUObject(this, &UOscuEventOutputSubsystem::Rebind);
	MappingsChangedHandle = FOscuPlayerMappings::OnChanged.AddUObject(this, &UOscuEventOutputSubsystem::RefreshOverrides);
	ActorSpawnedHandle = InWorld.AddOnActorSpawnedHandler(
		FOnActorSpawned::FDelegate::CreateUObject(this, &UOscuEventOutputSubsystem::HandleActorSpawned));

	Rebind();
}

void UOscuEventOutputSubsystem::Deinitialize()
{
	UOscuSettings::OnSettingsChanged.Remove(SettingsChangedHandle);
	FOscuPlayerMappings::OnChanged.Remove(MappingsChangedHandle);
	if (UWorld* World = GetWorld())
	{
		World->RemoveOnActorSpawnedHandler(ActorSpawnedHandle);
	}

	UnbindAll();
	Super::Deinitialize();
}

void UOscuEventOutputSubsystem::Rebind()
{
	UnbindAll();
	RefreshOverrides();

	UWorld* World = GetWorld();
	if (World == nullptr || UOscuSettings::Get()->EventOutputs.Num() == 0)
	{
		return;
	}

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		BindActor(*It);
	}

	UE_LOG(LogOSCulator, Log, TEXT("Event outputs: %d dispatcher binding(s)."), Listeners.Num());
}

void UOscuEventOutputSubsystem::HandleActorSpawned(AActor* Actor)
{
	BindActor(Actor);
}

void UOscuEventOutputSubsystem::BindActor(AActor* Actor)
{
	if (Actor == nullptr || Actor->Tags.Num() == 0)
	{
		return;
	}

	const UOscuSettings& Settings = *UOscuSettings::Get();
	for (int32 Index = 0; Index < Settings.EventOutputs.Num(); ++Index)
	{
		const FOscuEventOutput& Output = Settings.EventOutputs[Index];
		if (Output.Tag.IsNone() || Output.Dispatcher.IsNone() || !HasOSCulatorTag(*Actor, Output.Tag, Settings.TagPrefix))
		{
			continue;
		}

		const bool bAlready = Listeners.ContainsByPredicate([Actor, Index](const UOscuEventListener* Listener)
		{
			return Listener != nullptr && Listener->Actor.Get() == Actor && Listener->OutputIndex == Index;
		});
		if (bAlready)
		{
			continue;
		}

		FMulticastDelegateProperty* Property = FindFProperty<FMulticastDelegateProperty>(Actor->GetClass(), Output.Dispatcher);
		if (Property == nullptr || Property->SignatureFunction == nullptr)
		{
			const FString Key = FString::Printf(TEXT("%s.%s"), *Actor->GetClass()->GetName(), *Output.Dispatcher.ToString());
			if (!Warned.Contains(Key))
			{
				Warned.Add(Key);
				UE_LOG(LogOSCulator, Warning, TEXT("Event outputs: %s (tagged '%s') has no event dispatcher named '%s'."),
					*Actor->GetClass()->GetName(), *Output.Tag.ToString(), *Output.Dispatcher.ToString());
			}
			continue;
		}

		UOscuEventListener* Listener = NewObject<UOscuEventListener>(this);
		Listener->Owner = this;
		Listener->Actor = Actor;
		Listener->Signature = Property->SignatureFunction;
		Listener->Dispatcher = Output.Dispatcher;
		Listener->OutputIndex = Index;

		FScriptDelegate Delegate;
		Delegate.BindUFunction(Listener, GET_FUNCTION_NAME_CHECKED(UOscuEventListener, OnEvent));
		Property->AddDelegate(MoveTemp(Delegate), Actor);

		Listeners.Add(Listener);
	}
}

void UOscuEventOutputSubsystem::UnbindAll()
{
	for (UOscuEventListener* Listener : Listeners)
	{
		AActor* Actor = Listener != nullptr ? Listener->Actor.Get() : nullptr;
		if (Actor == nullptr)
		{
			continue;
		}

		// Looked up again rather than kept: the class may have been recompiled since.
		if (FMulticastDelegateProperty* Property = FindFProperty<FMulticastDelegateProperty>(Actor->GetClass(), Listener->Dispatcher))
		{
			FScriptDelegate Delegate;
			Delegate.BindUFunction(Listener, GET_FUNCTION_NAME_CHECKED(UOscuEventListener, OnEvent));
			Property->RemoveDelegate(Delegate, Actor);
		}
	}
	Listeners.Reset();
}

void UOscuEventOutputSubsystem::RefreshOverrides()
{
	ActiveOverrides.Reset();

	const TArray<FOscuEventOutput>& Outputs = UOscuSettings::Get()->EventOutputs;
	for (const FOscuPlayerOverride& Override : FOscuPlayerMappings::Get().GetActive())
	{
		if (!Override.bOutput)
		{
			continue;
		}
		for (int32 Index = 0; Index < Outputs.Num(); ++Index)
		{
			if (Outputs[Index].Tag == Override.Tag && Outputs[Index].Dispatcher == Override.FunctionName)
			{
				ActiveOverrides.Add(Index, Override);
			}
		}
	}
}

FString UOscuEventOutputSubsystem::ResolveOSC(const int32 OutputIndex, bool& bOutSend, bool& bOutCustom) const
{
	bOutSend = false;
	bOutCustom = false;

	const TArray<FOscuEventOutput>& Outputs = UOscuSettings::Get()->EventOutputs;
	if (!Outputs.IsValidIndex(OutputIndex))
	{
		return FString();
	}

	const FOscuEventOutput& Output = Outputs[OutputIndex];
	if (const FOscuPlayerOverride* Override = ActiveOverrides.Find(OutputIndex); Override != nullptr && Override->HasOSC())
	{
		bOutSend = true;
		bOutCustom = true;
		return Override->OSCAddress;
	}

	bOutSend = Output.bSendOSC;
	return Output.GetDefaultOSCAddress();
}

void UOscuEventOutputSubsystem::ResolveMIDI(const int32 OutputIndex, EOscuPlayerMIDIKind& OutKind, int32& OutChannel, int32& OutNumber, bool& bOutCustom) const
{
	OutKind = EOscuPlayerMIDIKind::None;
	OutChannel = 1;
	OutNumber = 0;
	bOutCustom = false;

	const TArray<FOscuEventOutput>& Outputs = UOscuSettings::Get()->EventOutputs;
	if (!Outputs.IsValidIndex(OutputIndex))
	{
		return;
	}

	if (const FOscuPlayerOverride* Override = ActiveOverrides.Find(OutputIndex); Override != nullptr && Override->HasMIDI())
	{
		bOutCustom = true;
		OutKind = Override->MIDIKind == EOscuPlayerMIDIKind::Off ? EOscuPlayerMIDIKind::None : Override->MIDIKind;
		OutChannel = Override->MIDIChannel;
		OutNumber = Override->MIDINumber;
		return;
	}

	const FOscuEventOutput& Output = Outputs[OutputIndex];
	switch (Output.MIDIKind)
	{
	case EOscuEventMIDIKind::Note:          OutKind = EOscuPlayerMIDIKind::Note; break;
	case EOscuEventMIDIKind::ControlChange: OutKind = EOscuPlayerMIDIKind::ControlChange; break;
	default:                                OutKind = EOscuPlayerMIDIKind::None; break;
	}
	OutChannel = FMath::Clamp(Output.MIDIChannel, 1, 16);
	OutNumber = FMath::Clamp(Output.MIDINumber, 0, 127);
}

int32 UOscuEventOutputSubsystem::ScaleToMIDI(const FOscuEventOutput& Output, const double Value)
{
	const double Range = static_cast<double>(Output.ValueMax) - static_cast<double>(Output.ValueMin);
	if (FMath::IsNearlyZero(Range))
	{
		return Value >= Output.ValueMax ? 127 : 0;
	}
	const double T = FMath::Clamp((Value - Output.ValueMin) / Range, 0.0, 1.0);
	return FMath::RoundToInt32(T * 127.0);
}

void UOscuEventOutputSubsystem::HandleEvent(const int32 OutputIndex, const UFunction* Signature, const void* Parms)
{
	const TArray<FOscuEventOutput>& Outputs = UOscuSettings::Get()->EventOutputs;
	if (!Outputs.IsValidIndex(OutputIndex))
	{
		return;
	}
	const FOscuEventOutput& Output = Outputs[OutputIndex];

	FOscuEventSend Out;
	Out.OutputIndex = OutputIndex;

	bool bCustom = false;
	Out.Message.Address = ResolveOSC(OutputIndex, Out.bOSC, bCustom);

	double FirstNumber = 0.0;
	bool bHaveNumber = false;
	if (Signature != nullptr && Parms != nullptr)
	{
		for (TFieldIterator<FProperty> It(Signature); It && IsSignatureParam(*It); ++It)
		{
			Encode(*It, It->ContainerPtrToValuePtr<void>(const_cast<void*>(Parms)), Out.Message.Args, FirstNumber, bHaveNumber);
		}
	}

	ResolveMIDI(OutputIndex, Out.MIDIKind, Out.MIDIChannel, Out.MIDINumber, bCustom);
	Out.MIDIValue = bHaveNumber ? ScaleToMIDI(Output, FirstNumber) : 127;
	Out.NoteLength = FMath::Max(0.01f, Output.NoteLength);

	Send(Out);
}

void UOscuEventOutputSubsystem::Send(FOscuEventSend& Out)
{
	const UOscuSettings& Settings = *UOscuSettings::Get();

	if (Out.bOSC && Settings.bEnableOSCOut && !Out.Message.Address.IsEmpty())
	{
		if (UOscuOSCSubsystem* OSC = UOscuOSCSubsystem::Get(GetWorld()))
		{
			OSC->SendMessage(Out.Message);
		}
	}

	if (Settings.bEnableMIDIOut)
	{
		if (UOscuMIDISubsystem* MIDI = UOscuMIDISubsystem::Get())
		{
			if (Out.MIDIKind == EOscuPlayerMIDIKind::ControlChange)
			{
				MIDI->SendControlChange(Out.MIDIChannel, Out.MIDINumber, Out.MIDIValue);
			}
			else if (Out.MIDIKind == EOscuPlayerMIDIKind::Note && Out.MIDIValue > 0)
			{
				// Velocity 0 is a note off on the wire, so a zero value sends nothing.
				MIDI->SendNote(Out.MIDIChannel, Out.MIDINumber, Out.MIDIValue, Out.NoteLength);
			}
		}
	}

	LastSend = Out;
	++SendCount;
}

FOscuEventSend UOscuEventOutputSubsystem::SendTest(const int32 OutputIndex)
{
	const TArray<FOscuEventOutput>& Outputs = UOscuSettings::Get()->EventOutputs;
	if (!Outputs.IsValidIndex(OutputIndex))
	{
		return FOscuEventSend();
	}

	const TObjectPtr<UOscuEventListener>* Found = Listeners.FindByPredicate([OutputIndex](const UOscuEventListener* Listener)
	{
		return Listener != nullptr && Listener->OutputIndex == OutputIndex && Listener->Signature.IsValid();
	});
	UFunction* Signature = Found != nullptr ? (*Found)->Signature.Get() : nullptr;

	if (Signature == nullptr)
	{
		HandleEvent(OutputIndex, nullptr, nullptr);
		return LastSend;
	}

	// A zeroed parameter block, with the first number at Value Max.
	uint8* Parms = static_cast<uint8*>(FMemory::Malloc(FMath::Max<int32>(Signature->ParmsSize, 1), Signature->GetMinAlignment()));
	FMemory::Memzero(Parms, Signature->ParmsSize);
	Signature->InitializeStruct(Parms);

	for (TFieldIterator<FProperty> It(Signature); It && IsSignatureParam(*It); ++It)
	{
		void* Value = It->ContainerPtrToValuePtr<void>(Parms);
		if (const FBoolProperty* Bool = CastField<FBoolProperty>(*It))
		{
			Bool->SetPropertyValue(Value, true);
			break;
		}
		if (const FNumericProperty* Numeric = CastField<FNumericProperty>(*It); Numeric != nullptr && !Numeric->IsEnum())
		{
			if (Numeric->IsFloatingPoint())
			{
				Numeric->SetFloatingPointPropertyValue(Value, Outputs[OutputIndex].ValueMax);
			}
			else
			{
				Numeric->SetIntPropertyValue(Value, static_cast<int64>(Outputs[OutputIndex].ValueMax));
			}
			break;
		}
	}

	HandleEvent(OutputIndex, Signature, Parms);

	Signature->DestroyStruct(Parms);
	FMemory::Free(Parms);
	return LastSend;
}

// ---- MIDI text ----

FString UOscuEventOutputSubsystem::DescribeMIDIOut(const EOscuPlayerMIDIKind Kind, const int32 Channel, const int32 Number)
{
	switch (Kind)
	{
	case EOscuPlayerMIDIKind::Note:
		return FString::Printf(TEXT("Ch %d · %s"), Channel,
			*OscuMIDINoteName::ToString(static_cast<uint8>(FMath::Clamp(Number, 0, 127)), UOscuSettings::Get()->MiddleCOctave));
	case EOscuPlayerMIDIKind::ControlChange:
		return FString::Printf(TEXT("Ch %d · CC %d"), Channel, Number);
	case EOscuPlayerMIDIKind::Off:
		return TEXT("off");
	default:
		return FString();
	}
}

bool UOscuEventOutputSubsystem::ParseMIDIOut(const FString& Text, EOscuPlayerMIDIKind& InOutKind, int32& InOutChannel, int32& InOutNumber, FString& OutError)
{
	FString Clean = Text.ToLower();
	Clean.ReplaceInline(TEXT("·"), TEXT(" "));
	Clean.ReplaceInline(TEXT(","), TEXT(" "));
	Clean.ReplaceInline(TEXT("="), TEXT(" "));

	TArray<FString> Tokens;
	Clean.ParseIntoArrayWS(Tokens);
	if (Tokens.Num() == 0)
	{
		OutError = TEXT("Type something like \"cc 21\", \"note C3\" or \"ch 2 cc 21\".");
		return false;
	}

	if (Tokens.Num() == 1 && (Tokens[0] == TEXT("off") || Tokens[0] == TEXT("none")))
	{
		InOutKind = EOscuPlayerMIDIKind::Off;
		return true;
	}

	EOscuPlayerMIDIKind Kind = InOutKind;
	int32 Channel = InOutChannel;
	int32 Number = InOutNumber;
	bool bGotNumber = false;

	const int32 MiddleCOctave = UOscuSettings::Get()->MiddleCOctave;
	const auto ReadNote = [MiddleCOctave](const FString& Token, int32& Out) -> bool
	{
		uint8 Note = 0;
		FString Error;
		if (OscuMIDINoteName::Parse(Token, MiddleCOctave, Note, Error))
		{
			Out = Note;
			return true;
		}
		return false;
	};
	const auto ReadInt = [](const FString& Token, int32& Out) -> bool
	{
		if (!Token.IsNumeric())
		{
			return false;
		}
		Out = FCString::Atoi(*Token);
		return true;
	};

	for (int32 Index = 0; Index < Tokens.Num(); ++Index)
	{
		const FString& Token = Tokens[Index];
		const bool bHasNext = Tokens.IsValidIndex(Index + 1);

		if (Token == TEXT("ch") || Token == TEXT("channel"))
		{
			if (!bHasNext || !ReadInt(Tokens[++Index], Channel))
			{
				OutError = TEXT("A channel is a number from 1 to 16.");
				return false;
			}
		}
		else if (Token.StartsWith(TEXT("ch")) && ReadInt(Token.RightChop(2), Channel))
		{
		}
		else if (Token == TEXT("cc"))
		{
			Kind = EOscuPlayerMIDIKind::ControlChange;
			if (!bHasNext || !ReadInt(Tokens[++Index], Number))
			{
				OutError = TEXT("A controller number is from 0 to 127.");
				return false;
			}
			bGotNumber = true;
		}
		else if (Token.StartsWith(TEXT("cc")) && ReadInt(Token.RightChop(2), Number))
		{
			Kind = EOscuPlayerMIDIKind::ControlChange;
			bGotNumber = true;
		}
		else if (Token == TEXT("note") || Token == TEXT("n"))
		{
			Kind = EOscuPlayerMIDIKind::Note;
			if (!bHasNext || !ReadNote(Tokens[++Index], Number))
			{
				OutError = TEXT("A note is a name like C3 or a number from 0 to 127.");
				return false;
			}
			bGotNumber = true;
		}
		else if (ReadInt(Token, Number))
		{
			// A bare number keeps the kind; with none yet, a controller.
			if (Kind != EOscuPlayerMIDIKind::Note && Kind != EOscuPlayerMIDIKind::ControlChange)
			{
				Kind = EOscuPlayerMIDIKind::ControlChange;
			}
			bGotNumber = true;
		}
		else if (ReadNote(Token, Number))
		{
			Kind = EOscuPlayerMIDIKind::Note;
			bGotNumber = true;
		}
		else
		{
			OutError = FString::Printf(TEXT("Could not read \"%s\". Try \"cc 21\", \"note C3\" or \"ch 2 cc 21\"."), *Token);
			return false;
		}
	}

	if (Channel < 1 || Channel > 16)
	{
		OutError = TEXT("A channel is a number from 1 to 16.");
		return false;
	}
	if (Number < 0 || Number > 127)
	{
		OutError = TEXT("A note or controller number is from 0 to 127.");
		return false;
	}
	if (!bGotNumber && Kind != EOscuPlayerMIDIKind::Note && Kind != EOscuPlayerMIDIKind::ControlChange)
	{
		OutError = TEXT("Say which: \"cc 21\" or \"note C3\".");
		return false;
	}

	InOutKind = Kind;
	InOutChannel = Channel;
	InOutNumber = Number;
	return true;
}
