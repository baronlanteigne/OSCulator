// Copyright Baron Lanteigne. All Rights Reserved.

#include "OscuOSCLibrary.h"

#include "OSCulatorCore.h"
#include "OscuOSCSubsystem.h"
#include "OscuSettings.h"

#include "Engine/Engine.h"
#include "Engine/World.h"

const FName UOscuOSCLibrary::AllTargets(TEXT("(all)"));

namespace
{
	/** The dropdown carries a literal "(all)"; the subsystem wants None for that. */
	FName ResolveTarget(FName Target)
	{
		return Target == UOscuOSCLibrary::AllTargets ? NAME_None : Target;
	}

	UOscuOSCSubsystem* ResolveSubsystem(const UObject* WorldContextObject)
	{
		const UWorld* World = GEngine != nullptr
			? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull)
			: nullptr;

		return UOscuOSCSubsystem::Get(World);
	}
}

TArray<FString> UOscuOSCLibrary::GetOSCTargetOptions()
{
	TArray<FString> Options;
	Options.Add(AllTargets.ToString());

	for (const FOscuOSCTarget& Target : UOscuSettings::Get()->OSCTargets)
	{
		if (!Target.Name.IsNone())
		{
			Options.AddUnique(Target.Name.ToString());
		}
	}

	return Options;
}

int32 UOscuOSCLibrary::SendOSC(const UObject* WorldContextObject, FName Target,
	const FString& Address, const TArray<FOscuValue>& Args)
{
	UOscuOSCSubsystem* OSC = ResolveSubsystem(WorldContextObject);
	if (OSC == nullptr)
	{
		return 0;
	}

	FOscuMessage Message;
	Message.Address = Address;
	Message.Args = Args;

	return OSC->SendMessage(Message, ResolveTarget(Target));
}

int32 UOscuOSCLibrary::SendOSCFloats(const UObject* WorldContextObject, FName Target,
	const FString& Address, const TArray<float>& Values)
{
	UOscuOSCSubsystem* OSC = ResolveSubsystem(WorldContextObject);
	if (OSC == nullptr)
	{
		return 0;
	}

	FOscuMessage Message;
	Message.Address = Address;
	Message.Args.Reserve(Values.Num());
	for (const float Value : Values)
	{
		Message.Args.Add(FOscuValue::MakeFloat(Value));
	}

	return OSC->SendMessage(Message, ResolveTarget(Target));
}

bool UOscuOSCLibrary::IsOSCOutputReady(const UObject* WorldContextObject)
{
	const UOscuOSCSubsystem* OSC = ResolveSubsystem(WorldContextObject);
	return OSC != nullptr && OSC->GetTargetCount() > 0;
}

FOscuValue UOscuOSCLibrary::Conv_DoubleToOscuValue(double Value)
{
	return FOscuValue::MakeFloat(Value);
}

FOscuValue UOscuOSCLibrary::Conv_IntToOscuValue(int32 Value)
{
	return FOscuValue::MakeInt(Value);
}

FOscuValue UOscuOSCLibrary::Conv_BoolToOscuValue(bool Value)
{
	return FOscuValue::MakeBool(Value);
}

FOscuValue UOscuOSCLibrary::Conv_StringToOscuValue(const FString& Value)
{
	return FOscuValue::MakeString(Value);
}

FOscuValue UOscuOSCLibrary::Conv_NameToOscuValue(FName Value)
{
	return FOscuValue::MakeString(Value.ToString());
}

FOscuValue UOscuOSCLibrary::Conv_VectorToOscuValue(FVector Value)
{
	return FOscuValue::MakeVector(Value);
}
