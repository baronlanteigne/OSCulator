// Copyright Baron Lanteigne. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "OscuIntrospection.h"
#include "OscuMarshal.h"
#include "OscuValue.h"
#include "OscuRouterSubsystem.generated.h"

class AActor;
class UFunction;
class UOscuSettings;

/** One exposed function on one class, resolved once. */
struct FOscuFunctionBinding
{
	/** Weak: a Blueprint recompile replaces the UFunction, and the stale entry
	 *  should rebuild rather than dispatch into freed memory. */
	TWeakObjectPtr<UFunction> Function;

	/** Everything introspection needs. Address is left blank here and filled in
	 *  per tag, since one class can answer under several tags. */
	FOscuExposedFunctionInfo Info;
};

/** Every exposed function on one class. Built once per class, per world session. */
struct FOscuClassExposure
{
	TMap<FName, FOscuFunctionBinding> ByAddressName;
};

/**
 * Maps tags to actors, and classes to their callable surface.
 *
 * A world subsystem rather than a game-instance one, so it dies cleanly with each
 * PIE session instead of carrying a previous run's actors into the next.
 */
UCLASS()
class OSCULATORCORE_API UOscuRouterSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	static UOscuRouterSubsystem* Get(const UWorld* World);

	/** Adds an actor under each of its prefixed tags. Safe to call twice. */
	void RegisterActor(AActor* Actor);

	/** Rebuilds the whole registry from a fresh actor sweep. */
	void ScanWorld();

	/**
	 * Live actors under a tag, compacting away any that have been destroyed.
	 *
	 * Stale weak pointers are purged here rather than on an actor-destroyed
	 * delegate: cheaper, simpler, and it cannot miss a teardown path.
	 */
	void GatherActors(FName Tag, TArray<AActor*>& OutActors);

	/**
	 * Routes one message to every tagged actor that has the function.
	 *
	 * The address must be /tag/function -- no wildcards, no deeper nesting.
	 *
	 * Returns how many actors were actually called. When OutError is supplied it
	 * receives any rejection reason and nothing is logged, leaving reporting to the
	 * caller; when it is null the reason is logged instead.
	 *
	 * Game thread only: ProcessEvent is not safe anywhere else.
	 */
	int32 DispatchMessage(const FOscuMessage& Message, EOscuArgPolicy Policy, FString* OutError = nullptr);

	/**
	 * Describes the registry. One function, three consumers: the console
	 * commands, OSC self-describe, and MIDI auto-populate.
	 */
	TArray<FOscuExposedTagInfo> Introspect(EOscuIntrospectFilter Filter, FName TagFilter = NAME_None) const;

	/** Cached exposure for a class, built and logged on first sight. Never null. */
	const FOscuClassExposure& GetClassExposure(UClass* Class) const;

	int32 GetNumTags() const { return TagToActors.Num(); }

	/**
	 * Distinct addresses that arrived but matched nothing, for diagnostics.
	 *
	 * This is the answer to "I'm sending it and nothing happens" -- if the address
	 * is in here, it reached us and we could not route it.
	 */
	const TSet<FString>& GetUnroutableAddresses() const { return ReportedBadAddresses; }

	/**
	 * True when the address resolves to a function that takes no arguments.
	 *
	 * Such a function is a trigger and must fire on every hit rather than being
	 * collapsed by coalescing. The question has to be asked of the FUNCTION, not of
	 * the message: senders routinely append surplus arguments a trigger ignores, so
	 * a message carrying values may still be addressing a zero-argument function.
	 */
	bool IsTriggerAddress(const FString& Address) const;

private:
	void OnActorSpawned(AActor* Actor);

	/**
	 * Whether a rejection is worth a log line, given how many times we have already
	 * said it. Rejections are reachable from the network at line rate.
	 */
	bool ShouldLogRejection(const FString& Address);
	TSharedRef<FOscuClassExposure> BuildClassExposure(UClass* Class) const;

	/** Tag with the prefix already stripped -> the actors carrying it. Weak,
	 *  because actors get destroyed mid-performance. */
	TMap<FName, TArray<TWeakObjectPtr<AActor>>> TagToActors;

	/** Keyed weakly so a Blueprint recompile drops the stale entry by itself. */
	mutable TMap<TWeakObjectPtr<UClass>, TSharedRef<FOscuClassExposure>> ClassExposureCache;

	FDelegateHandle ActorSpawnedHandle;

	/** Addresses already complained about, so a rejected 60 Hz stream costs one log
	 *  line rather than sixty a second. Bounded, because these are network input. */
	TSet<FString> ReportedBadAddresses;
	bool bReportedAddressCapHit = false;

	/** Only populated for addresses that actually resolve, so a flood of unknown
	 *  ones cannot grow it. Dropped whenever a class exposure is invalidated. */
	mutable TMap<FString, bool> TriggerAddressCache;
};
