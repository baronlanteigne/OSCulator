// Copyright Baron Lanteigne. All Rights Reserved.

#include "OSCulatorCore.h"
#include "OscuOSCSubsystem.h"
#include "OscuRouterSubsystem.h"
#include "OscuSettings.h"

#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

namespace
{
	void StatusCommand(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		UOscuOSCSubsystem* OSC = UOscuOSCSubsystem::Get(World);
		if (OSC == nullptr)
		{
			Ar.Log(TEXT("[OSCulator] No OSC subsystem for this world. OSC runs in PIE and packaged builds only."));
			return;
		}

		const UOscuSettings& Settings = *UOscuSettings::Get();

		if (!OSC->IsListening())
		{
			Ar.Log(Settings.bEnableOSCIn
				? TEXT("[OSCulator] OSC input is enabled but NOT listening -- the bind failed. Check the log for the reason.")
				: TEXT("[OSCulator] OSC input is disabled in Project Settings > Plugins > OSCulator."));
			return;
		}

		Ar.Log(*FString::Printf(TEXT("[OSCulator] Listening on %s:%d"), *Settings.BindAddress, OSC->GetListenPort()));
		Ar.Log(*FString::Printf(TEXT("  packets   received %llu, malformed %llu, from blocked senders %llu"),
			OSC->GetPacketsReceived(), OSC->GetPacketsRejected(), OSC->GetPacketsFromBlockedSenders()));
		Ar.Log(*FString::Printf(TEXT("  messages  drained %llu, coalesced away %llu, dispatched %llu"),
			OSC->GetMessagesDrained(), OSC->GetMessagesCoalescedAway(), OSC->GetMessagesDispatched()));

		if (Settings.AllowedSenderIPs.Num() > 0)
		{
			Ar.Log(*FString::Printf(TEXT("  senders   restricted to %s"), *FString::Join(Settings.AllowedSenderIPs, TEXT(", "))));
		}

		// The answer to "I'm sending it and nothing happens": if the address shows
		// up here, it reached us and could not be routed.
		if (const UOscuRouterSubsystem* Router = UOscuRouterSubsystem::Get(World))
		{
			const TSet<FString>& Unroutable = Router->GetUnroutableAddresses();
			if (Unroutable.Num() > 0)
			{
				TArray<FString> Sorted = Unroutable.Array();
				Sorted.Sort();
				Ar.Log(TEXT("  arrived but matched nothing:"));
				for (const FString& Address : Sorted)
				{
					Ar.Log(*FString::Printf(TEXT("    %s"), *Address));
				}
			}
		}
	}
}

static FAutoConsoleCommandWithWorldArgsAndOutputDevice GOscuStatusCommand(
	TEXT("OSCulator.Status"),
	TEXT("Reports whether OSC input is listening, and the packet and message counters."),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&StatusCommand));
