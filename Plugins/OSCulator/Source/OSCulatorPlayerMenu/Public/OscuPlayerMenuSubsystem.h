// Copyright Baron Lanteigne. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineBaseTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "OscuPlayerMenuSubsystem.generated.h"

class SOscuPlayerMenu;
class SOverlay;
class SWidget;
class UGameViewportClient;
class UWorld;
class FOscuPlayerMenuInput;

/**
 * Puts the player menu on this game instance's viewport: the corner button, the key,
 * and the menu itself. One per game instance, so each PIE client has its own.
 *
 * Does nothing at all unless Project Settings > OSCulator > Player Menu is enabled.
 */
UCLASS()
class OSCULATORPLAYERMENU_API UOscuPlayerMenuSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	static UOscuPlayerMenuSubsystem* Get(const UObject* WorldContext);

	void Open();
	void Close();
	void Toggle();
	bool IsOpen() const { return Menu.IsValid(); }

	/** For the key handler: whether this instance's viewport is the one being played. */
	bool OwnsActiveViewport() const;

	/** Escape while open: cancels a pending Listen/Learn first, then closes. */
	void HandleEscape();

private:
	void AttachToViewport();
	void DetachFromViewport();
	void RefreshButton();
	void HandleViewportCreated();
	void HandlePostLoadMap(UWorld* World);

	UGameViewportClient* GetViewportClient() const;

	TSharedPtr<SOverlay> Root;
	TSharedPtr<SWidget> Button;
	TSharedPtr<SOscuPlayerMenu> Menu;
	TSharedPtr<FOscuPlayerMenuInput> Input;
	TWeakObjectPtr<UGameViewportClient> AttachedTo;

	FDelegateHandle ViewportCreatedHandle;
	FDelegateHandle PostLoadMapHandle;
	FDelegateHandle SettingsChangedHandle;

	// What opening the menu changed about input, put back on close.
	bool bSavedShowCursor = false;
	bool bSavedIgnoreInput = false;
	bool bSavedHideCursorDuringCapture = true;
	EMouseCaptureMode SavedCaptureMode = EMouseCaptureMode::CapturePermanently;
	EMouseLockMode SavedLockMode = EMouseLockMode::LockOnCapture;
};
