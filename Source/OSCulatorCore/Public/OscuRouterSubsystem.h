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
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
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
	 * Forgets the cached signature of a class, and of everything derived from it.
	 *
	 * Derived classes too, because a child's exposure includes what it inherits: a
	 * parent gaining or losing a function changes the child's callable surface without
	 * the child itself being touched.
	 *
	 * Public so a caller that knows a class changed can say so. Called automatically on
	 * Blueprint recompile in the editor, which is the case that actually matters.
	 */
	void InvalidateClassExposure(const UClass* Class);

	/**
	 * The exposure of the first class under a tag that has this function, or null.
	 *
	 * Allocation-free: it walks the registry's own actor array rather than copying it,
	 * and stops at the first class that has the function. That matters because the MIDI
	 * side calls this per message for any binding that names a parameter.
	 */
	const FOscuFunctionBinding* FindExposedFunction(FName Tag, FName FunctionName) const;

	/**
	 * How many times a class's callable surface has been walked by reflection.
	 *
	 * Rises once per class on first sight, and again whenever something invalidated an
	 * entry and it was asked for afterwards. A flat count across a dispatch storm means
	 * the cache is doing its job; a climbing one means something is invalidating more
	 * than it should.
	 *
	 * It exists because "did it look again, or answer from cache?" has no other
	 * observable. Comparing the returned object's address does not work: freeing a
	 * cached exposure and immediately building its replacement can land on the same
	 * address, which reads as a cache hit that never happened.
	 */
	uint64 GetClassExposureBuildCount() const { return ClassExposureBuilds; }

	/**
	 * Describes the registry. One function, three consumers: the console
	 * commands, OSC self-describe, and MIDI auto-populate.
	 */
	TArray<FOscuExposedTagInfo> Introspect(EOscuIntrospectFilter Filter, FName TagFilter = NAME_None) const;

	/** Cached exposure for a class, built and logged on first sight. Never null. */
	const FOscuClassExposure& GetClassExposure(UClass* Class) const;

	/**
	 * Where a named parameter sits in the argument list, for a /tag/function.
	 *
	 * Returns the first wire slot the parameter occupies, or INDEX_NONE if nothing
	 * tagged has the function or no parameter goes by that name. A slot is not a
	 * parameter index: a vec3 occupies three slots and an output pin occupies none, so
	 * this is a running sum over the signature rather than a position in it.
	 *
	 * Here rather than in the MIDI module because the per-class exposure cache is here,
	 * already invalidated on Blueprint recompile -- and because the answer is a fact
	 * about a signature, which has nothing to do with MIDI. MIDI is simply the first
	 * caller that needs to address a parameter by name instead of by order.
	 *
	 * OutArgCount receives how many slots the parameter occupies, so a caller can tell
	 * a float apart from a vec3 it is about to under-fill.
	 */
	int32 FindParamSlot(FName Tag, FName FunctionName, FName ParamName, int32* OutArgCount = nullptr) const;

	/**
	 * Every parameter of a /tag/function, in declaration order, with its slot.
	 *
	 * For showing an author what names are available to assign, which is the other half
	 * of addressing parameters by name: a name you cannot discover is a name you cannot
	 * type. Empty when nothing tagged exposes the function.
	 */
	bool DescribeParams(FName Tag, FName FunctionName, TArray<FOscuExposedParamInfo>& OutParams, TArray<int32>& OutSlots) const;

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

	/** Reflection walks performed. See GetClassExposureBuildCount. */
	mutable uint64 ClassExposureBuilds = 0;

	FDelegateHandle ActorSpawnedHandle;

	/** Addresses already complained about, so a rejected 60 Hz stream costs one log
	 *  line rather than sixty a second. Bounded, because these are network input. */
	TSet<FString> ReportedBadAddresses;
	bool bReportedAddressCapHit = false;

	/** Only populated for addresses that actually resolve, so a flood of unknown
	 *  ones cannot grow it. Dropped whenever a class exposure is invalidated. */
	mutable TMap<FString, bool> TriggerAddressCache;

#if WITH_EDITOR
	/**
	 * Drops a recompiled class's cached signature, so the next question is answered by
	 * the new class rather than the old one.
	 *
	 * The editor's whole loop is edit, compile, try again -- so a reflection cache that
	 * only ever filled was guaranteed to go stale, and did: a deleted function kept being
	 * offered and a newly added one kept being missed, both because the cache was never
	 * asked to forget anything.
	 */
	void HandleClassRecompiled(UObject* CDO, const struct FObjectPostCDOCompiledContext& Context);

	/**
	 * Follows reinstanced actors to their replacements, and forgets replaced classes.
	 *
	 * Recompiling a Blueprint destroys every instance of it and builds new ones. That
	 * does not go through the spawn path, so OnActorSpawned never fires and the registry
	 * would simply lose those actors until something rescanned the world.
	 */
	void HandleObjectsReinstanced(const TMap<UObject*, UObject*>& ReplacementMap);

	FDelegateHandle CDOCompiledHandle;
	FDelegateHandle ObjectsReinstancedHandle;
#endif
};
