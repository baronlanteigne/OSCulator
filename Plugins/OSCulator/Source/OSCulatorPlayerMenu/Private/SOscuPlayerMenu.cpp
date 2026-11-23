// Copyright Baron Lanteigne. All Rights Reserved.

#include "SOscuPlayerMenu.h"

#include "OscuEventOutputSubsystem.h"
#include "OscuMIDINoteName.h"
#include "OscuMIDISubsystem.h"
#include "OscuOSCSubsystem.h"
#include "OscuPlayerMappings.h"
#include "OscuSettings.h"

#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Engine/World.h"
#include "HAL/PlatformProcess.h"
#include "IPAddress.h"
#include "SocketSubsystem.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "OSCulatorPlayerMenu"

static const TCHAR* OscuGitHubURL = TEXT("https://github.com/baronlanteigne/OSCulator");

namespace OscuMenuStyle
{
	/** One theme, in sRGB as a designer would write it. */
	struct FPalette
	{
		FColor Background;
		FColor Text;
		FColor Muted;
		FColor Accent;
		FColor Good;
		FColor Bad;
	};

	/** The presets, in EOscuPlayerMenuTheme order (Custom is built from the settings). */
	const FPalette& Preset(const EOscuPlayerMenuTheme Theme)
	{
		static const FPalette Presets[] =
		{
			/* Midnight */ { FColor::FromHex(TEXT("0D1117")), FColor::FromHex(TEXT("E6EDF3")), FColor::FromHex(TEXT("7D8590")), FColor::FromHex(TEXT("58A6FF")), FColor::FromHex(TEXT("3FB950")), FColor::FromHex(TEXT("F85149")) },
			/* Graphite */ { FColor::FromHex(TEXT("1C1C1E")), FColor::FromHex(TEXT("F2F2F7")), FColor::FromHex(TEXT("8E8E93")), FColor::FromHex(TEXT("0A84FF")), FColor::FromHex(TEXT("30D158")), FColor::FromHex(TEXT("FF453A")) },
			/* Light */    { FColor::FromHex(TEXT("F7F7F5")), FColor::FromHex(TEXT("1B1B1F")), FColor::FromHex(TEXT("77777D")), FColor::FromHex(TEXT("2563EB")), FColor::FromHex(TEXT("16A34A")), FColor::FromHex(TEXT("DC2626")) },
			/* Ember */    { FColor::FromHex(TEXT("17110D")), FColor::FromHex(TEXT("F3E9DC")), FColor::FromHex(TEXT("9C8A7A")), FColor::FromHex(TEXT("FF8A3D")), FColor::FromHex(TEXT("8BC34A")), FColor::FromHex(TEXT("FF5A4E")) },
			/* Noir */     { FColor::FromHex(TEXT("000000")), FColor::FromHex(TEXT("FFFFFF")), FColor::FromHex(TEXT("8A8A8A")), FColor::FromHex(TEXT("D4FF3A")), FColor::FromHex(TEXT("D4FF3A")), FColor::FromHex(TEXT("FF5A4E")) },
		};
		const int32 Index = FMath::Clamp(static_cast<int32>(Theme), 0, static_cast<int32>(UE_ARRAY_COUNT(Presets)) - 1);
		return Presets[Index];
	}

	FPalette CustomPalette(const UOscuSettings& Settings)
	{
		FPalette Out = Preset(EOscuPlayerMenuTheme::Midnight);
		Out.Background = Settings.PlayerMenuBackground.ToFColorSRGB();
		Out.Text = Settings.PlayerMenuText.ToFColorSRGB();
		Out.Accent = Settings.PlayerMenuAccent.ToFColorSRGB();

		// Halfway between text and background, mixed as the eye sees it rather than in linear.
		const auto Mix = [](const uint8 A, const uint8 B) { return static_cast<uint8>(FMath::RoundToInt(A * 0.55f + B * 0.45f)); };
		Out.Muted = FColor(Mix(Out.Text.R, Out.Background.R), Mix(Out.Text.G, Out.Background.G), Mix(Out.Text.B, Out.Background.B));
		return Out;
	}

	/** The theme the menu shows: the player's pick if allowed and set, else the project's. */
	EOscuPlayerMenuTheme EffectiveTheme()
	{
		const UOscuSettings& Settings = *UOscuSettings::Get();
		const int32 Picked = FOscuPlayerMappings::Get().GetPlayerTheme();
		if (Settings.bPlayerMenuThemePicker && Picked >= 0 && Picked <= static_cast<int32>(EOscuPlayerMenuTheme::Custom))
		{
			const EOscuPlayerMenuTheme Theme = static_cast<EOscuPlayerMenuTheme>(Picked);
			// Custom exists only when the project defines it.
			if (Theme != EOscuPlayerMenuTheme::Custom || Settings.PlayerMenuTheme == EOscuPlayerMenuTheme::Custom)
			{
				return Theme;
			}
		}
		return Settings.PlayerMenuTheme;
	}

	FPalette PaletteFor(const EOscuPlayerMenuTheme Theme)
	{
		return Theme == EOscuPlayerMenuTheme::Custom ? CustomPalette(*UOscuSettings::Get()) : Preset(Theme);
	}

	/**
	 * Every colour and style the menu draws with. One instance, rewritten in place by
	 * ApplyTheme, so widgets that point at a style here see the new one without being
	 * told; the menu rebuilds itself anyway for the colours it read once.
	 */
	struct FTheme
	{
		FLinearColor Text, Muted, Accent, Good, Bad;
		FSlateBrush Dim, Panel, Hairline, GoodDot, BadDot;
		FButtonStyle TextButton;
		FEditableTextBoxStyle TextBox;
		FCheckBoxStyle Check;
	};

	FTheme& Current();

	FSlateFontInfo Font(const int32 Size, const FName Typeface = "Regular")
	{
		return FCoreStyle::GetDefaultFontStyle(Typeface, Size);
	}

	void ApplyTheme()
	{
		const FPalette P = PaletteFor(EffectiveTheme());
		FTheme& T = Current();

		T.Text = FLinearColor(P.Text);
		T.Muted = FLinearColor(P.Muted);
		T.Accent = FLinearColor(P.Accent);
		T.Good = FLinearColor(P.Good);
		T.Bad = FLinearColor(P.Bad);

		const FLinearColor Background(P.Background);
		const bool bLight = Background.GetLuminance() > 0.4f;
		const auto Ink = [&T](const float Alpha) { return FLinearColor(T.Text.R, T.Text.G, T.Text.B, Alpha); };

		T.Dim = FSlateColorBrush(FLinearColor(0.0f, 0.0f, 0.0f, bLight ? 0.3f : 0.5f));
		T.Panel = FSlateRoundedBoxBrush(FLinearColor(Background.R, Background.G, Background.B, 0.97f), 6.0f);
		T.Hairline = FSlateColorBrush(Ink(0.09f));
		T.GoodDot = FSlateRoundedBoxBrush(T.Good, 3.0f, FVector2f(6.0f, 6.0f));
		T.BadDot = FSlateRoundedBoxBrush(T.Bad, 3.0f, FVector2f(6.0f, 6.0f));

		FButtonStyle Button;
		Button.SetNormal(FSlateNoResource());
		Button.SetHovered(FSlateNoResource());
		Button.SetPressed(FSlateNoResource());
		Button.SetDisabled(FSlateNoResource());
		Button.SetNormalPadding(FMargin(4.0f, 2.0f));
		Button.SetPressedPadding(FMargin(4.0f, 2.0f));
		Button.SetNormalForeground(FSlateColor(T.Muted));
		Button.SetHoveredForeground(FSlateColor(T.Text));
		Button.SetPressedForeground(FSlateColor(T.Accent));
		Button.SetDisabledForeground(FSlateColor(T.Muted));
		T.TextButton = Button;

		FEditableTextBoxStyle Box = FCoreStyle::Get().GetWidgetStyle<FEditableTextBoxStyle>("NormalEditableTextBox");
		Box.SetBackgroundImageNormal(FSlateRoundedBoxBrush(Ink(0.05f), 3.0f));
		Box.SetBackgroundImageHovered(FSlateRoundedBoxBrush(Ink(0.08f), 3.0f));
		Box.SetBackgroundImageFocused(FSlateRoundedBoxBrush(Ink(0.08f), 3.0f, T.Accent, 1.0f));
		Box.SetBackgroundImageReadOnly(FSlateRoundedBoxBrush(Ink(0.03f), 3.0f));
		Box.SetForegroundColor(FSlateColor(T.Text));
		Box.SetFocusedForegroundColor(FSlateColor(T.Text));
		Box.SetPadding(FMargin(8.0f, 4.0f));
		Box.SetFont(Font(10));
		T.TextBox = Box;

		const FVector2f Size(10.0f, 10.0f);
		FCheckBoxStyle Check = FCoreStyle::Get().GetWidgetStyle<FCheckBoxStyle>("Checkbox");
		Check.SetUncheckedImage(FSlateRoundedBoxBrush(FLinearColor::Transparent, 2.0f, T.Muted, 1.0f, Size));
		Check.SetUncheckedHoveredImage(FSlateRoundedBoxBrush(FLinearColor::Transparent, 2.0f, T.Text, 1.0f, Size));
		Check.SetUncheckedPressedImage(FSlateRoundedBoxBrush(FLinearColor::Transparent, 2.0f, T.Accent, 1.0f, Size));
		Check.SetCheckedImage(FSlateRoundedBoxBrush(T.Accent, 2.0f, T.Accent, 1.0f, Size));
		Check.SetCheckedHoveredImage(FSlateRoundedBoxBrush(T.Accent, 2.0f, T.Text, 1.0f, Size));
		Check.SetCheckedPressedImage(FSlateRoundedBoxBrush(T.Accent, 2.0f, T.Accent, 1.0f, Size));
		Check.SetBackgroundImage(FSlateNoResource());
		Check.SetBackgroundHoveredImage(FSlateNoResource());
		Check.SetBackgroundPressedImage(FSlateNoResource());
		Check.SetPadding(FMargin(7.0f, 0.0f, 0.0f, 0.0f));
		Check.SetForegroundColor(FSlateColor(T.Text));
		Check.SetHoveredForegroundColor(FSlateColor(T.Text));
		Check.SetPressedForegroundColor(FSlateColor(T.Text));
		Check.SetCheckedForegroundColor(FSlateColor(T.Text));
		Check.SetCheckedHoveredForegroundColor(FSlateColor(T.Text));
		Check.SetCheckedPressedForegroundColor(FSlateColor(T.Text));
		T.Check = Check;
	}

	FTheme& Current()
	{
		static FTheme Theme;
		return Theme;
	}

	FLinearColor Text() { return Current().Text; }
	FLinearColor Muted() { return Current().Muted; }
	FLinearColor Accent() { return Current().Accent; }
	FLinearColor Good() { return Current().Good; }
	FLinearColor Bad() { return Current().Bad; }

	/** Small spaced capitals for section and column labels. */
	FSlateFontInfo LabelFont()
	{
		FSlateFontInfo Out = Font(8, "Bold");
		Out.LetterSpacing = 160;
		return Out;
	}

	const FSlateBrush* Dim() { return &Current().Dim; }
	const FSlateBrush* Panel() { return &Current().Panel; }
	const FSlateBrush* Hairline() { return &Current().Hairline; }
	const FSlateBrush* Dot(const bool bGood) { return bGood ? &Current().GoodDot : &Current().BadDot; }
	const FButtonStyle& TextButton() { return Current().TextButton; }
	const FEditableTextBoxStyle& TextBox() { return Current().TextBox; }
	const FCheckBoxStyle& Check() { return Current().Check; }

	/** A theme's swatch: its background, ringed in its accent; the chosen one ringed in the current text colour. */
	const FSlateBrush* Swatch(const EOscuPlayerMenuTheme Theme, const bool bSelected)
	{
		// Heap-held: an image keeps a pointer to its brush, and a growing map would move them.
		static TMap<uint32, TUniquePtr<FSlateBrush>> Cache;
		const FPalette P = PaletteFor(Theme);
		const uint32 Key = HashCombine(HashCombine(GetTypeHash(P.Background), GetTypeHash(P.Accent)), HashCombine(GetTypeHash(bSelected), GetTypeHash(Current().Text)));
		if (const TUniquePtr<FSlateBrush>* Found = Cache.Find(Key))
		{
			return Found->Get();
		}
		const FVector2f Size(14.0f, 14.0f);
		TUniquePtr<FSlateBrush>& Added = Cache.Add(Key, MakeUnique<FSlateBrush>(bSelected
			? FSlateRoundedBoxBrush(FLinearColor(P.Background), 7.0f, Current().Text, 2.0f, Size)
			: FSlateRoundedBoxBrush(FLinearColor(P.Background), 7.0f, FLinearColor(P.Accent), 1.5f, Size)));
		return Added.Get();
	}

	TSharedRef<STextBlock> Label(const FText& Title)
	{
		return SNew(STextBlock)
			.Text(Title)
			.Font(LabelFont())
			.ColorAndOpacity(Muted());
	}
}

namespace
{
	EOscuPlayerMIDIKind ToPlayerKind(const EOscuMIDIInputType Type)
	{
		switch (Type)
		{
		case EOscuMIDIInputType::ControlChange: return EOscuPlayerMIDIKind::ControlChange;
		case EOscuMIDIInputType::ProgramChange: return EOscuPlayerMIDIKind::ProgramChange;
		default:                                return EOscuPlayerMIDIKind::Note;
		}
	}

	FString NoteName(const int32 Number)
	{
		return OscuMIDINoteName::ToString(static_cast<uint8>(FMath::Clamp(Number, 0, 127)), UOscuSettings::Get()->MiddleCOctave);
	}

	FText DescribeSource(const FOscuMIDISource& Source, const EOscuMIDIInputType Type)
	{
		FString What;
		switch (Type)
		{
		case EOscuMIDIInputType::Note:
			What = NoteName(Source.GetNoteLow());
			break;
		case EOscuMIDIInputType::NoteRange:
			What = FString::Printf(TEXT("%s to %s"), *NoteName(Source.GetNoteLow()), *NoteName(Source.GetNoteHigh()));
			break;
		case EOscuMIDIInputType::ControlChange:
			What = FString::Printf(TEXT("CC %d"), Source.ControlNumber);
			break;
		default:
			What = FString::Printf(TEXT("Program %d"), Source.ProgramNumber);
			break;
		}
		return FText::FromString(FString::Printf(TEXT("Ch %d · %s"), Source.Channel, *What));
	}
}

void SOscuPlayerMenu::Construct(const FArguments& InArgs)
{
	World = InArgs._World;
	OnClose = InArgs._OnClose;

	SettingsChangedHandle = UOscuSettings::OnSettingsChanged.AddSP(this, &SOscuPlayerMenu::Rebuild);

	BuildAll();
}

void SOscuPlayerMenu::BuildAll()
{
	OscuMenuStyle::ApplyTheme();
	const UOscuSettings& Settings = *UOscuSettings::Get();

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(OscuMenuStyle::Dim())
		.Padding(16.0f)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SBox)
			.WidthOverride(860.0f)
			.MaxDesiredHeight(760.0f)
			[
				SNew(SBorder)
				.BorderImage(OscuMenuStyle::Panel())
				.Padding(FMargin(32.0f, 26.0f, 32.0f, 22.0f))
				[
					SNew(SVerticalBox)

					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Text(Settings.PlayerMenuTitle)
							.Font(OscuMenuStyle::Font(16, "Light"))
							.ColorAndOpacity(OscuMenuStyle::Text())
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							SNew(SButton)
							.ButtonStyle(&OscuMenuStyle::TextButton())
							.ToolTipText(LOCTEXT("CloseTip", "Close"))
							.OnClicked_Lambda([this]()
							{
								OnClose.ExecuteIfBound();
								return FReply::Handled();
							})
							[
								SNew(STextBlock).Text(FText::FromString(TEXT("×"))).Font(OscuMenuStyle::Font(16, "Light"))
							]
						]
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 4.0f, 0.0f, 0.0f)
					[
						SNew(STextBlock)
						.AutoWrapText(true)
						.Font(OscuMenuStyle::Font(9))
						.Visibility_Lambda([this]() { return Status.IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible; })
						.Text_Lambda([this]() { return Status; })
						.ColorAndOpacity_Lambda([this]() { return FSlateColor(bStatusError ? OscuMenuStyle::Bad() : OscuMenuStyle::Accent()); })
					]

					+ SVerticalBox::Slot()
					.FillHeight(1.0f)
					.Padding(0.0f, 22.0f, 0.0f, 0.0f)
					[
						SAssignNew(Body, SBox)
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 22.0f, 0.0f, 0.0f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							SNew(SButton)
							.ButtonStyle(&OscuMenuStyle::TextButton())
							.ContentPadding(FMargin(0.0f, 2.0f))
							.ToolTipText(LOCTEXT("ResetAllTip", "Every control back to the project's own OSC address and MIDI input. The port and devices stay as they are."))
							.OnClicked_Lambda([this]()
							{
								CancelCapture();
								FOscuPlayerMappings::Get().ResetAll();
								SetStatus(LOCTEXT("ResetAllDone", "Every control is back to its default."));
								return FReply::Handled();
							})
							[
								SNew(STextBlock).Text(LOCTEXT("ResetAll", "Reset all")).Font(OscuMenuStyle::Font(9))
							]
						]
						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						.HAlign(HAlign_Right)
						.VAlign(VAlign_Center)
						[
							BuildThemePicker()
						]
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.HAlign(HAlign_Center)
					.Padding(0.0f, 18.0f, 0.0f, 0.0f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Font(OscuMenuStyle::Font(8))
							.ColorAndOpacity(OscuMenuStyle::Muted())
							.Text(LOCTEXT("Credit", "Made using OSCulator plugin by Baron Lanteigne"))
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(6.0f, 0.0f, 0.0f, 0.0f)
						[
							SNew(STextBlock)
							.Font(OscuMenuStyle::Font(8))
							.ColorAndOpacity(OscuMenuStyle::Muted())
							.Text(FText::FromString(TEXT("\u00B7")))
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(2.0f, 0.0f, 0.0f, 0.0f)
						[
							SNew(SButton)
							.ButtonStyle(&OscuMenuStyle::TextButton())
							.ContentPadding(FMargin(4.0f, 0.0f))
							.Cursor(EMouseCursor::Hand)
							.ToolTipText(FText::FromString(OscuGitHubURL))
							.OnClicked_Lambda([]()
							{
								FPlatformProcess::LaunchURL(OscuGitHubURL, nullptr, nullptr);
								return FReply::Handled();
							})
							[
								SNew(STextBlock)
								.Font(OscuMenuStyle::Font(8))
								.Text(LOCTEXT("GitHub", "github.com/baronlanteigne/OSCulator"))
							]
						]
					]
				]
			]
		]
	];

	BuildBody();
}

TSharedRef<SWidget> SOscuPlayerMenu::BuildThemePicker()
{
	const UOscuSettings& Settings = *UOscuSettings::Get();
	if (!Settings.bPlayerMenuThemePicker)
	{
		return SNullWidget::NullWidget;
	}

	TArray<EOscuPlayerMenuTheme> Themes = {
		EOscuPlayerMenuTheme::Midnight, EOscuPlayerMenuTheme::Graphite, EOscuPlayerMenuTheme::Light,
		EOscuPlayerMenuTheme::Ember, EOscuPlayerMenuTheme::Noir };
	if (Settings.PlayerMenuTheme == EOscuPlayerMenuTheme::Custom)
	{
		Themes.Insert(EOscuPlayerMenuTheme::Custom, 0);
	}

	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
	for (const EOscuPlayerMenuTheme Theme : Themes)
	{
		const FText Name = StaticEnum<EOscuPlayerMenuTheme>()->GetDisplayNameTextByValue(static_cast<int64>(Theme));
		Row->AddSlot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(5.0f, 0.0f, 0.0f, 0.0f)
		[
			SNew(SButton)
			.ButtonStyle(&OscuMenuStyle::TextButton())
			.ContentPadding(FMargin(1.0f))
			.Cursor(EMouseCursor::Hand)
			.ToolTipText(Name)
			.OnClicked_Lambda([this, Theme]()
			{
				// Picking the project's own theme clears the choice, so a later change to it shows.
				const bool bProjectTheme = Theme == UOscuSettings::Get()->PlayerMenuTheme;
				FOscuPlayerMappings::Get().SetPlayerTheme(bProjectTheme ? INDEX_NONE : static_cast<int32>(Theme));
				Rebuild();
				return FReply::Handled();
			})
			[
				SNew(SImage)
				.Image(OscuMenuStyle::Swatch(Theme, OscuMenuStyle::EffectiveTheme() == Theme))
			]
		];
	}
	return Row;
}

SOscuPlayerMenu::~SOscuPlayerMenu()
{
	CancelCapture();
	UOscuSettings::OnSettingsChanged.Remove(SettingsChangedHandle);
}

void SOscuPlayerMenu::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	if (bRebuildPending)
	{
		bRebuildPending = false;
		BuildAll();
	}
}

void SOscuPlayerMenu::BuildBody()
{
	Body->SetContent(SNew(SScrollBox)
		+ SScrollBox::Slot()[BuildConnection()]
		+ SScrollBox::Slot()[BuildControls()]
		+ SScrollBox::Slot()[BuildOutputs()]);
}

void SOscuPlayerMenu::Rebuild()
{
	bRebuildPending = true;
}

void SOscuPlayerMenu::SetStatus(const FText& Text, const bool bError)
{
	Status = Text;
	bStatusError = bError;
}

const FOscuPlayerMenuEntry* SOscuPlayerMenu::GetEntry(const int32 EntryIndex) const
{
	const TArray<FOscuPlayerMenuEntry>& Entries = UOscuSettings::Get()->PlayerMenuEntries;
	return Entries.IsValidIndex(EntryIndex) ? &Entries[EntryIndex] : nullptr;
}

bool SOscuPlayerMenu::CancelCapture()
{
	if (Capturing == ECapture::None)
	{
		return false;
	}

	if (Capturing == ECapture::OSC)
	{
		if (UOscuOSCSubsystem* OSC = UOscuOSCSubsystem::Get(World.Get()))
		{
			OSC->CancelAddressCapture();
		}
	}
	else if (UOscuMIDISubsystem* MIDI = UOscuMIDISubsystem::Get())
	{
		MIDI->CancelCapture();
	}

	Capturing = ECapture::None;
	CapturingIndex = INDEX_NONE;
	SetStatus(FText::GetEmpty());
	return true;
}

FString SOscuPlayerMenu::GetLocalAddress() const
{
	ISocketSubsystem* Sockets = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (Sockets == nullptr)
	{
		return FString();
	}

	bool bCanBindAll = false;
	const TSharedRef<FInternetAddr> Address = Sockets->GetLocalHostAddr(*GLog, bCanBindAll);
	return Address->IsValid() ? Address->ToString(/*bAppendPort*/ false) : FString();
}

FText SOscuPlayerMenu::DescribeOSCPort() const
{
	const UOscuOSCSubsystem* OSC = UOscuOSCSubsystem::Get(World.Get());
	if (OSC != nullptr && OSC->IsListening())
	{
		const FString Address = GetLocalAddress();
		return Address.IsEmpty()
			? LOCTEXT("Listening", "Listening")
			: FText::Format(LOCTEXT("ListeningAt", "Listening on {0}"), FText::FromString(Address));
	}
	return LOCTEXT("NotListening", "Not listening. The port may be in use; try another.");
}

TSharedRef<SWidget> SOscuPlayerMenu::BuildConnection()
{
	const UOscuSettings& Settings = *UOscuSettings::Get();
	const bool bShowPort = Settings.bEnableOSCIn && Settings.bPlayerMenuOSCPort;
	const bool bShowDevices = Settings.bEnableMIDIIn && Settings.bPlayerMenuMIDIDevices;
	const bool bShowOSCOut = Settings.bEnableOSCOut && Settings.bPlayerMenuOSCOut;
	const bool bShowMIDIOut = Settings.bEnableMIDIOut && Settings.bPlayerMenuMIDIOutDevices;

	if (!bShowPort && !bShowDevices && !bShowOSCOut && !bShowMIDIOut)
	{
		return SNullWidget::NullWidget;
	}

	const auto RowLabel = [](const FText& Text)
	{
		return SNew(SBox)
			.WidthOverride(120.0f)
			[
				SNew(STextBlock).Text(Text).Font(OscuMenuStyle::Font(10)).ColorAndOpacity(OscuMenuStyle::Muted())
			];
	};

	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 12.0f)
		[
			OscuMenuStyle::Label(LOCTEXT("Connection", "CONNECTION"))
		];

	if (bShowPort)
	{
		Box->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 10.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				RowLabel(LOCTEXT("OSCPort", "OSC in"))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SBox)
				.WidthOverride(72.0f)
				[
					SNew(SEditableTextBox)
					.Style(&OscuMenuStyle::TextBox())
					.Text_Lambda([]() { return FText::AsNumber(UOscuSettings::Get()->ListenPort, &FNumberFormattingOptions::DefaultNoGrouping()); })
					.SelectAllTextWhenFocused(true)
					.OnTextCommitted_Lambda([this](const FText& Text, ETextCommit::Type)
					{
						const FString Trimmed = Text.ToString().TrimStartAndEnd();
						if (!Trimmed.IsNumeric())
						{
							SetStatus(LOCTEXT("PortNotNumber", "A port is a number from 1 to 65535."), true);
							return;
						}
						FString Error;
						const int32 Port = FCString::Atoi(*Trimmed);
						if (Port == UOscuSettings::Get()->ListenPort)
						{
							return;
						}
						if (FOscuPlayerMappings::Get().SetListenPort(Port, Error))
						{
							SetStatus(FText::Format(LOCTEXT("PortSet", "OSC port set to {0}."), FText::AsNumber(Port, &FNumberFormattingOptions::DefaultNoGrouping())));
						}
						else
						{
							SetStatus(FText::FromString(Error), true);
						}
					})
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(14.0f, 0.0f, 8.0f, 0.0f)
			[
				SNew(SImage)
				.Image_Lambda([this]()
				{
					const UOscuOSCSubsystem* OSC = UOscuOSCSubsystem::Get(World.Get());
					return OscuMenuStyle::Dot(OSC != nullptr && OSC->IsListening());
				})
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.Font(OscuMenuStyle::Font(9))
				.ColorAndOpacity(OscuMenuStyle::Muted())
				.ToolTipText(LOCTEXT("PortTip", "Point your OSC controller at this address and port."))
				.Text_Lambda([this]() { return DescribeOSCPort(); })
			]
		];
	}

	if (bShowDevices)
	{
		Box->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 2.0f, 0.0f, 0.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Top)
			[
				RowLabel(LOCTEXT("MIDIDevices", "MIDI in"))
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				BuildDevices(false)
			]
		];
	}

	if (bShowOSCOut)
	{
		const auto CurrentHost = []()
		{
			const TArray<FOscuOSCTarget>& Targets = UOscuSettings::Get()->OSCTargets;
			return Targets.Num() > 0 ? Targets[0].Host : FString(TEXT("127.0.0.1"));
		};
		const auto CurrentPort = []()
		{
			const TArray<FOscuOSCTarget>& Targets = UOscuSettings::Get()->OSCTargets;
			return Targets.Num() > 0 ? Targets[0].Port : 9000;
		};

		Box->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 10.0f, 0.0f, 0.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				RowLabel(LOCTEXT("OSCOut", "OSC out"))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SBox)
				.WidthOverride(150.0f)
				[
					SNew(SEditableTextBox)
					.Style(&OscuMenuStyle::TextBox())
					.ToolTipText(LOCTEXT("OSCOutHostTip", "The machine to send OSC to. 127.0.0.1 is this one."))
					.Text_Lambda([CurrentHost]() { return FText::FromString(CurrentHost()); })
					.SelectAllTextWhenFocused(true)
					.OnTextCommitted_Lambda([this, CurrentHost, CurrentPort](const FText& Text, ETextCommit::Type)
					{
						if (Text.ToString().TrimStartAndEnd() == CurrentHost())
						{
							return;
						}
						FString Error;
						if (!FOscuPlayerMappings::Get().SetOSCOutTarget(Text.ToString(), CurrentPort(), Error))
						{
							SetStatus(FText::FromString(Error), true);
						}
					})
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(6.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SBox)
				.WidthOverride(72.0f)
				[
					SNew(SEditableTextBox)
					.Style(&OscuMenuStyle::TextBox())
					.Text_Lambda([CurrentPort]() { return FText::AsNumber(CurrentPort(), &FNumberFormattingOptions::DefaultNoGrouping()); })
					.SelectAllTextWhenFocused(true)
					.OnTextCommitted_Lambda([this, CurrentHost, CurrentPort](const FText& Text, ETextCommit::Type)
					{
						const FString Trimmed = Text.ToString().TrimStartAndEnd();
						if (!Trimmed.IsNumeric())
						{
							SetStatus(LOCTEXT("PortNotNumber", "A port is a number from 1 to 65535."), true);
							return;
						}
						const int32 Port = FCString::Atoi(*Trimmed);
						if (Port == CurrentPort())
						{
							return;
						}
						FString Error;
						if (!FOscuPlayerMappings::Get().SetOSCOutTarget(CurrentHost(), Port, Error))
						{
							SetStatus(FText::FromString(Error), true);
						}
					})
				]
			]
		];
	}

	if (bShowMIDIOut)
	{
		Box->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 12.0f, 0.0f, 0.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Top)
			[
				RowLabel(LOCTEXT("MIDIOut", "MIDI out"))
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				BuildDevices(true)
			]
		];
	}

	Box->AddSlot()
	.AutoHeight()
	[
		SNew(SSpacer).Size(FVector2D(1.0f, 30.0f))
	];

	return Box;
}

TSharedRef<SWidget> SOscuPlayerMenu::BuildDevices(const bool bOutputs)
{
	TArray<FString> Names;
	if (bOutputs)
	{
		UOscuMIDISubsystem::GetAvailableOutputDevices(Names);
	}
	else
	{
		UOscuMIDISubsystem::GetAvailableInputDevices(Names);
	}

	const auto Listed = [bOutputs]()
	{
		TArray<FString> Out;
		const UOscuSettings& Settings = *UOscuSettings::Get();
		if (bOutputs)
		{
			Out = Settings.MIDIOutputDeviceNames;
		}
		else
		{
			for (const FOscuMIDIInputDevice& Device : Settings.MIDIInputDevices)
			{
				Out.Add(Device.Name);
			}
		}
		return Out;
	};
	const auto IsListed = [Listed](const FString& Name)
	{
		return Listed().ContainsByPredicate([&Name](const FString& Other) { return Other.Equals(Name, ESearchCase::IgnoreCase); });
	};

	// Configured but not on this machine: shown so the player can untick them.
	TSet<FString> Present;
	for (const FString& Name : Names)
	{
		Present.Add(Name.ToLower());
	}
	TArray<FString> Missing;
	for (const FString& Name : Listed())
	{
		if (!Name.IsEmpty() && !Present.Contains(Name.ToLower()))
		{
			Missing.Add(Name);
		}
	}

	if (Names.Num() == 0 && Missing.Num() == 0)
	{
		return SNew(STextBlock)
			.AutoWrapText(true)
			.Font(OscuMenuStyle::Font(10))
			.ColorAndOpacity(OscuMenuStyle::Muted())
			.Text(bOutputs
				? LOCTEXT("NoOutDevices", "No MIDI output found. Connect one, then restart the game.")
				: LOCTEXT("NoDevices", "No MIDI input found. Connect one, then restart the game."));
	}

	TSharedRef<SWrapBox> Wrap = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(22.0f, 8.0f));

	const auto AddDevice = [this, &Wrap, bOutputs, IsListed](const FString& Name, const bool bPresent)
	{
		Wrap->AddSlot()
		[
			SNew(SCheckBox)
			.Style(&OscuMenuStyle::Check())
			.IsChecked_Lambda([Name, IsListed]() { return IsListed(Name) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([this, Name, bOutputs](const ECheckBoxState State)
			{
				CancelCapture();
				FString Error;
				const bool bOn = State == ECheckBoxState::Checked;
				const bool bOk = bOutputs
					? FOscuPlayerMappings::Get().SetMIDIOutputDeviceEnabled(Name, bOn, Error)
					: FOscuPlayerMappings::Get().SetMIDIInputDeviceEnabled(Name, bOn, Error);
				if (!bOk)
				{
					SetStatus(FText::FromString(Error), true);
				}
			})
			[
				SNew(STextBlock)
				.Font(OscuMenuStyle::Font(10))
				.Text(bPresent ? FText::FromString(Name) : FText::Format(LOCTEXT("DeviceMissing", "{0}  (not connected)"), FText::FromString(Name)))
				.ColorAndOpacity_Lambda([Name, bPresent, IsListed]()
				{
					return FSlateColor(bPresent && IsListed(Name) ? OscuMenuStyle::Text() : OscuMenuStyle::Muted());
				})
			]
		];
	};

	for (const FString& Name : Names)
	{
		AddDevice(Name, true);
	}
	for (const FString& Name : Missing)
	{
		AddDevice(Name, false);
	}
	return Wrap;
}

TSharedRef<SWidget> SOscuPlayerMenu::BuildControls()
{
	const UOscuSettings& Settings = *UOscuSettings::Get();

	bool bAnyOSC = false;
	bool bAnyMIDI = false;
	for (const FOscuPlayerMenuEntry& Entry : Settings.PlayerMenuEntries)
	{
		bAnyOSC |= Entry.bOSC;
		bAnyMIDI |= Entry.bMIDI;
	}
	const bool bShowOSC = Settings.bEnableOSCIn && bAnyOSC;
	const bool bShowMIDI = Settings.bEnableMIDIIn && bAnyMIDI;

	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);

	if (Settings.PlayerMenuEntries.Num() == 0 || (!bShowOSC && !bShowMIDI))
	{
		Box->AddSlot()
		.AutoHeight()
		[
			SNew(STextBlock)
			.Font(OscuMenuStyle::Font(10))
			.ColorAndOpacity(OscuMenuStyle::Muted())
			.Text(LOCTEXT("NoControls", "This project has no controls to remap."))
		];
		return Box;
	}

	// The section label doubles as the first column's title.
	TSharedRef<SHorizontalBox> Header = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(0.75f)[ OscuMenuStyle::Label(LOCTEXT("Controls", "INPUTS")) ];
	if (bShowOSC)
	{
		Header->AddSlot().FillWidth(1.8f).Padding(12.0f, 0.0f, 0.0f, 0.0f)[ OscuMenuStyle::Label(LOCTEXT("OSCAddress", "OSC")) ];
	}
	if (bShowMIDI)
	{
		Header->AddSlot().FillWidth(1.2f).Padding(20.0f, 0.0f, 0.0f, 0.0f)[ OscuMenuStyle::Label(LOCTEXT("MIDIInput", "MIDI")) ];
	}

	Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)[ Header ];

	int32 Shown = 0;
	for (int32 Index = 0; Index < Settings.PlayerMenuEntries.Num(); ++Index)
	{
		const FOscuPlayerMenuEntry& Entry = Settings.PlayerMenuEntries[Index];
		if (Entry.Tag.IsNone() || Entry.FunctionName.IsNone())
		{
			continue;
		}
		Box->AddSlot().AutoHeight()[ BuildRow(Index, bShowOSC, bShowMIDI, Shown++ > 0) ];
	}
	return Box;
}

FText SOscuPlayerMenu::DescribeMIDI(const int32 EntryIndex, bool& bOutCustom) const
{
	bOutCustom = false;
	const FOscuPlayerMenuEntry* Entry = GetEntry(EntryIndex);
	if (Entry == nullptr)
	{
		return FText::GetEmpty();
	}

	const UOscuMIDISubsystem* MIDI = UOscuMIDISubsystem::Get();
	const FOscuMIDIBinding* Project = MIDI != nullptr ? MIDI->FindProjectBinding(Entry->Tag, Entry->FunctionName) : nullptr;

	if (const FOscuPlayerOverride* Override = FOscuPlayerMappings::Get().Find(Entry->Tag, Entry->FunctionName); Override != nullptr && Override->HasMIDI())
	{
		bOutCustom = true;

		// Described the way dispatch will apply it: a note onto a range keeps the range.
		FOscuMIDISource Source;
		Source.Channel = static_cast<uint8>(Override->MIDIChannel);
		EOscuMIDIInputType Type = EOscuMIDIInputType::Note;
		switch (Override->MIDIKind)
		{
		case EOscuPlayerMIDIKind::ControlChange:
			Type = EOscuMIDIInputType::ControlChange;
			Source.ControlNumber = static_cast<uint8>(Override->MIDINumber);
			break;
		case EOscuPlayerMIDIKind::ProgramChange:
			Type = EOscuMIDIInputType::ProgramChange;
			Source.ProgramNumber = static_cast<uint8>(Override->MIDINumber);
			break;
		default:
			Source.ResolvedNote = static_cast<uint8>(Override->MIDINumber);
			if (Project != nullptr && Project->Type == EOscuMIDIInputType::NoteRange)
			{
				const int32 Span = Project->Sources.Num() > 0 ? Project->Sources[0].GetNoteHigh() - Project->Sources[0].GetNoteLow() : 12;
				Type = EOscuMIDIInputType::NoteRange;
				Source.Type = Type;
				Source.ResolvedNoteHigh = static_cast<uint8>(FMath::Min(127, Override->MIDINumber + Span));
			}
			break;
		}
		Source.Type = Type;
		return DescribeSource(Source, Type);
	}

	if (Project == nullptr || Project->Sources.Num() == 0)
	{
		return LOCTEXT("NotAssigned", "—");
	}

	FText Text = DescribeSource(Project->Sources[0], Project->Type);
	if (Project->Sources.Num() > 1)
	{
		Text = FText::Format(LOCTEXT("MoreSources", "{0} (+{1})"), Text, FText::AsNumber(Project->Sources.Num() - 1));
	}
	return Text;
}

TSharedRef<SWidget> SOscuPlayerMenu::BuildRow(const int32 EntryIndex, const bool bShowOSC, const bool bShowMIDI, const bool bDivider)
{
	const FOscuPlayerMenuEntry& Entry = *GetEntry(EntryIndex);
	const FName Tag = Entry.Tag;
	const FName Function = Entry.FunctionName;
	const FString Default = FOscuPlayerMappings::DefaultOSCAddress(Tag, Function);

	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.FillWidth(0.75f)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(Entry.GetDisplayLabel())
			.ToolTipText(FText::FromString(Default))
			.Font(OscuMenuStyle::Font(10))
			.ColorAndOpacity(OscuMenuStyle::Text())
		];

	if (bShowOSC)
	{
		TSharedRef<SWidget> Cell = SNullWidget::NullWidget;
		if (Entry.bOSC)
		{
			Cell = SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(SEditableTextBox)
					.Style(&OscuMenuStyle::TextBox())
					.HintText(FText::FromString(Default))
					.ToolTipText(LOCTEXT("OSCTip", "The address your controller sends. Leave empty for the default shown in grey."))
					.Text_Lambda([Tag, Function]()
					{
						const FOscuPlayerOverride* Override = FOscuPlayerMappings::Get().Find(Tag, Function);
						return Override != nullptr ? FText::FromString(Override->OSCAddress) : FText::GetEmpty();
					})
					.OnTextCommitted_Lambda([this, Tag, Function](const FText& Text, ETextCommit::Type)
					{
						const FOscuPlayerOverride* Override = FOscuPlayerMappings::Get().Find(Tag, Function);
						const FString Current = Override != nullptr ? Override->OSCAddress : FString();
						if (Text.ToString().TrimStartAndEnd() != Current)
						{
							FOscuPlayerMappings::Get().SetOSCAddress(Tag, Function, Text.ToString());
						}
					})
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.MinWidth(62.0f)
				.Padding(10.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.ButtonStyle(&OscuMenuStyle::TextButton())
					.ToolTipText(LOCTEXT("ListenTip", "Click, then move the control on your OSC controller: its address is used."))
					.OnClicked_Lambda([this, EntryIndex]()
					{
						const bool bWasThis = Capturing == ECapture::OSC && CapturingIndex == EntryIndex;
						CancelCapture();
						if (!bWasThis)
						{
							StartOSCCapture(EntryIndex);
						}
						return FReply::Handled();
					})
					[
						SNew(STextBlock)
						.Font(OscuMenuStyle::Font(9))
						.ColorAndOpacity_Lambda([this, EntryIndex]()
						{
							return Capturing == ECapture::OSC && CapturingIndex == EntryIndex ? FSlateColor(OscuMenuStyle::Accent()) : FSlateColor::UseForeground();
						})
						.Text_Lambda([this, EntryIndex]()
						{
							return Capturing == ECapture::OSC && CapturingIndex == EntryIndex
								? LOCTEXT("Listening...", "listening…")
								: LOCTEXT("Listen", "listen");
						})
					]
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(2.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.ButtonStyle(&OscuMenuStyle::TextButton())
					.ToolTipText(LOCTEXT("ResetOSCTip", "Back to the default address."))
					.Visibility_Lambda([Tag, Function]()
					{
						const FOscuPlayerOverride* Override = FOscuPlayerMappings::Get().Find(Tag, Function);
						return Override != nullptr && Override->HasOSC() ? EVisibility::Visible : EVisibility::Hidden;
					})
					.OnClicked_Lambda([this, Tag, Function]()
					{
						CancelCapture();
						FOscuPlayerMappings::Get().SetOSCAddress(Tag, Function, FString());
						return FReply::Handled();
					})
					[
						SNew(STextBlock).Font(OscuMenuStyle::Font(12, "Light")).Text(FText::FromString(TEXT("\u00D7")))
					]
				];
		}

		Row->AddSlot().FillWidth(1.8f).VAlign(VAlign_Center).Padding(12.0f, 0.0f, 0.0f, 0.0f)[ Cell ];
	}

	if (bShowMIDI)
	{
		TSharedRef<SWidget> Cell = SNullWidget::NullWidget;
		if (Entry.bMIDI)
		{
			Cell = SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Font(OscuMenuStyle::Font(10))
					.Text_Lambda([this, EntryIndex]()
					{
						bool bCustom = false;
						return DescribeMIDI(EntryIndex, bCustom);
					})
					.ColorAndOpacity_Lambda([this, EntryIndex]()
					{
						bool bCustom = false;
						DescribeMIDI(EntryIndex, bCustom);
						return FSlateColor(bCustom ? OscuMenuStyle::Accent() : OscuMenuStyle::Text());
					})
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.MinWidth(62.0f)
				.Padding(10.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.ButtonStyle(&OscuMenuStyle::TextButton())
					.ToolTipText(LOCTEXT("LearnTip", "Click, then play a note, press a pad or move a knob on your MIDI controller."))
					.OnClicked_Lambda([this, EntryIndex]()
					{
						const bool bWasThis = Capturing == ECapture::MIDI && CapturingIndex == EntryIndex;
						CancelCapture();
						if (!bWasThis)
						{
							StartMIDICapture(EntryIndex);
						}
						return FReply::Handled();
					})
					[
						SNew(STextBlock)
						.Font(OscuMenuStyle::Font(9))
						.ColorAndOpacity_Lambda([this, EntryIndex]()
						{
							return Capturing == ECapture::MIDI && CapturingIndex == EntryIndex ? FSlateColor(OscuMenuStyle::Accent()) : FSlateColor::UseForeground();
						})
						.Text_Lambda([this, EntryIndex]()
						{
							return Capturing == ECapture::MIDI && CapturingIndex == EntryIndex
								? LOCTEXT("Waiting...", "waiting…")
								: LOCTEXT("Learn", "learn");
						})
					]
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(2.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.ButtonStyle(&OscuMenuStyle::TextButton())
					.ToolTipText(LOCTEXT("ResetMIDITip", "Back to the project's MIDI input."))
					.Visibility_Lambda([Tag, Function]()
					{
						const FOscuPlayerOverride* Override = FOscuPlayerMappings::Get().Find(Tag, Function);
						return Override != nullptr && Override->HasMIDI() ? EVisibility::Visible : EVisibility::Hidden;
					})
					.OnClicked_Lambda([this, Tag, Function]()
					{
						CancelCapture();
						FOscuPlayerMappings::Get().SetMIDI(Tag, Function, EOscuPlayerMIDIKind::None, 1, 0);
						return FReply::Handled();
					})
					[
						SNew(STextBlock).Font(OscuMenuStyle::Font(12, "Light")).Text(FText::FromString(TEXT("\u00D7")))
					]
				];
		}

		Row->AddSlot().FillWidth(1.2f).VAlign(VAlign_Center).Padding(20.0f, 0.0f, 0.0f, 0.0f)[ Cell ];
	}

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SBox)
			.HeightOverride(1.0f)
			.Visibility(bDivider ? EVisibility::HitTestInvisible : EVisibility::Collapsed)
			[
				SNew(SImage).Image(OscuMenuStyle::Hairline())
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 7.0f)
		[
			Row
		];
}

void SOscuPlayerMenu::StartOSCCapture(const int32 EntryIndex)
{
	UOscuOSCSubsystem* OSC = UOscuOSCSubsystem::Get(World.Get());
	const FOscuPlayerMenuEntry* Entry = GetEntry(EntryIndex);
	if (OSC == nullptr || Entry == nullptr)
	{
		return;
	}
	if (!OSC->IsListening())
	{
		SetStatus(LOCTEXT("ListenNoSocket", "OSC is not listening, so nothing can arrive. Check the port above."), true);
		return;
	}

	Capturing = ECapture::OSC;
	CapturingIndex = EntryIndex;
	SetStatus(FText::Format(LOCTEXT("ListenArmed", "Move the control for \"{0}\" on your OSC controller..."), Entry->GetDisplayLabel()));

	const FName Tag = Entry->Tag;
	const FName Function = Entry->FunctionName;
	const FText Label = Entry->GetDisplayLabel();
	TWeakPtr<SOscuPlayerMenu> Weak = StaticCastSharedRef<SOscuPlayerMenu>(AsShared());

	OSC->ArmAddressCapture([Weak, Tag, Function, Label](const FString& Address)
	{
		FOscuPlayerMappings::Get().SetOSCAddress(Tag, Function, Address);
		if (const TSharedPtr<SOscuPlayerMenu> Menu = Weak.Pin())
		{
			Menu->Capturing = ECapture::None;
			Menu->CapturingIndex = INDEX_NONE;
			Menu->SetStatus(FText::Format(LOCTEXT("ListenDone", "\"{0}\" now answers to {1}."), Label, FText::FromString(Address)));
		}
	});
}

void SOscuPlayerMenu::StartMIDICapture(const int32 EntryIndex)
{
	UOscuMIDISubsystem* MIDI = UOscuMIDISubsystem::Get();
	const FOscuPlayerMenuEntry* Entry = GetEntry(EntryIndex);
	if (MIDI == nullptr || Entry == nullptr)
	{
		return;
	}
	if (MIDI->GetOpenDeviceCount() == 0)
	{
		SetStatus(LOCTEXT("LearnNoDevice", "No MIDI device is open. Tick yours under MIDI devices first."), true);
		return;
	}

	Capturing = ECapture::MIDI;
	CapturingIndex = EntryIndex;
	SetStatus(FText::Format(LOCTEXT("LearnArmed", "Play a note, press a pad or move a knob for \"{0}\"..."), Entry->GetDisplayLabel()));

	const FName Tag = Entry->Tag;
	const FName Function = Entry->FunctionName;
	const FText Label = Entry->GetDisplayLabel();
	TWeakPtr<SOscuPlayerMenu> Weak = StaticCastSharedRef<SOscuPlayerMenu>(AsShared());

	MIDI->ArmCapture([Weak, Tag, Function, Label, EntryIndex](const FOscuMIDICapture& Capture)
	{
		FOscuPlayerMappings::Get().SetMIDI(Tag, Function, ToPlayerKind(Capture.Type), Capture.Channel, Capture.Number);
		if (const TSharedPtr<SOscuPlayerMenu> Menu = Weak.Pin())
		{
			Menu->Capturing = ECapture::None;
			Menu->CapturingIndex = INDEX_NONE;
			bool bCustom = false;
			Menu->SetStatus(FText::Format(LOCTEXT("LearnDone", "\"{0}\" now answers to {1}."),
				Label, Menu->DescribeMIDI(EntryIndex, bCustom)));
		}
	});
}

const FOscuEventOutput* SOscuPlayerMenu::GetOutput(const int32 OutputIndex) const
{
	const TArray<FOscuEventOutput>& Outputs = UOscuSettings::Get()->EventOutputs;
	return Outputs.IsValidIndex(OutputIndex) ? &Outputs[OutputIndex] : nullptr;
}

TSharedRef<SWidget> SOscuPlayerMenu::BuildOutputs()
{
	const UOscuSettings& Settings = *UOscuSettings::Get();

	TArray<int32> Shown;
	bool bAnyOSC = false;
	bool bAnyMIDI = false;
	for (int32 Index = 0; Index < Settings.EventOutputs.Num(); ++Index)
	{
		const FOscuEventOutput& Output = Settings.EventOutputs[Index];
		if (Output.bInPlayerMenu && !Output.Tag.IsNone() && !Output.Dispatcher.IsNone())
		{
			Shown.Add(Index);
			bAnyOSC |= Output.bSendOSC || Output.bPlayerOSC;
			bAnyMIDI |= Output.MIDIKind != EOscuEventMIDIKind::None || Output.bPlayerMIDI;
		}
	}
	const bool bShowOSC = Settings.bEnableOSCOut && bAnyOSC;
	const bool bShowMIDI = Settings.bEnableMIDIOut && bAnyMIDI;

	if (Shown.Num() == 0 || (!bShowOSC && !bShowMIDI))
	{
		return SNullWidget::NullWidget;
	}

	TSharedRef<SHorizontalBox> Header = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(0.75f)[ OscuMenuStyle::Label(LOCTEXT("Outputs", "OUTPUTS")) ];
	if (bShowOSC)
	{
		Header->AddSlot().FillWidth(1.8f).Padding(12.0f, 0.0f, 0.0f, 0.0f)[ OscuMenuStyle::Label(LOCTEXT("OutOSC", "OSC")) ];
	}
	if (bShowMIDI)
	{
		Header->AddSlot().FillWidth(1.2f).Padding(20.0f, 0.0f, 0.0f, 0.0f)[ OscuMenuStyle::Label(LOCTEXT("OutMIDI", "MIDI")) ];
	}

	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[ SNew(SSpacer).Size(FVector2D(1.0f, 30.0f)) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)[ Header ];

	for (int32 Row = 0; Row < Shown.Num(); ++Row)
	{
		Box->AddSlot().AutoHeight()[ BuildOutputRow(Shown[Row], bShowOSC, bShowMIDI, Row > 0) ];
	}
	return Box;
}

TSharedRef<SWidget> SOscuPlayerMenu::BuildOutputRow(const int32 OutputIndex, const bool bShowOSC, const bool bShowMIDI, const bool bDivider)
{
	const FOscuEventOutput& Output = *GetOutput(OutputIndex);
	const FName Tag = Output.Tag;
	const FName Dispatcher = Output.Dispatcher;
	const FString DefaultAddress = Output.GetDefaultOSCAddress();

	const auto Events = [this]() { return UOscuEventOutputSubsystem::Get(World.Get()); };

	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.FillWidth(0.75f)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(Output.GetDisplayLabel())
			.ToolTipText(FText::FromString(FString::Printf(TEXT("%s on %s"), *Dispatcher.ToString(), *Tag.ToString())))
			.Font(OscuMenuStyle::Font(10))
			.ColorAndOpacity(OscuMenuStyle::Text())
		];

	const auto ResetButton = [this](TFunction<bool()> IsCustom, TFunction<void()> Reset, const FText& Tip)
	{
		return SNew(SButton)
			.ButtonStyle(&OscuMenuStyle::TextButton())
			.ToolTipText(Tip)
			.Visibility_Lambda([IsCustom]() { return IsCustom() ? EVisibility::Visible : EVisibility::Hidden; })
			.OnClicked_Lambda([this, Reset]()
			{
				CancelCapture();
				Reset();
				return FReply::Handled();
			})
			[
				SNew(STextBlock).Font(OscuMenuStyle::Font(12, "Light")).Text(FText::FromString(TEXT("×")))
			];
	};

	if (bShowOSC)
	{
		TSharedRef<SWidget> Field = SNullWidget::NullWidget;
		if (Output.bPlayerOSC)
		{
			Field = SNew(SEditableTextBox)
				.Style(&OscuMenuStyle::TextBox())
				.HintText(Output.bSendOSC ? FText::FromString(DefaultAddress) : LOCTEXT("OSCOutOffHint", "not sent; type an address to send"))
				.ToolTipText(LOCTEXT("OSCOutTip", "The address this is sent to. Leave empty for the default shown in grey."))
				.Text_Lambda([Tag, Dispatcher]()
				{
					const FOscuPlayerOverride* Override = FOscuPlayerMappings::Get().Find(Tag, Dispatcher, true);
					return Override != nullptr ? FText::FromString(Override->OSCAddress) : FText::GetEmpty();
				})
				.OnTextCommitted_Lambda([Tag, Dispatcher](const FText& Text, ETextCommit::Type)
				{
					const FOscuPlayerOverride* Override = FOscuPlayerMappings::Get().Find(Tag, Dispatcher, true);
					const FString Current = Override != nullptr ? Override->OSCAddress : FString();
					if (Text.ToString().TrimStartAndEnd() != Current)
					{
						FOscuPlayerMappings::Get().SetOSCAddress(Tag, Dispatcher, Text.ToString(), true);
					}
				});
		}
		else
		{
			Field = SNew(STextBlock)
				.Font(OscuMenuStyle::Font(10))
				.ColorAndOpacity(OscuMenuStyle::Muted())
				.Text(Output.bSendOSC ? FText::FromString(DefaultAddress) : FText::FromString(TEXT("—")));
		}

		TSharedRef<SWidget> Cell = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)[ Field ]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.MinWidth(62.0f)
				.Padding(10.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.ButtonStyle(&OscuMenuStyle::TextButton())
				.ToolTipText(LOCTEXT("TestTip", "Send it once now, as if the event happened at its highest value."))
				.OnClicked_Lambda([this, OutputIndex, Events]()
				{
					CancelCapture();
					if (UOscuEventOutputSubsystem* Subsystem = Events())
					{
						const FOscuEventSend Sent = Subsystem->SendTest(OutputIndex);
						SetStatus(FText::Format(LOCTEXT("TestSent", "Sent {0}"), FText::FromString(Sent.Describe())));
					}
					return FReply::Handled();
				})
				[
					SNew(STextBlock).Font(OscuMenuStyle::Font(9)).Text(LOCTEXT("Test", "test"))
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(2.0f, 0.0f, 0.0f, 0.0f)
			[
				ResetButton(
					[Tag, Dispatcher]() { const FOscuPlayerOverride* O = FOscuPlayerMappings::Get().Find(Tag, Dispatcher, true); return O != nullptr && O->HasOSC(); },
					[Tag, Dispatcher]() { FOscuPlayerMappings::Get().SetOSCAddress(Tag, Dispatcher, FString(), true); },
					LOCTEXT("ResetOSCOutTip", "Back to the default address."))
			];

		Row->AddSlot().FillWidth(1.8f).VAlign(VAlign_Center).Padding(12.0f, 0.0f, 0.0f, 0.0f)[ Cell ];
	}

	if (bShowMIDI)
	{
		// The project's message, for the hint and as the base a partial edit ("cc 22") builds on.
		EOscuPlayerMIDIKind DefaultKind = EOscuPlayerMIDIKind::None;
		switch (Output.MIDIKind)
		{
		case EOscuEventMIDIKind::Note:          DefaultKind = EOscuPlayerMIDIKind::Note; break;
		case EOscuEventMIDIKind::ControlChange: DefaultKind = EOscuPlayerMIDIKind::ControlChange; break;
		default: break;
		}
		const FString DefaultText = UOscuEventOutputSubsystem::DescribeMIDIOut(DefaultKind, Output.MIDIChannel, Output.MIDINumber);

		TSharedRef<SWidget> Field = SNullWidget::NullWidget;
		if (Output.bPlayerMIDI)
		{
			Field = SNew(SEditableTextBox)
				.Style(&OscuMenuStyle::TextBox())
				.HintText(DefaultText.IsEmpty() ? LOCTEXT("MIDIOutHint", "cc 21, note C3, off") : FText::FromString(DefaultText))
				.ToolTipText(LOCTEXT("MIDIOutTip", "What to send: \"cc 21\", \"note C3\", \"ch 2 cc 21\" or \"off\". Leave empty for the default."))
				.Text_Lambda([Tag, Dispatcher]()
				{
					const FOscuPlayerOverride* Override = FOscuPlayerMappings::Get().Find(Tag, Dispatcher, true);
					return Override != nullptr && Override->HasMIDI()
						? FText::FromString(UOscuEventOutputSubsystem::DescribeMIDIOut(Override->MIDIKind, Override->MIDIChannel, Override->MIDINumber))
						: FText::GetEmpty();
				})
				.OnTextCommitted_Lambda([this, Tag, Dispatcher, DefaultKind, OutputIndex](const FText& Text, ETextCommit::Type)
				{
					const FString Typed = Text.ToString().TrimStartAndEnd();
					const FOscuPlayerOverride* Override = FOscuPlayerMappings::Get().Find(Tag, Dispatcher, true);
					const FString Current = Override != nullptr && Override->HasMIDI()
						? UOscuEventOutputSubsystem::DescribeMIDIOut(Override->MIDIKind, Override->MIDIChannel, Override->MIDINumber)
						: FString();
					if (Typed == Current)
					{
						return;
					}
					if (Typed.IsEmpty())
					{
						FOscuPlayerMappings::Get().SetMIDI(Tag, Dispatcher, EOscuPlayerMIDIKind::None, 1, 0, true);
						return;
					}

					const FOscuEventOutput* Def = GetOutput(OutputIndex);
					EOscuPlayerMIDIKind Kind = Override != nullptr && Override->HasMIDI() ? Override->MIDIKind : DefaultKind;
					int32 Channel = Override != nullptr && Override->HasMIDI() ? Override->MIDIChannel : (Def != nullptr ? Def->MIDIChannel : 1);
					int32 Number = Override != nullptr && Override->HasMIDI() ? Override->MIDINumber : (Def != nullptr ? Def->MIDINumber : 0);
					FString Error;
					if (UOscuEventOutputSubsystem::ParseMIDIOut(Typed, Kind, Channel, Number, Error))
					{
						FOscuPlayerMappings::Get().SetMIDI(Tag, Dispatcher, Kind, Channel, Number, true);
					}
					else
					{
						SetStatus(FText::FromString(Error), true);
					}
				});
		}
		else
		{
			Field = SNew(STextBlock)
				.Font(OscuMenuStyle::Font(10))
				.ColorAndOpacity(OscuMenuStyle::Muted())
				.Text(FText::FromString(DefaultText.IsEmpty() ? FString(TEXT("—")) : DefaultText));
		}

		TSharedRef<SWidget> Cell = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)[ Field ]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.MinWidth(62.0f)
				.Padding(10.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.ButtonStyle(&OscuMenuStyle::TextButton())
				.Visibility(Output.bPlayerMIDI ? EVisibility::Visible : EVisibility::Hidden)
				.ToolTipText(LOCTEXT("OutLearnTip", "Press the pad or move the knob that should receive this: for controllers that light up or move to what they are sent."))
				.OnClicked_Lambda([this, OutputIndex]()
				{
					const bool bWasThis = Capturing == ECapture::MIDIOut && CapturingIndex == OutputIndex;
					CancelCapture();
					if (!bWasThis)
					{
						StartMIDIOutCapture(OutputIndex);
					}
					return FReply::Handled();
				})
				[
					SNew(STextBlock)
					.Font(OscuMenuStyle::Font(9))
					.ColorAndOpacity_Lambda([this, OutputIndex]()
					{
						return Capturing == ECapture::MIDIOut && CapturingIndex == OutputIndex ? FSlateColor(OscuMenuStyle::Accent()) : FSlateColor::UseForeground();
					})
					.Text_Lambda([this, OutputIndex]()
					{
						return Capturing == ECapture::MIDIOut && CapturingIndex == OutputIndex
							? LOCTEXT("OutWaiting", "waiting…")
							: LOCTEXT("OutLearn", "learn");
					})
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(2.0f, 0.0f, 0.0f, 0.0f)
			[
				ResetButton(
					[Tag, Dispatcher]() { const FOscuPlayerOverride* O = FOscuPlayerMappings::Get().Find(Tag, Dispatcher, true); return O != nullptr && O->HasMIDI(); },
					[Tag, Dispatcher]() { FOscuPlayerMappings::Get().SetMIDI(Tag, Dispatcher, EOscuPlayerMIDIKind::None, 1, 0, true); },
					LOCTEXT("ResetMIDIOutTip", "Back to the project's MIDI message."))
			];

		Row->AddSlot().FillWidth(1.2f).VAlign(VAlign_Center).Padding(20.0f, 0.0f, 0.0f, 0.0f)[ Cell ];
	}

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SBox)
			.HeightOverride(1.0f)
			.Visibility(bDivider ? EVisibility::HitTestInvisible : EVisibility::Collapsed)
			[
				SNew(SImage).Image(OscuMenuStyle::Hairline())
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 7.0f)
		[
			Row
		];
}

void SOscuPlayerMenu::StartMIDIOutCapture(const int32 OutputIndex)
{
	UOscuMIDISubsystem* MIDI = UOscuMIDISubsystem::Get();
	const FOscuEventOutput* Output = GetOutput(OutputIndex);
	if (MIDI == nullptr || Output == nullptr)
	{
		return;
	}
	if (MIDI->GetOpenDeviceCount() == 0)
	{
		SetStatus(LOCTEXT("OutLearnNoDevice", "learn listens to a MIDI input, and none is open. Type the message instead, e.g. cc 21."), true);
		return;
	}

	Capturing = ECapture::MIDIOut;
	CapturingIndex = OutputIndex;
	SetStatus(FText::Format(LOCTEXT("OutLearnArmed", "Press the pad or move the knob that should receive \"{0}\"..."), Output->GetDisplayLabel()));

	const FName Tag = Output->Tag;
	const FName Dispatcher = Output->Dispatcher;
	const FText Label = Output->GetDisplayLabel();
	TWeakPtr<SOscuPlayerMenu> Weak = StaticCastSharedRef<SOscuPlayerMenu>(AsShared());

	MIDI->ArmCapture([Weak, Tag, Dispatcher, Label](const FOscuMIDICapture& Capture)
	{
		const TSharedPtr<SOscuPlayerMenu> Menu = Weak.Pin();
		if (Capture.Type == EOscuMIDIInputType::ProgramChange)
		{
			if (Menu.IsValid())
			{
				Menu->Capturing = ECapture::None;
				Menu->CapturingIndex = INDEX_NONE;
				Menu->SetStatus(LOCTEXT("OutLearnProgram", "That was a program change; outputs send notes or controllers. Try a pad or a knob."), true);
			}
			return;
		}

		const EOscuPlayerMIDIKind Kind = Capture.Type == EOscuMIDIInputType::ControlChange
			? EOscuPlayerMIDIKind::ControlChange : EOscuPlayerMIDIKind::Note;
		FOscuPlayerMappings::Get().SetMIDI(Tag, Dispatcher, Kind, Capture.Channel, Capture.Number, true);
		if (Menu.IsValid())
		{
			Menu->Capturing = ECapture::None;
			Menu->CapturingIndex = INDEX_NONE;
			Menu->SetStatus(FText::Format(LOCTEXT("OutLearnDone", "\"{0}\" now sends {1}."), Label,
				FText::FromString(UOscuEventOutputSubsystem::DescribeMIDIOut(Kind, Capture.Channel, Capture.Number))));
		}
	});
}

#undef LOCTEXT_NAMESPACE
