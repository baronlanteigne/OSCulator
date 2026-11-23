// Copyright Baron Lanteigne. All Rights Reserved.

#include "OscuPlayerMenuEntryCustomization.h"

#include "OscuRouterSubsystem.h"
#include "OscuSettings.h"

#include "DetailWidgetRow.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "GameFramework/Actor.h"
#include "Editor.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "IDetailChildrenBuilder.h"
#include "PropertyHandle.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "OSCulatorEditor"

namespace
{
	/** The level being edited, scanned fresh: the picker should show what is tagged now. */
	UOscuRouterSubsystem* ScannedRouter()
	{
		UWorld* World = GEditor != nullptr ? GEditor->GetEditorWorldContext().World() : nullptr;
		UOscuRouterSubsystem* Router = UOscuRouterSubsystem::Get(World);
		if (Router != nullptr)
		{
			Router->ScanWorld();
		}
		return Router;
	}

	FName GetName(const TSharedPtr<IPropertyHandle>& Handle)
	{
		FName Value;
		if (Handle.IsValid())
		{
			Handle->GetValue(Value);
		}
		return Value;
	}
}

TSharedRef<IPropertyTypeCustomization> FOscuPlayerMenuEntryCustomization::MakeInstance()
{
	return MakeShared<FOscuPlayerMenuEntryCustomization>();
}

void FOscuPlayerMenuEntryCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	// Shared by both structs: an event output names a dispatcher where an entry names a function.
	TagHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FOscuPlayerMenuEntry, Tag));
	FunctionHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FOscuEventOutput, Dispatcher));
	bOutput = FunctionHandle.IsValid() && FunctionHandle->IsValidHandle();
	if (!bOutput)
	{
		FunctionHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FOscuPlayerMenuEntry, FunctionName));
	}
	LabelHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FOscuPlayerMenuEntry, Label));

	HeaderRow
	.NameContent()
	[
		PropertyHandle->CreatePropertyNameWidget()
	]
	.ValueContent()
	.MinDesiredWidth(300.0f)
	[
		SNew(STextBlock)
		.Font(CustomizationUtils.GetRegularFont())
		.Text_Lambda([this]()
		{
			const FName Tag = GetName(TagHandle);
			const FName Function = GetName(FunctionHandle);
			if (Tag.IsNone() || Function.IsNone())
			{
				return bOutput
					? LOCTEXT("OutputUnset", "Pick a tag and an event dispatcher")
					: LOCTEXT("EntryUnset", "Pick a tag and a function");
			}

			FText Label;
			if (LabelHandle.IsValid())
			{
				LabelHandle->GetValueAsDisplayText(Label);
			}
			const FString Address = FString::Printf(TEXT("/%s/%s"), *Tag.ToString(), *Function.ToString());
			return Label.IsEmpty()
				? FText::FromString(Address)
				: FText::Format(LOCTEXT("EntrySummary", "{0}   {1}"), Label, FText::FromString(Address));
		})
	];
}

void FOscuPlayerMenuEntryCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	uint32 NumChildren = 0;
	PropertyHandle->GetNumChildren(NumChildren);

	for (uint32 Index = 0; Index < NumChildren; ++Index)
	{
		const TSharedRef<IPropertyHandle> Child = PropertyHandle->GetChildHandle(Index).ToSharedRef();
		const FName Name = Child->GetProperty() != nullptr ? Child->GetProperty()->GetFName() : NAME_None;

		const bool bTag = Name == GET_MEMBER_NAME_CHECKED(FOscuPlayerMenuEntry, Tag);
		const bool bFunction = Name == GET_MEMBER_NAME_CHECKED(FOscuPlayerMenuEntry, FunctionName)
			|| Name == GET_MEMBER_NAME_CHECKED(FOscuEventOutput, Dispatcher);
		if (!bTag && !bFunction)
		{
			ChildBuilder.AddProperty(Child);
			continue;
		}

		ChildBuilder.AddProperty(Child)
		.CustomWidget()
		.NameContent()
		[
			Child->CreatePropertyNameWidget()
		]
		.ValueContent()
		.MinDesiredWidth(300.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				Child->CreatePropertyValueWidget()
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(4.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SComboButton)
				.ToolTipText(bTag
					? LOCTEXT("PickTagTip", "Tags in the open level (prefix stripped).")
					: (bOutput
						? LOCTEXT("PickDispatcherTip", "Event dispatchers on the tag's actors.")
						: LOCTEXT("PickFunctionTip", "Blueprint functions and events the tag's actors expose.")))
				.OnGetMenuContent_Lambda([this, bTag]()
				{
					return bTag ? MakeTagMenu() : MakeFunctionMenu();
				})
				.ButtonContent()
				[
					SNew(STextBlock)
					.Font(CustomizationUtils.GetRegularFont())
					.Text(LOCTEXT("Pick", "Pick"))
				]
			]
		];
	}
}

TSharedRef<SWidget> FOscuPlayerMenuEntryCustomization::MakeTagMenu()
{
	FMenuBuilder Menu(/*bShouldCloseWindowAfterMenuSelection*/ true, nullptr);

	const UOscuRouterSubsystem* Router = ScannedRouter();
	const TArray<FOscuExposedTagInfo> Tags = Router != nullptr
		? Router->Introspect(EOscuIntrospectFilter::ActorsOnly)
		: TArray<FOscuExposedTagInfo>();

	if (Tags.Num() == 0)
	{
		Menu.AddMenuEntry(LOCTEXT("NoTags", "No tagged actors in the open level"), FText::GetEmpty(), FSlateIcon(), FUIAction());
		return Menu.MakeWidget();
	}

	for (const FOscuExposedTagInfo& Info : Tags)
	{
		const FName Tag = Info.Tag;
		Menu.AddMenuEntry(
			FText::FromName(Tag),
			FText::Format(LOCTEXT("TagActors", "{0} actor(s)"), FText::AsNumber(Info.Actors.Num())),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([this, Tag]()
			{
				if (TagHandle.IsValid())
				{
					TagHandle->SetValue(Tag);
				}
			})));
	}
	return Menu.MakeWidget();
}

TSharedRef<SWidget> FOscuPlayerMenuEntryCustomization::MakeFunctionMenu()
{
	FMenuBuilder Menu(/*bShouldCloseWindowAfterMenuSelection*/ true, nullptr);

	const FName Tag = GetName(TagHandle);
	if (Tag.IsNone())
	{
		Menu.AddMenuEntry(LOCTEXT("PickTagFirst", "Pick a tag first"), FText::GetEmpty(), FSlateIcon(), FUIAction());
		return Menu.MakeWidget();
	}

	if (bOutput)
	{
		return MakeDispatcherMenu(Tag);
	}

	const UOscuRouterSubsystem* Router = ScannedRouter();
	const TArray<FOscuExposedTagInfo> Tags = Router != nullptr
		? Router->Introspect(EOscuIntrospectFilter::Custom, Tag)
		: TArray<FOscuExposedTagInfo>();

	bool bAny = false;
	for (const FOscuExposedTagInfo& Info : Tags)
	{
		for (const FOscuExposedFunctionInfo& Function : Info.Functions)
		{
			bAny = true;
			const FName FunctionName = Function.FunctionName;
			Menu.AddMenuEntry(
				FText::FromName(FunctionName),
				FText::FromString(FString::Printf(TEXT("%s  (%s)"), *Function.Address, *Function.GetSignatureString())),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateLambda([this, FunctionName]()
				{
					if (FunctionHandle.IsValid())
					{
						FunctionHandle->SetValue(FunctionName);
					}
				})));
		}
	}

	if (!bAny)
	{
		Menu.AddMenuEntry(
			FText::Format(LOCTEXT("NoFunctions", "Nothing exposed under '{0}' in the open level"), FText::FromName(Tag)),
			FText::GetEmpty(), FSlateIcon(), FUIAction());
	}
	return Menu.MakeWidget();
}

TSharedRef<SWidget> FOscuPlayerMenuEntryCustomization::MakeDispatcherMenu(const FName Tag)
{
	FMenuBuilder Menu(/*bShouldCloseWindowAfterMenuSelection*/ true, nullptr);

	TArray<AActor*> Actors;
	if (UOscuRouterSubsystem* Router = ScannedRouter())
	{
		Router->GatherActors(Tag, Actors);
	}

	// Blueprint-declared dispatchers only: an actor also carries a dozen engine ones
	// (OnDestroyed, overlaps...) that nobody means here. The field still takes any name.
	TSet<FName> Seen;
	for (const AActor* Actor : Actors)
	{
		if (Actor == nullptr)
		{
			continue;
		}
		for (TFieldIterator<FMulticastDelegateProperty> It(Actor->GetClass()); It; ++It)
		{
			const FMulticastDelegateProperty* Property = *It;
			if (!Property->HasAnyPropertyFlags(CPF_BlueprintAssignable)
				|| Cast<UBlueprintGeneratedClass>(Property->GetOwnerClass()) == nullptr
				|| Seen.Contains(Property->GetFName()))
			{
				continue;
			}
			Seen.Add(Property->GetFName());

			FString Signature;
			if (const UFunction* Sig = Property->SignatureFunction)
			{
				TArray<FString> Params;
				for (TFieldIterator<FProperty> P(Sig); P && P->HasAnyPropertyFlags(CPF_Parm); ++P)
				{
					Params.Add(FString::Printf(TEXT("%s %s"), *P->GetCPPType(), *P->GetName()));
				}
				Signature = FString::Join(Params, TEXT(", "));
			}

			const FName DispatcherName = Property->GetFName();
			Menu.AddMenuEntry(
				FText::FromName(DispatcherName),
				FText::FromString(FString::Printf(TEXT("/%s/%s  (%s)"), *Tag.ToString(), *DispatcherName.ToString(), *Signature)),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateLambda([this, DispatcherName]()
				{
					if (FunctionHandle.IsValid())
					{
						FunctionHandle->SetValue(DispatcherName);
					}
				})));
		}
	}

	if (Seen.Num() == 0)
	{
		Menu.AddMenuEntry(
			FText::Format(LOCTEXT("NoDispatchers", "No event dispatchers on '{0}' in the open level"), FText::FromName(Tag)),
			FText::GetEmpty(), FSlateIcon(), FUIAction());
	}
	return Menu.MakeWidget();
}

#undef LOCTEXT_NAMESPACE
