// Copyright Baron Lanteigne. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "OscuValue.h"
#include "Subsystems/WorldSubsystem.h"

// Not forward-declared: UHT emits this class's constructor into the generated
// .gen.cpp, which instantiates ~TUniquePtr<FOscuOSCReceiver> and so needs the
// complete type. The receiver header is cheap -- Core plus FOscuValue.
#include "OscuOSCReceiver.h"
#include "OscuOSCSender.h"

#include "OscuOSCSubsystem.generated.h"

namespace OscuOSC
{
	/**
	 * Collapses a frame's worth of messages to last-wins per address.
	 *
	 * A 60 Hz stream across 20 parameters is 1200 dispatches per frame if handled
	 * naively; only the final value of each address can possibly matter.
	 *
	 * Position is taken from an address's FIRST appearance and the value from its
	 * LAST, so relative ordering between different addresses is preserved.
	 *
	 * Two things are never collapsed:
	 *  - Triggers, as decided by IsTrigger. This asks about the target FUNCTION's
	 *    arity rather than the message's, because a sender may append surplus
	 *    arguments that a zero-argument function simply discards -- so a message
	 *    carrying values can still be addressing a trigger.
	 *  - Anything named in NoCoalesceAddresses.
	 */
	OSCULATOROSC_API void CoalesceBatch(
		TArray<FOscuMessage>& InOutMessages,
		const TSet<FString>& NoCoalesceAddresses,
		TFunctionRef<bool(const FOscuMessage&)> IsTrigger);
}

/**
 * Owns OSC input for one world: the receive thread, the drain, and the hand-off
 * to the router.
 *
 * Lives in the OSC module so that the router stays protocol-agnostic -- MIDI will
 * hand it messages through the same door.
 */
UCLASS()
class OSCULATOROSC_API UOscuOSCSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	static UOscuOSCSubsystem* Get(const UWorld* World);

	bool IsListening() const;
	int32 GetListenPort() const;

	/**
	 * Sends to one configured target, or to every enabled target when Target is
	 * None. Returns how many targets accepted it.
	 *
	 * Naming a target that does not exist is a mistake worth reporting, not a
	 * silent no-op.
	 */
	int32 SendMessage(const FOscuMessage& Message, FName Target = NAME_None);

	int32 GetTargetCount() const { return Senders.Num(); }
	const TArray<TUniquePtr<FOscuOSCSender>>& GetSenders() const { return Senders; }

	/** Closes and reopens both directions, re-reading settings. */
	void Restart();

	/**
	 * Serialises the callable surface into a reply, one message per function.
	 *
	 * The third consumer of Introspect(), after the console commands and MIDI
	 * auto-populate. A patch can ask what this build exposes and build its own
	 * senders from the answer, rather than being told the addresses by hand.
	 */
	void BuildDescribeReply(TArray<FOscuMessage>& OutMessages) const;

	uint64 GetPacketsReceived() const;
	uint64 GetPacketsRejected() const;
	uint64 GetPacketsFromBlockedSenders() const;

	uint64 GetMessagesDrained() const { return MessagesDrained; }
	uint64 GetMessagesCoalescedAway() const { return MessagesCoalescedAway; }
	uint64 GetMessagesDispatched() const { return MessagesDispatched; }

private:
	/**
	 * Hooked to FWorldDelegates::OnWorldPreActorTick so dispatched calls land
	 * BEFORE actor ticks. A function called this frame then affects this frame's
	 * tick rather than next frame's.
	 */
	void HandleWorldPreActorTick(UWorld* TickingWorld, ELevelTick TickType, float DeltaSeconds);

	void Drain();

	/** True if the message was a describe request and has been answered. */
	bool HandleDescribeRequest(const FOscuMessage& Message);

	/** Cached from settings so the drain does not read them per message. */
	FString DescribeAddress;

	void OpenReceiver();
	void OpenSenders();
	void CloseSenders();

	TUniquePtr<FOscuOSCReceiver> Receiver;

	/** One per configured target. Sending is point-to-point, unlike receiving. */
	TArray<TUniquePtr<FOscuOSCSender>> Senders;

	FDelegateHandle PreActorTickHandle;
	FDelegateHandle SettingsChangedHandle;

	/** Resolved once at BeginPlay rather than per message. */
	TSet<FString> NoCoalesceAddresses;

	/** Reused every frame so a steady stream allocates nothing after warmup. */
	TArray<FOscuMessage> Batch;

	uint64 MessagesDrained = 0;
	uint64 MessagesCoalescedAway = 0;
	uint64 MessagesDispatched = 0;
};
