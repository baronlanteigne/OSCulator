// Copyright Baron Lanteigne. All Rights Reserved.

#include "OscuOSCSubsystem.h"

#include "OSCulatorCore.h"
#include "OscuOSCReceiver.h"
#include "OscuMarshal.h"
#include "OscuRouterSubsystem.h"
#include "OscuSettings.h"

#include "Engine/World.h"

// The whole game-thread cost of OSC input for one frame: draining the queue,
// coalescing, and every dispatch it decided to make.
DECLARE_CYCLE_STAT(TEXT("Drain (total per frame)"), STAT_OscuDrain, STATGROUP_OSCulator);

// Everything except the dispatching, which is the part the plugin actually owns.
DECLARE_CYCLE_STAT(TEXT("Coalesce"), STAT_OscuCoalesce, STATGROUP_OSCulator);

// Per-frame counters, so the coalescing ratio is visible at a glance: a healthy
// overloaded stream shows Received climbing while Dispatched stays flat.
DECLARE_DWORD_COUNTER_STAT(TEXT("Messages drained"), STAT_OscuDrained, STATGROUP_OSCulator);
DECLARE_DWORD_COUNTER_STAT(TEXT("Messages coalesced away"), STAT_OscuCoalescedAway, STATGROUP_OSCulator);
DECLARE_DWORD_COUNTER_STAT(TEXT("Messages dispatched"), STAT_OscuDispatched, STATGROUP_OSCulator);

namespace OscuOSC
{
	void CoalesceBatch(
		TArray<FOscuMessage>& InOutMessages,
		const TSet<FString>& NoCoalesceAddresses,
		TFunctionRef<bool(const FOscuMessage&)> IsTrigger)
	{
		if (InOutMessages.Num() < 2)
		{
			return;
		}

		TMap<FString, int32> FirstIndexForAddress;
		TArray<FOscuMessage> Result;
		Result.Reserve(InOutMessages.Num());

		for (FOscuMessage& Message : InOutMessages)
		{
			const bool bCoalesce = !IsTrigger(Message) && !NoCoalesceAddresses.Contains(Message.Address);

			if (bCoalesce)
			{
				if (const int32* Existing = FirstIndexForAddress.Find(Message.Address))
				{
					// Last value wins, at the position the address first appeared.
					Result[*Existing] = MoveTemp(Message);
					continue;
				}
				FirstIndexForAddress.Add(Message.Address, Result.Num());
			}

			Result.Add(MoveTemp(Message));
		}

		InOutMessages = MoveTemp(Result);
	}
}

bool UOscuOSCSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	// OSC runs in PIE and packaged builds only. Nothing listens in an editor world.
	const UWorld* World = Cast<UWorld>(Outer);
	return World != nullptr && World->IsGameWorld();
}

void UOscuOSCSubsystem::Restart()
{
	// Only the receiver needs the tick hook; senders are driven by Blueprint calls.
	if (PreActorTickHandle.IsValid())
	{
		FWorldDelegates::OnWorldPreActorTick.Remove(PreActorTickHandle);
		PreActorTickHandle.Reset();
	}
	Receiver.Reset();
	CloseSenders();

	OpenReceiver();
	OpenSenders();
}

void UOscuOSCSubsystem::OpenSenders()
{
	const UOscuSettings& Settings = *UOscuSettings::Get();
	if (!Settings.bEnableOSCOut)
	{
		return;
	}

	if (Settings.OSCTargets.Num() == 0)
	{
		UE_LOG(LogOSCulator, Warning,
			TEXT("OSC output is enabled but no targets are configured in Project Settings > Plugins > OSCulator."));
		return;
	}

	for (const FOscuOSCTarget& Target : Settings.OSCTargets)
	{
		if (!Target.bEnabled)
		{
			continue;
		}

		TUniquePtr<FOscuOSCSender> Sender = MakeUnique<FOscuOSCSender>();
		if (Sender->Open(Target.Name, Target.Host, Target.Port))
		{
			Senders.Add(MoveTemp(Sender));
		}
	}

	UE_LOG(LogOSCulator, Log, TEXT("OSC output: %d of %d target(s) ready."), Senders.Num(), Settings.OSCTargets.Num());
}

void UOscuOSCSubsystem::CloseSenders()
{
	Senders.Reset();
}

void UOscuOSCSubsystem::BuildDescribeReply(TArray<FOscuMessage>& OutMessages) const
{
	OutMessages.Reset();

	const UOscuRouterSubsystem* Router = UOscuRouterSubsystem::Get(GetWorld());
	if (Router == nullptr)
	{
		return;
	}

	// Blueprint-authored only. Describing a couple of hundred inherited engine
	// functions would drown the thing asking.
	const TArray<FOscuExposedTagInfo> Tags = Router->Introspect(EOscuIntrospectFilter::Custom);

	FOscuMessage& Begin = OutMessages.AddDefaulted_GetRef();
	Begin.Address = TEXT("/_describe/begin");

	int32 FunctionCount = 0;
	for (const FOscuExposedTagInfo& TagInfo : Tags)
	{
		for (const FOscuExposedFunctionInfo& Function : TagInfo.Functions)
		{
			// One flat message per function, all strings and ints, so anything that
			// speaks OSC can read it without a parser.
			FOscuMessage& Entry = OutMessages.AddDefaulted_GetRef();
			Entry.Address = TEXT("/_describe/function");
			Entry.Args.Add(FOscuValue::MakeString(Function.Address));
			Entry.Args.Add(FOscuValue::MakeString(TagInfo.Tag.ToString()));
			Entry.Args.Add(FOscuValue::MakeString(Function.FunctionName.ToString()));
			Entry.Args.Add(FOscuValue::MakeInt(Function.TotalArgCount));
			Entry.Args.Add(FOscuValue::MakeString(Function.GetSignatureString()));
			Entry.Args.Add(FOscuValue::MakeBool(Function.bVariadic));
			++FunctionCount;
		}
	}

	FOscuMessage& End = OutMessages.AddDefaulted_GetRef();
	End.Address = TEXT("/_describe/end");
	End.Args.Add(FOscuValue::MakeInt(FunctionCount));
}

bool UOscuOSCSubsystem::HandleDescribeRequest(const FOscuMessage& Message)
{
	if (DescribeAddress.IsEmpty() || Message.Address != DescribeAddress)
	{
		return false;
	}

	TArray<FOscuMessage> Reply;
	BuildDescribeReply(Reply);

	// An integer argument names the port to answer on. Without it there is no way
	// to know where the asker listens -- its source port is ephemeral, not the one
	// it receives on -- so the reply goes to the configured targets instead.
	if (Message.Args.Num() > 0)
	{
		const int32 ReplyPort = static_cast<int32>(Message.Args[0].AsNumber());
		if (ReplyPort > 0 && ReplyPort <= 65535)
		{
			const FString Host = FString::Printf(TEXT("%d.%d.%d.%d"),
				(Message.SourceIP >> 24) & 0xFF, (Message.SourceIP >> 16) & 0xFF,
				(Message.SourceIP >> 8) & 0xFF, Message.SourceIP & 0xFF);

			FOscuOSCSender Direct;
			if (Direct.Open(FName("_describe"), Host, ReplyPort))
			{
				for (const FOscuMessage& Entry : Reply)
				{
					Direct.Send(Entry);
				}
				UE_LOG(LogOSCulator, Log, TEXT("Described %d function(s) to %s:%d."), Reply.Num() - 2, *Host, ReplyPort);
			}
			return true;
		}
	}

	for (const FOscuMessage& Entry : Reply)
	{
		SendMessage(Entry, NAME_None);
	}
	UE_LOG(LogOSCulator, Log, TEXT("Described %d function(s) to every configured target."), Reply.Num() - 2);
	return true;
}

int32 UOscuOSCSubsystem::SendMessage(const FOscuMessage& Message, FName Target)
{
	int32 SentCount = 0;
	bool bMatchedName = false;

	for (const TUniquePtr<FOscuOSCSender>& Sender : Senders)
	{
		if (!Target.IsNone() && Sender->GetName() != Target)
		{
			continue;
		}
		bMatchedName = true;

		if (Sender->Send(Message))
		{
			++SentCount;
		}
	}

	if (!Target.IsNone() && !bMatchedName)
	{
		// Naming a target that is not configured is an authoring mistake, and
		// silence here looks exactly like a network problem.
		UE_LOG(LogOSCulator, Warning, TEXT("No OSC target named '%s' is configured. '%s' was not sent."),
			*Target.ToString(), *Message.Address);
	}
	else if (Senders.Num() == 0)
	{
		UE_LOG(LogOSCulator, Verbose, TEXT("No OSC targets are open; '%s' went nowhere."), *Message.Address);
	}

	return SentCount;
}

void UOscuOSCSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

#if STATS
	// A stat group is registered lazily, the first time a stat inside it actually
	// executes. Without this, "stat OSCulator" reports nothing at all whenever the
	// socket failed to bind or no message has arrived yet -- which is exactly when
	// someone is most likely to go looking for it. Touching the IDs here makes the
	// group exist for the whole session.
	(void)GET_STATID(STAT_OscuDrain);
	(void)GET_STATID(STAT_OscuCoalesce);
	(void)GET_STATID(STAT_OscuDrained);
	(void)GET_STATID(STAT_OscuCoalescedAway);
	(void)GET_STATID(STAT_OscuDispatched);
#endif

	OpenReceiver();
	OpenSenders();

	// Editing a port or a target should take effect without leaving play.
	SettingsChangedHandle = UOscuSettings::OnSettingsChanged.AddUObject(this, &UOscuOSCSubsystem::Restart);
}

void UOscuOSCSubsystem::OpenReceiver()
{
	const UOscuSettings& Settings = *UOscuSettings::Get();
	if (!Settings.bEnableOSCIn)
	{
		// The gate is real: no socket, no thread, no tick hook.
		UE_LOG(LogOSCulator, Log, TEXT("OSC input is disabled in settings."));
		return;
	}

	NoCoalesceAddresses.Reset();
	for (const FString& Address : Settings.NoCoalesceAddresses)
	{
		NoCoalesceAddresses.Add(Address);
	}

	DescribeAddress = Settings.bEnableOSCOut ? Settings.DescribeAddress : FString();

	Receiver = MakeUnique<FOscuOSCReceiver>();
	if (!Receiver->Start(Settings.BindAddress, Settings.ListenPort, Settings.AllowedSenderIPs, Settings.ReceiveBufferSize))
	{
		Receiver.Reset();
		return;
	}

	PreActorTickHandle = FWorldDelegates::OnWorldPreActorTick.AddUObject(this, &UOscuOSCSubsystem::HandleWorldPreActorTick);
}

void UOscuOSCSubsystem::Deinitialize()
{
	if (PreActorTickHandle.IsValid())
	{
		FWorldDelegates::OnWorldPreActorTick.Remove(PreActorTickHandle);
		PreActorTickHandle.Reset();
	}

	if (SettingsChangedHandle.IsValid())
	{
		UOscuSettings::OnSettingsChanged.Remove(SettingsChangedHandle);
		SettingsChangedHandle.Reset();
	}

	// Joins the receive thread and closes the socket before the world goes away.
	Receiver.Reset();
	CloseSenders();

	Super::Deinitialize();
}

UOscuOSCSubsystem* UOscuOSCSubsystem::Get(const UWorld* World)
{
	return World != nullptr ? World->GetSubsystem<UOscuOSCSubsystem>() : nullptr;
}

bool UOscuOSCSubsystem::IsListening() const
{
	return Receiver.IsValid() && Receiver->IsListening();
}

int32 UOscuOSCSubsystem::GetListenPort() const
{
	return Receiver.IsValid() ? Receiver->GetBoundPort() : 0;
}

uint64 UOscuOSCSubsystem::GetPacketsReceived() const
{
	return Receiver.IsValid() ? Receiver->GetPacketsReceived() : 0;
}

uint64 UOscuOSCSubsystem::GetPacketsRejected() const
{
	return Receiver.IsValid() ? Receiver->GetPacketsRejected() : 0;
}

uint64 UOscuOSCSubsystem::GetPacketsFromBlockedSenders() const
{
	return Receiver.IsValid() ? Receiver->GetPacketsFromBlockedSenders() : 0;
}

void UOscuOSCSubsystem::HandleWorldPreActorTick(UWorld* TickingWorld, ELevelTick TickType, float DeltaSeconds)
{
	// The delegate is global; several worlds can tick in one PIE session.
	if (TickingWorld != GetWorld())
	{
		return;
	}

	Drain();
}

void UOscuOSCSubsystem::Drain()
{
	if (!Receiver.IsValid())
	{
		return;
	}

	SCOPE_CYCLE_COUNTER(STAT_OscuDrain);

	Batch.Reset();

	FOscuMessage Message;
	while (Receiver->Dequeue(Message))
	{
		Batch.Add(MoveTemp(Message));
	}

	if (Batch.Num() == 0)
	{
		return;
	}

	MessagesDrained += Batch.Num();
	INC_DWORD_STAT_BY(STAT_OscuDrained, Batch.Num());

	UOscuRouterSubsystem* Router = UOscuRouterSubsystem::Get(GetWorld());
	if (Router == nullptr)
	{
		return;
	}

	const int32 BeforeCoalescing = Batch.Num();
	{
		SCOPE_CYCLE_COUNTER(STAT_OscuCoalesce);
		OscuOSC::CoalesceBatch(Batch, NoCoalesceAddresses,
			[Router](const FOscuMessage& Message)
			{
				// A message with no arguments cannot be anything but a trigger, and
				// answering that without a lookup keeps the common case cheap.
				return Message.Args.Num() == 0 || Router->IsTriggerAddress(Message.Address);
			});
	}
	MessagesCoalescedAway += BeforeCoalescing - Batch.Num();
	INC_DWORD_STAT_BY(STAT_OscuCoalescedAway, BeforeCoalescing - Batch.Num());
	INC_DWORD_STAT_BY(STAT_OscuDispatched, Batch.Num());

	for (const FOscuMessage& Dispatchable : Batch)
	{
		// Control traffic in the reserved "/_" namespace is answered, not routed.
		if (HandleDescribeRequest(Dispatchable))
		{
			continue;
		}

		// Strict: OSC senders know the signature, so a wrong count is a mistake
		// worth naming rather than papering over.
		Router->DispatchMessage(Dispatchable, EOscuArgPolicy::Strict);
		++MessagesDispatched;
	}
}
