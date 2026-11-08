// Copyright Baron Lanteigne. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Sound/QuartzQuantizationUtilities.h"
#include "OscuPulseComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOscuPulseEvent, int32, Index);

/**
 * An event on every pulse of a division of the OSCulator clock: every 1/8, every dotted
 * 1/4, every bar. For visuals, lights, OSC and DMX out, and logic. For sound that must
 * land sample-accurate, use Play On Pulse on the clock instead.
 *
 * Counted from the clock's position, never added up per frame, so it does not drift.
 * The same rules as the clock's On Beat: a pulse fires on the first frame after it, so
 * up to a frame late, and after a hitch that skipped several only the latest fires.
 * Nothing fires while the clock is stopped. Start fires pulse 0 at once; Continue mid-pulse
 * waits for the next one.
 *
 * Index counts pulses since beat 0, from 0, the same as Get Pulse Index.
 */
UCLASS(ClassGroup = (OSCulator), meta = (BlueprintSpawnableComponent, DisplayName = "OSCu Pulse"))
class OSCULATORCORE_API UOscuPulseComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UOscuPulseComponent();

	/** How often to fire. Beat is a quarter note; Bar follows the clock's Beats Per Bar. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pulse")
	EQuartzCommandQuantization Division = EQuartzCommandQuantization::Beat;

	/**
	 * How far into each pulse to fire, 0-1 of one pulse. 0.5 fires halfway between two
	 * pulses, which is how two effects at the same rate interlace.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pulse", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float Offset = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pulse", meta = (DisplayName = "Enabled"))
	bool bEnabled = true;

	/** Once per pulse. Index counts pulses since beat 0, from 0. */
	UPROPERTY(BlueprintAssignable, Category = "Pulse")
	FOscuPulseEvent OnPulse;

	/** 0-1 through the current pulse, Offset included: 0 is the moment On Pulse fires. */
	UFUNCTION(BlueprintPure, Category = "Pulse")
	float GetPhase() const;

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Fires the pulse if one is due. Called every tick; public so a test can step it. */
	void Evaluate();

	/** Bound to the clock's On Start: Start and Continue both move the position, so count afresh. */
	UFUNCTION()
	void HandleClockStart();

private:
	/** Pulses since beat 0, less Offset, fractional. False when the clock or Division gives none. */
	bool GetPulses(double& OutPulses) const;

	/** The last pulse fired, or the last one already past when counting started. */
	int64 LastFiredIndex = -1;

	/** False until counting has started from the current position, Division and Offset. */
	bool bCounting = false;

	EQuartzCommandQuantization CountedDivision = EQuartzCommandQuantization::None;
	float CountedOffset = 0.0f;
};
