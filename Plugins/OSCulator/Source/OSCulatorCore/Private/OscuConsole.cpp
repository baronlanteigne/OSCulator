// Copyright Baron Lanteigne. All Rights Reserved.

#include "OSCulatorCore.h"
#include "OscuIntrospection.h"
#include "OscuMarshal.h"
#include "OscuRouterSubsystem.h"
#include "OscuValue.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	/** Prints to both the log and the in-game console, so it is visible either way. */
	void Line(FOutputDevice& Ar, const FString& Text)
	{
		Ar.Log(*Text);
	}

	FString FormatParams(const FOscuExposedFunctionInfo& Function)
	{
		if (Function.Params.Num() == 0)
		{
			return TEXT("()");
		}

		TArray<FString> Parts;
		Parts.Reserve(Function.Params.Num());
		for (const FOscuExposedParamInfo& Param : Function.Params)
		{
			Parts.Add(FString::Printf(TEXT("%s %s"), *Param.TypeLabel, *Param.Name.ToString()));
		}
		return FString::Printf(TEXT("(%s)"), *FString::Join(Parts, TEXT(", ")));
	}

	FString FormatArgCount(const FOscuExposedFunctionInfo& Function)
	{
		if (Function.bVariadic)
		{
			return FString::Printf(TEXT("%d+ args"), Function.TotalArgCount);
		}
		return FString::Printf(TEXT("%d arg%s"), Function.TotalArgCount, Function.TotalArgCount == 1 ? TEXT("") : TEXT("s"));
	}

	FString DescribeActors(const FOscuExposedTagInfo& TagInfo)
	{
		TMap<FString, int32> CountByClass;
		for (const TWeakObjectPtr<AActor>& Weak : TagInfo.Actors)
		{
			if (const AActor* Actor = Weak.Get())
			{
				++CountByClass.FindOrAdd(Actor->GetClass()->GetName());
			}
		}

		TArray<FString> Parts;
		for (const TPair<FString, int32>& Pair : CountByClass)
		{
			Parts.Add(Pair.Value > 1
				? FString::Printf(TEXT("%s x%d"), *Pair.Key, Pair.Value)
				: Pair.Key);
		}
		Parts.Sort();
		return FString::Join(Parts, TEXT(", "));
	}

	UOscuRouterSubsystem* ResolveSubsystem(UWorld* World, FOutputDevice& Ar)
	{
		UOscuRouterSubsystem* Router = UOscuRouterSubsystem::Get(World);
		if (Router == nullptr)
		{
			Line(Ar, TEXT("[OSCulator] No router for this world. The registry only exists in PIE and packaged builds."));
		}
		return Router;
	}

	void ListCommand(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		UOscuRouterSubsystem* Router = ResolveSubsystem(World, Ar);
		if (Router == nullptr)
		{
			return;
		}

		EOscuIntrospectFilter Filter = EOscuIntrospectFilter::All;
		FName TagFilter = NAME_None;

		if (Args.Num() > 0)
		{
			if (Args[0].Equals(TEXT("Custom"), ESearchCase::IgnoreCase))
			{
				Filter = EOscuIntrospectFilter::Custom;
			}
			else if (Args[0].Equals(TEXT("Actors"), ESearchCase::IgnoreCase))
			{
				Filter = EOscuIntrospectFilter::ActorsOnly;
			}
			else
			{
				// Anything else is read as a tag name.
				TagFilter = FName(*Args[0]);
			}
		}

		const TArray<FOscuExposedTagInfo> Tags = Router->Introspect(Filter, TagFilter);

		if (Tags.Num() == 0)
		{
			Line(Ar, TagFilter.IsNone()
				? TEXT("[OSCulator] Nothing registered. Tag an actor 'OSC_something' and re-enter play.")
				: *FString::Printf(TEXT("[OSCulator] No tag '%s' in the registry."), *TagFilter.ToString()));
			return;
		}

		int32 TotalFunctions = 0;
		for (const FOscuExposedTagInfo& TagInfo : Tags)
		{
			TotalFunctions += TagInfo.Functions.Num();
		}

		Line(Ar, FString::Printf(TEXT("[OSCulator] %d tag(s), %d exposed function(s)%s"),
			Tags.Num(), TotalFunctions,
			Filter == EOscuIntrospectFilter::Custom ? TEXT(" (Blueprint-authored only)") : TEXT("")));

		for (const FOscuExposedTagInfo& TagInfo : Tags)
		{
			Line(Ar, TEXT(""));
			Line(Ar, FString::Printf(TEXT("  %s  [%d actor(s): %s]"),
				*TagInfo.Tag.ToString(), TagInfo.Actors.Num(), *DescribeActors(TagInfo)));

			if (Filter == EOscuIntrospectFilter::ActorsOnly)
			{
				continue;
			}

			if (TagInfo.Functions.Num() == 0)
			{
				Line(Ar, TEXT("    (no functions match this filter)"));
				continue;
			}

			for (const FOscuExposedFunctionInfo& Function : TagInfo.Functions)
			{
				Line(Ar, FString::Printf(TEXT("    %-40s %-10s %s"),
					*Function.Address, *FormatArgCount(Function), *FormatParams(Function)));
			}
		}
	}

	void SendCommand(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		UOscuRouterSubsystem* Router = ResolveSubsystem(World, Ar);
		if (Router == nullptr)
		{
			return;
		}

		int32 Index = 0;
		EOscuArgPolicy Policy = EOscuArgPolicy::Strict;

		if (Args.Num() > 0 && Args[0].Equals(TEXT("-lenient"), ESearchCase::IgnoreCase))
		{
			// Lets the MIDI-side behaviour be exercised before MIDI itself exists.
			Policy = EOscuArgPolicy::Lenient;
			++Index;
		}

		if (!Args.IsValidIndex(Index))
		{
			Line(Ar, TEXT("[OSCulator] Usage: OSCulator.Send [-lenient] /tag/function [args...]"));
			return;
		}

		FOscuMessage Message;
		Message.Address = Args[Index++];

		for (; Index < Args.Num(); ++Index)
		{
			// The console gives us text, so numbers are recovered by inspection.
			// This mirrors what arrives over the wire closely enough to test with:
			// senders overwhelmingly emit floats and strings.
			Message.Args.Add(Args[Index].IsNumeric()
				? FOscuValue::MakeFloat(FCString::Atod(*Args[Index]))
				: FOscuValue::MakeString(Args[Index]));
		}

		FString Error;
		const int32 CallCount = Router->DispatchMessage(Message, Policy, &Error);

		if (!Error.IsEmpty())
		{
			Line(Ar, FString::Printf(TEXT("[OSCulator] %s"), *Error));
			return;
		}

		Line(Ar, FString::Printf(TEXT("[OSCulator] %s -> %d actor(s)"), *Message.ToLogString(), CallCount));
	}

	void ExportCommand(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		UOscuRouterSubsystem* Router = ResolveSubsystem(World, Ar);
		if (Router == nullptr)
		{
			return;
		}

		const FString Path = Args.Num() > 0
			? Args[0]
			: FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("OSCulator.json"));

		const TArray<FOscuExposedTagInfo> Tags = Router->Introspect(EOscuIntrospectFilter::All);

		FString Json;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);

		Writer->WriteObjectStart();
		Writer->WriteArrayStart(TEXT("tags"));

		for (const FOscuExposedTagInfo& TagInfo : Tags)
		{
			Writer->WriteObjectStart();
			Writer->WriteValue(TEXT("tag"), TagInfo.Tag.ToString());
			Writer->WriteValue(TEXT("actorCount"), TagInfo.Actors.Num());

			Writer->WriteArrayStart(TEXT("functions"));
			for (const FOscuExposedFunctionInfo& Function : TagInfo.Functions)
			{
				Writer->WriteObjectStart();
				Writer->WriteValue(TEXT("address"), Function.Address);
				Writer->WriteValue(TEXT("function"), Function.FunctionName.ToString());
				Writer->WriteValue(TEXT("argCount"), Function.TotalArgCount);
				Writer->WriteValue(TEXT("variadic"), Function.bVariadic);
				Writer->WriteValue(TEXT("blueprintAuthored"), Function.bBlueprintAuthored);
				Writer->WriteValue(TEXT("hasOutParams"), Function.bHasOutParams);

				Writer->WriteArrayStart(TEXT("params"));
				for (const FOscuExposedParamInfo& Param : Function.Params)
				{
					Writer->WriteObjectStart();
					Writer->WriteValue(TEXT("name"), Param.Name.ToString());
					Writer->WriteValue(TEXT("type"), Param.TypeLabel);
					Writer->WriteValue(TEXT("argCount"), Param.ArgCount);
					Writer->WriteValue(TEXT("outputOnly"), Param.bOutputOnly);
					Writer->WriteObjectEnd();
				}
				Writer->WriteArrayEnd();

				Writer->WriteObjectEnd();
			}
			Writer->WriteArrayEnd();

			Writer->WriteObjectEnd();
		}

		Writer->WriteArrayEnd();
		Writer->WriteObjectEnd();
		Writer->Close();

		if (FFileHelper::SaveStringToFile(Json, *Path))
		{
			Line(Ar, FString::Printf(TEXT("[OSCulator] Exported %d tag(s) to %s"), Tags.Num(), *Path));
		}
		else
		{
			Line(Ar, FString::Printf(TEXT("[OSCulator] Could not write %s"), *Path));
		}
	}
}

static FAutoConsoleCommandWithWorldArgsAndOutputDevice GOscuListCommand(
	TEXT("OSCulator.List"),
	TEXT("Lists the OSC surface.\n")
	TEXT("  OSCulator.List          every exposed function\n")
	TEXT("  OSCulator.List Custom   Blueprint-authored functions only\n")
	TEXT("  OSCulator.List Actors   tags and their actors, no functions\n")
	TEXT("  OSCulator.List <tag>    one tag in full"),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&ListCommand));

static FAutoConsoleCommandWithWorldArgsAndOutputDevice GOscuSendCommand(
	TEXT("OSCulator.Send"),
	TEXT("Synthesises a message and dispatches it, exactly as the network would.\n")
	TEXT("  OSCulator.Send /laser/Fire 0 0 1 burst 0.5\n")
	TEXT("  OSCulator.Send -lenient /laser/Fire 0.5   (fills what it can, zeroes the rest)"),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&SendCommand));

static FAutoConsoleCommandWithWorldArgsAndOutputDevice GOscuExportCommand(
	TEXT("OSCulator.Export"),
	TEXT("Writes the OSC surface to JSON. Defaults to Saved/OSCulator.json."),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&ExportCommand));
