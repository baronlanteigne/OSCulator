// Copyright Baron Lanteigne. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SBox;
class UWorld;
struct FOscuPlayerMenuEntry;
struct FOscuEventOutput;

/**
 * The player menu: OSC port, MIDI devices, and one row per function the project lets
 * the player remap. Every change applies at once and is saved by FOscuPlayerMappings;
 * there is no Apply button to forget.
 */
class SOscuPlayerMenu : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SOscuPlayerMenu) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UWorld>, World)
		SLATE_EVENT(FSimpleDelegate, OnClose)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SOscuPlayerMenu() override;

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual void Tick(const FGeometry& AllottedGeometry, double InCurrentTime, float InDeltaTime) override;

	/** Stops a pending Listen or Learn. True if one was pending. */
	bool CancelCapture();

private:
	enum class ECapture : uint8 { None, OSC, MIDI, MIDIOut };

	/** Deferred to the next tick: a settings or theme change can arrive from inside one of this menu's own callbacks. */
	void Rebuild();

	/** The whole menu, after applying the current theme. */
	void BuildAll();
	void BuildBody();
	TSharedRef<SWidget> BuildThemePicker();
	TSharedRef<SWidget> BuildConnection();
	TSharedRef<SWidget> BuildDevices(bool bOutputs);
	TSharedRef<SWidget> BuildControls();
	TSharedRef<SWidget> BuildRow(int32 EntryIndex, bool bShowOSC, bool bShowMIDI, bool bDivider);
	TSharedRef<SWidget> BuildOutputs();
	TSharedRef<SWidget> BuildOutputRow(int32 OutputIndex, bool bShowOSC, bool bShowMIDI, bool bDivider);

	void StartOSCCapture(int32 EntryIndex);
	void StartMIDICapture(int32 EntryIndex);
	void StartMIDIOutCapture(int32 OutputIndex);

	void SetStatus(const FText& Text, bool bError = false);

	const FOscuPlayerMenuEntry* GetEntry(int32 EntryIndex) const;
	const FOscuEventOutput* GetOutput(int32 OutputIndex) const;
	FText DescribeMIDI(int32 EntryIndex, bool& bOutCustom) const;
	FText DescribeOSCPort() const;
	FString GetLocalAddress() const;

	TWeakObjectPtr<UWorld> World;
	FSimpleDelegate OnClose;

	TSharedPtr<SBox> Body;

	FText Status;
	bool bStatusError = false;

	ECapture Capturing = ECapture::None;
	int32 CapturingIndex = INDEX_NONE;

	/** Settings changed (port, devices): rebuild on the next tick, never inside the callback that caused it. */
	bool bRebuildPending = false;

	FDelegateHandle SettingsChangedHandle;
};
