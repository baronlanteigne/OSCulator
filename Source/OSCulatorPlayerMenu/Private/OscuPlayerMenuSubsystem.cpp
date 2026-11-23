// Copyright Baron Lanteigne. All Rights Reserved.

#include "OscuPlayerMenuSubsystem.h"

#include "OSCulatorCore.h"
#include "OscuMIDISubsystem.h"
#include "OscuOSCSubsystem.h"
#include "OscuPlayerMappings.h"
#include "OscuSettings.h"
#include "SOscuPlayerMenu.h"

#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "Misc/App.h"
#include "UObject/UObjectGlobals.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SViewport.h"

#define LOCTEXT_NAMESPACE "OSCulatorPlayerMenu"

/**
 * The open key, read before the game sees it. A preprocessor rather than an input
 * binding, so it works whatever the project's own input setup is and while a text box
 * in the menu has focus.
 */
class FOscuPlayerMenuInput : public IInputProcessor
{
public:
	explicit FOscuPlayerMenuInput(UOscuPlayerMenuSubsystem* InOwner) : Owner(InOwner) {}

	virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override {}

	virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& Event) override
	{
		UOscuPlayerMenuSubsystem* Menu = Owner.Get();
		const UOscuSettings& Settings = *UOscuSettings::Get();
		if (Menu == nullptr || !Settings.bEnablePlayerMenu || !Menu->OwnsActiveViewport())
		{
			return false;
		}

		if (Menu->IsOpen() && Event.GetKey() == EKeys::Escape)
		{
			Menu->HandleEscape();
			return true;
		}

		if (Settings.bPlayerMenuKey && Settings.PlayerMenuKey.IsValid()
			&& Event.GetKey() == Settings.PlayerMenuKey && !Event.IsRepeat())
		{
			Menu->Toggle();
			return true;
		}
		return false;
	}

	virtual const TCHAR* GetDebugName() const override { return TEXT("OSCulatorPlayerMenu"); }

private:
	TWeakObjectPtr<UOscuPlayerMenuSubsystem> Owner;
};

namespace
{
	/** Three short bars: reads as "settings" without a glyph the default font may lack. */
	TSharedRef<SWidget> MakeMenuGlyph()
	{
		static const FSlateColorBrush Bar(FLinearColor::White);

		TSharedRef<SVerticalBox> Bars = SNew(SVerticalBox);
		for (int32 Index = 0; Index < 3; ++Index)
		{
			Bars->AddSlot()
			.AutoHeight()
			.Padding(0.0f, Index == 0 ? 0.0f : 4.0f, 0.0f, 0.0f)
			[
				SNew(SBox).WidthOverride(16.0f).HeightOverride(1.5f)
				[
					SNew(SImage).Image(&Bar)
				]
			];
		}
		return Bars;
	}

	const FButtonStyle& CornerButtonStyle()
	{
		static const FButtonStyle Style = []()
		{
			FButtonStyle Out;
			Out.SetNormal(FSlateNoResource());
			Out.SetHovered(FSlateRoundedBoxBrush(FLinearColor(0.0f, 0.0f, 0.0f, 0.35f), 4.0f));
			Out.SetPressed(FSlateRoundedBoxBrush(FLinearColor(0.0f, 0.0f, 0.0f, 0.5f), 4.0f));
			Out.SetNormalPadding(FMargin(8.0f));
			Out.SetPressedPadding(FMargin(8.0f));
			return Out;
		}();
		return Style;
	}
}

bool UOscuPlayerMenuSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return !IsRunningDedicatedServer() && !IsRunningCommandlet() && Super::ShouldCreateSubsystem(Outer);
}

void UOscuPlayerMenuSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	if (FSlateApplication::IsInitialized())
	{
		Input = MakeShared<FOscuPlayerMenuInput>(this);
		FSlateApplication::Get().RegisterInputPreProcessor(Input);
	}

	ViewportCreatedHandle = UGameViewportClient::OnViewportCreated().AddUObject(this, &UOscuPlayerMenuSubsystem::HandleViewportCreated);
	PostLoadMapHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &UOscuPlayerMenuSubsystem::HandlePostLoadMap);
	SettingsChangedHandle = UOscuSettings::OnSettingsChanged.AddUObject(this, &UOscuPlayerMenuSubsystem::RefreshButton);

	AttachToViewport();
}

void UOscuPlayerMenuSubsystem::Deinitialize()
{
	Close();
	DetachFromViewport();

	if (Input.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().UnregisterInputPreProcessor(Input);
	}
	Input.Reset();

	UGameViewportClient::OnViewportCreated().Remove(ViewportCreatedHandle);
	FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadMapHandle);
	UOscuSettings::OnSettingsChanged.Remove(SettingsChangedHandle);

	// Play stopping in the editor: undo what the menu changed, unless it is being saved.
	FOscuPlayerMappings::Get().EndSession();

	Super::Deinitialize();
}

UOscuPlayerMenuSubsystem* UOscuPlayerMenuSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = GEngine != nullptr
		? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull)
		: nullptr;
	const UGameInstance* GameInstance = World != nullptr ? World->GetGameInstance() : nullptr;
	return GameInstance != nullptr ? GameInstance->GetSubsystem<UOscuPlayerMenuSubsystem>() : nullptr;
}

UGameViewportClient* UOscuPlayerMenuSubsystem::GetViewportClient() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	return GameInstance != nullptr ? GameInstance->GetGameViewportClient() : nullptr;
}

bool UOscuPlayerMenuSubsystem::OwnsActiveViewport() const
{
	if (IsOpen())
	{
		return true;
	}

	UGameViewportClient* Viewport = GetViewportClient();
	if (Viewport == nullptr)
	{
		return false;
	}

	// A packaged game has the one viewport. In the editor, only the PIE window being
	// played should answer, not the editor or a second client.
	if (!GIsEditor)
	{
		return true;
	}
	const TSharedPtr<SViewport> Widget = Viewport->GetGameViewportWidget();
	return Widget.IsValid() && Widget->HasAnyUserFocusOrFocusedDescendants();
}

void UOscuPlayerMenuSubsystem::HandleViewportCreated()
{
	AttachToViewport();
}

void UOscuPlayerMenuSubsystem::HandlePostLoadMap(UWorld* World)
{
	if (World != nullptr && World->GetGameInstance() == GetGameInstance())
	{
		AttachToViewport();
	}
}

void UOscuPlayerMenuSubsystem::AttachToViewport()
{
	UGameViewportClient* Viewport = GetViewportClient();
	if (Viewport == nullptr)
	{
		return;
	}

	if (!Root.IsValid())
	{
		Root = SNew(SOverlay).Visibility(EVisibility::SelfHitTestInvisible);
	}

	// Remove first, so a second call (a map load, a late viewport) never stacks two.
	Viewport->RemoveViewportWidgetContent(Root.ToSharedRef());
	Viewport->AddViewportWidgetContent(Root.ToSharedRef(), /*ZOrder*/ 10000);
	AttachedTo = Viewport;

	RefreshButton();
}

void UOscuPlayerMenuSubsystem::DetachFromViewport()
{
	if (UGameViewportClient* Viewport = AttachedTo.Get(); Viewport != nullptr && Root.IsValid())
	{
		Viewport->RemoveViewportWidgetContent(Root.ToSharedRef());
	}
	AttachedTo.Reset();
	Button.Reset();
	Root.Reset();
}

void UOscuPlayerMenuSubsystem::RefreshButton()
{
	if (!Root.IsValid())
	{
		return;
	}

	if (Button.IsValid())
	{
		Root->RemoveSlot(Button.ToSharedRef());
		Button.Reset();
	}

	const UOscuSettings& Settings = *UOscuSettings::Get();
	if (!Settings.bEnablePlayerMenu)
	{
		Close();
		return;
	}
	if (!Settings.bPlayerMenuButton)
	{
		return;
	}

	const bool bLeft = Settings.PlayerMenuCorner == EOscuPlayerMenuCorner::TopLeft || Settings.PlayerMenuCorner == EOscuPlayerMenuCorner::BottomLeft;
	const bool bTop = Settings.PlayerMenuCorner == EOscuPlayerMenuCorner::TopLeft || Settings.PlayerMenuCorner == EOscuPlayerMenuCorner::TopRight;

	TWeakObjectPtr<UOscuPlayerMenuSubsystem> Weak(this);
	TSharedRef<SButton> Made = SNew(SButton)
		.ButtonStyle(&CornerButtonStyle())
		.ToolTipText(Settings.PlayerMenuTitle)
		.Visibility_Lambda([Weak]()
		{
			return Weak.IsValid() && !Weak->IsOpen() ? EVisibility::Visible : EVisibility::Collapsed;
		})
		.OnClicked_Lambda([Weak]()
		{
			if (Weak.IsValid())
			{
				Weak->Open();
			}
			return FReply::Handled();
		})
		[
			MakeMenuGlyph()
		];

	// Faint until the pointer is on it: discreet over a show, findable when wanted.
	TWeakPtr<SButton> WeakButton = Made;
	Made->SetColorAndOpacity(TAttribute<FLinearColor>::CreateLambda([WeakButton]()
	{
		const TSharedPtr<SButton> Pinned = WeakButton.Pin();
		return FLinearColor(1.0f, 1.0f, 1.0f, Pinned.IsValid() && Pinned->IsHovered() ? 0.95f : 0.3f);
	}));

	Button = Made;
	Root->AddSlot()
		.HAlign(bLeft ? HAlign_Left : HAlign_Right)
		.VAlign(bTop ? VAlign_Top : VAlign_Bottom)
		.Padding(12.0f)
		[
			Made
		];
}

void UOscuPlayerMenuSubsystem::Open()
{
	const UOscuSettings& Settings = *UOscuSettings::Get();
	if (IsOpen() || !Settings.bEnablePlayerMenu)
	{
		return;
	}

	AttachToViewport();
	UGameViewportClient* Viewport = AttachedTo.Get();
	if (Viewport == nullptr || !Root.IsValid())
	{
		UE_LOG(LogOSCulator, Warning, TEXT("Player menu: no game viewport to open on."));
		return;
	}

	TWeakObjectPtr<UOscuPlayerMenuSubsystem> Weak(this);
	Menu = SNew(SOscuPlayerMenu)
		.World(GetGameInstance()->GetWorld())
		.OnClose_Lambda([Weak]()
		{
			if (Weak.IsValid())
			{
				Weak->Close();
			}
		});

	Root->AddSlot()[ Menu.ToSharedRef() ];

	// The menu needs a pointer and must keep keys away from the game while open.
	bSavedIgnoreInput = Viewport->IgnoreInput();
	bSavedHideCursorDuringCapture = Viewport->HideCursorDuringCapture();
	SavedCaptureMode = Viewport->GetMouseCaptureMode();
	SavedLockMode = Viewport->GetMouseLockMode();

	Viewport->SetIgnoreInput(true);
	Viewport->SetMouseCaptureMode(EMouseCaptureMode::NoCapture);
	Viewport->SetMouseLockMode(EMouseLockMode::DoNotLock);
	Viewport->SetHideCursorDuringCapture(false);

	if (APlayerController* Controller = GetGameInstance()->GetFirstLocalPlayerController())
	{
		bSavedShowCursor = Controller->ShouldShowMouseCursor();
		Controller->SetShowMouseCursor(true);
	}

	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().ReleaseAllPointerCapture();
		FSlateApplication::Get().SetAllUserFocus(Menu, EFocusCause::SetDirectly);
	}
}

void UOscuPlayerMenuSubsystem::Close()
{
	if (!Menu.IsValid())
	{
		return;
	}

	Menu->CancelCapture();
	if (Root.IsValid())
	{
		Root->RemoveSlot(Menu.ToSharedRef());
	}
	Menu.Reset();

	if (UGameViewportClient* Viewport = AttachedTo.Get())
	{
		Viewport->SetIgnoreInput(bSavedIgnoreInput);
		Viewport->SetMouseCaptureMode(SavedCaptureMode);
		Viewport->SetMouseLockMode(SavedLockMode);
		Viewport->SetHideCursorDuringCapture(bSavedHideCursorDuringCapture);
	}

	if (const UGameInstance* GameInstance = GetGameInstance())
	{
		if (APlayerController* Controller = GameInstance->GetFirstLocalPlayerController())
		{
			Controller->SetShowMouseCursor(bSavedShowCursor);
		}
	}

	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetAllUserFocusToGameViewport();
	}
}

void UOscuPlayerMenuSubsystem::Toggle()
{
	if (IsOpen())
	{
		Close();
	}
	else
	{
		Open();
	}
}

void UOscuPlayerMenuSubsystem::HandleEscape()
{
	if (Menu.IsValid() && Menu->CancelCapture())
	{
		return;
	}
	Close();
}

#undef LOCTEXT_NAMESPACE
