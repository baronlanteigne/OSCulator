// Copyright Baron Lanteigne. All Rights Reserved.

#include "OscuMIDIMapDetails.h"

#include "OSCulatorCore.h"
#include "OscuIntrospection.h"
#include "OscuMIDIMap.h"
#include "OscuRouterSubsystem.h"
#include "OscuSettings.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "GameFramework/Actor.h"
#include "PropertyHandle.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "OscuMIDIMapDetails"

namespace
{
	const FName ColStatus("Status");
	const FName ColFunction("Function");
	const FName ColType("Type");
	const FName ColValue("Value");
	const FName ColPitch("Pitch");
	const FName ColActions("Actions");

	const TCHAR* TypeDisplayName(EOscuMIDIInputType Type)
	{
		switch (Type)
		{
		case EOscuMIDIInputType::ControlChange: return TEXT("Control Change");
		case EOscuMIDIInputType::ProgramChange: return TEXT("Program Change");
		case EOscuMIDIInputType::NoteRange:     return TEXT("Note Range");
		default:                                return TEXT("Single Note");
		}
	}

	/** A dimmed inline caption, so a source row says what its boxes are. */
	TSharedRef<SWidget> Caption(const FText& Text)
	{
		return SNew(STextBlock)
			.Text(Text)
			.ColorAndOpacity(FSlateColor::UseSubduedForeground());
	}
}

/**
 * The row shell. Holds its indices and asks the customization for each cell.
 *
 * A raw owner pointer: the tree, its rows and the customization are torn down together
 * when the panel closes, so a row cannot outlive what it points at.
 */
/**
 * A source row. One strip across the whole row rather than cells in the binding's
 * columns, because the two have nothing in common column-wise.
 */
class SOscuSourceRowWidget : public STableRow<TSharedPtr<FOscuMapRow>>
{
public:
	SLATE_BEGIN_ARGS(SOscuSourceRowWidget) {}
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& OwnerTable)
	{
		STableRow<TSharedPtr<FOscuMapRow>>::Construct(
			STableRow<TSharedPtr<FOscuMapRow>>::FArguments()
			.Content()
			[
				InArgs._Content.Widget
			],
			OwnerTable);
	}
};

class SOscuMapRowWidget : public SMultiColumnTableRow<TSharedPtr<FOscuMapRow>>
{
public:
	SLATE_BEGIN_ARGS(SOscuMapRowWidget) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& OwnerTable,
		TSharedPtr<FOscuMapRow> InRow, FOscuMIDIMapDetails* InOwner)
	{
		Row = InRow;
		Owner = InOwner;
		SMultiColumnTableRow<TSharedPtr<FOscuMapRow>>::Construct(FSuperRowType::FArguments(), OwnerTable);
	}

	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override
	{
		if (Owner == nullptr || !Row.IsValid())
		{
			return SNullWidget::NullWidget;
		}

		TSharedRef<SWidget> Cell = Owner->GenerateCell(ColumnName, Row);

		// The expander goes in the first column only, and only there is the indent
		// applied -- otherwise every column of a child row would shift right.
		if (ColumnName == ColStatus)
		{
			return SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SExpanderArrow, SharedThis(this)).IndentAmount(10.0f)
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					Cell
				];
		}

		return Cell;
	}

private:
	TSharedPtr<FOscuMapRow> Row;
	FOscuMIDIMapDetails* Owner = nullptr;
};

TSharedRef<IDetailCustomization> FOscuMIDIMapDetails::MakeInstance()
{
	return MakeShared<FOscuMIDIMapDetails>();
}

void FOscuMIDIMapDetails::PostUndo(bool bSuccess)
{
	RebuildRows();
}

void FOscuMIDIMapDetails::PostRedo(bool bSuccess)
{
	RebuildRows();
}

UWorld* FOscuMIDIMapDetails::EditorWorld()
{
	return (GEditor != nullptr) ? GEditor->GetEditorWorldContext().World() : nullptr;
}

void FOscuMIDIMapDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> Objects;
	DetailBuilder.GetObjectsBeingCustomized(Objects);

	// One asset only. A table of two assets' bindings side by side would have to say
	// which row came from where on every row, and nobody multi-selects show files.
	if (Objects.Num() != 1)
	{
		return;
	}

	Map = Cast<UOscuMIDIMap>(Objects[0].Get());
	UOscuMIDIMap* MapPtr = Map.Get();
	if (MapPtr == nullptr)
	{
		return;
	}

	BindingsHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UOscuMIDIMap, Bindings));
	BindingsArray = BindingsHandle.IsValid() ? BindingsHandle->AsArray() : nullptr;
	if (!BindingsArray.IsValid())
	{
		return;
	}

	// Only on a LENGTH change, deliberately. Hooking every value change would rebuild
	// the tree on each tick of a spin box drag, which throws away the widget the user
	// is dragging. Every cell reads through a lambda instead, so it follows the data
	// without the row being regenerated.
	BindingsArray->SetOnNumElementsChanged(
		FSimpleDelegate::CreateSP(this, &FOscuMIDIMapDetails::RebuildRows));

	// Open informative. Validate reports and never edits -- it does not dirty the
	// package, which is what makes it safe to run unprompted -- and the status it
	// writes is the entire point of the first column. Only when something has not been
	// judged yet, so a panel rebuild does not re-walk the level every time.
	if (UWorld* World = EditorWorld())
	{
		const bool bAnyUnchecked = MapPtr->Bindings.ContainsByPredicate(
			[](const FOscuMIDIBinding& Binding)
			{
				return Binding.Status == EOscuMIDIRowStatus::Unchecked;
			});

		if (bAnyUnchecked)
		{
			MapPtr->Validate(World);
		}
	}

	// Reading order, not enum order: the two note kinds belong next to each other.
	TypeOptions.Reset();
	TypeValues.Reset();
	for (const EOscuMIDIInputType Value : {
			EOscuMIDIInputType::Note,
			EOscuMIDIInputType::NoteRange,
			EOscuMIDIInputType::ControlChange,
			EOscuMIDIInputType::ProgramChange })
	{
		TypeOptions.Add(MakeShared<FString>(TypeDisplayName(Value)));
		TypeValues.Add(Value);
	}

	SortColumn = ColFunction;

	// Its own category at Important priority, so it lands ABOVE the stock "OSCulator"
	// one instead of after it. The alternative was re-adding every existing property to
	// control the order, which would also mean re-adding the CallInEditor buttons by
	// hand -- more code, and one rename away from silently dropping a button.
	IDetailCategoryBuilder& Category = DetailBuilder.EditCategory(
		"MIDI Mapping", LOCTEXT("MappingCategory", "MIDI Mapping"), ECategoryPriority::Important);

	Category.AddCustomRow(LOCTEXT("BindingsTableFilterText", "bindings table"))
		.WholeRowContent()
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 2.0f, 0.0f, 4.0f)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0.0f, 0.0f, 6.0f, 0.0f)
				[
					SNew(SButton)
					.ContentPadding(FMargin(8.0f, 2.0f))
					.ToolTipText(LOCTEXT("AddActorTip",
						"Opens an empty section. Type the actor's tag -- the part after the prefix -- "
						"then click Scan to list every function it exposes in the open level."))
					.OnClicked_Lambda([this]() { AddPendingTag(); return FReply::Handled(); })
					[
						SNew(STextBlock).Text(LOCTEXT("AddActor", "+ Actor"))
					]
				]

				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SNew(SSearchBox)
					.HintText(LOCTEXT("FilterHint", "Filter by tag, function, input or device"))
					.OnTextChanged(this, &FOscuMIDIMapDetails::OnFilterChanged)
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(6.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.ContentPadding(FMargin(6.0f, 2.0f))
					.ToolTipText(LOCTEXT("ExpandAllTip", "Open every tag section."))
					.OnClicked_Lambda([this]() { SetAllExpanded(true); return FReply::Handled(); })
					[
						SNew(STextBlock).Text(LOCTEXT("ExpandAll", "Expand all"))
					]
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(4.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.ContentPadding(FMargin(6.0f, 2.0f))
					.ToolTipText(LOCTEXT("CollapseAllTip",
						"Close every tag section. An actor with forty functions is forty rows; this is how "
						"you get the one you are working on back on screen."))
					.OnClicked_Lambda([this]() { SetAllExpanded(false); return FReply::Handled(); })
					[
						SNew(STextBlock).Text(LOCTEXT("CollapseAll", "Collapse all"))
					]
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(6.0f, 0.0f, 0.0f, 0.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text_Lambda([this]()
					{
						const UOscuMIDIMap* M = Map.Get();
						const int32 Total = (M != nullptr) ? M->Bindings.Num() : 0;

						// Rows are SECTIONS now, so counting them would report tags and
						// call them bindings. The visible count is summed from the
						// sections instead.
						int32 Shown = 0;
						for (const TSharedPtr<FOscuMapRow>& Row : Rows)
						{
							Shown += Row->NumBindings;
						}
						return FText::Format(LOCTEXT("RowCount", "{0} of {1}"),
							FText::AsNumber(Shown), FText::AsNumber(Total));
					})
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				// Bounded, and scrolling: a details row that grew with the asset would
				// push every other property off the bottom at fifty bindings.
				SNew(SBox)
				.MaxDesiredHeight(460.0f)
				[
					SAssignNew(TreeView, STreeView<TSharedPtr<FOscuMapRow>>)
					.TreeItemsSource(&Rows)
					.SelectionMode(ESelectionMode::Single)
					.OnGenerateRow(this, &FOscuMIDIMapDetails::OnGenerateRow)
					.OnGetChildren(this, &FOscuMIDIMapDetails::OnGetChildren)
					.OnExpansionChanged_Lambda([this](TSharedPtr<FOscuMapRow> Row, bool bExpanded)
					{
						// Only sections are remembered. A binding's expansion follows
						// whether it has inputs, which is derived rather than chosen.
						if (Row.IsValid() && Row->IsTag())
						{
							if (bExpanded)
							{
								CollapsedTags.Remove(Row->Tag);
							}
							else
							{
								CollapsedTags.Add(Row->Tag);
							}
						}
					})
					.HeaderRow(
						SNew(SHeaderRow)

						+ SHeaderRow::Column(ColStatus)
						.DefaultLabel(FText::GetEmpty())
						.FixedWidth(46.0f)

						// Every header names ONE thing now. That is only possible
						// because a source spans the row instead of borrowing these.
						+ SHeaderRow::Column(ColFunction)
						.DefaultLabel(LOCTEXT("ColFunction", "Function"))
						.FillWidth(0.26f)
						.SortMode(this, &FOscuMIDIMapDetails::GetSortMode, ColFunction)
						.OnSort(this, &FOscuMIDIMapDetails::OnSortColumn)

						+ SHeaderRow::Column(ColType)
						.DefaultLabel(LOCTEXT("ColType", "Type"))
						.FillWidth(0.13f)

						+ SHeaderRow::Column(ColValue)
						.DefaultLabel(LOCTEXT("ColValue", "Value →       remap out"))
						.FillWidth(0.305f)

						+ SHeaderRow::Column(ColPitch)
						.DefaultLabel(LOCTEXT("ColPitch", "Pitch →       remap out"))
						.FillWidth(0.305f)

						+ SHeaderRow::Column(ColActions)
						.DefaultLabel(FText::GetEmpty())
						.FixedWidth(172.0f)
					)
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 4.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("StatusLegend",
					"● mapped    ○ no input yet    ▲ function missing    △ tag not in this level"))
				.ToolTipText(LOCTEXT("StatusLegendTip",
					"On Note Off, Send Source Number and Available Parameters are under Bindings below, "
					"along with the Auto Map Rules."))
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			]
		];

	RebuildRows();
}

const FOscuMIDIBinding* FOscuMIDIMapDetails::GetBinding(int32 BindingIndex) const
{
	const UOscuMIDIMap* MapPtr = Map.Get();
	return (MapPtr != nullptr && MapPtr->Bindings.IsValidIndex(BindingIndex))
		? &MapPtr->Bindings[BindingIndex] : nullptr;
}

const FOscuMIDISource* FOscuMIDIMapDetails::GetSource(int32 BindingIndex, int32 SourceIndex) const
{
	const FOscuMIDIBinding* Binding = GetBinding(BindingIndex);
	return (Binding != nullptr && Binding->Sources.IsValidIndex(SourceIndex))
		? &Binding->Sources[SourceIndex] : nullptr;
}

// ---------------------------------------------------------------- handles

TSharedPtr<IPropertyHandle> FOscuMIDIMapDetails::GetBindingField(int32 BindingIndex, FName Field) const
{
	if (!BindingsArray.IsValid() || GetBinding(BindingIndex) == nullptr)
	{
		return nullptr;
	}
	return BindingsArray->GetElement(BindingIndex)->GetChildHandle(Field);
}

TSharedPtr<IPropertyHandleArray> FOscuMIDIMapDetails::GetSourcesArray(int32 BindingIndex) const
{
	const TSharedPtr<IPropertyHandle> Handle =
		GetBindingField(BindingIndex, GET_MEMBER_NAME_CHECKED(FOscuMIDIBinding, Sources));
	return Handle.IsValid() ? Handle->AsArray() : nullptr;
}

TSharedPtr<IPropertyHandle> FOscuMIDIMapDetails::GetSourceField(int32 BindingIndex, int32 SourceIndex, FName Field) const
{
	const TSharedPtr<IPropertyHandleArray> Sources = GetSourcesArray(BindingIndex);
	if (!Sources.IsValid() || GetSource(BindingIndex, SourceIndex) == nullptr)
	{
		return nullptr;
	}
	return Sources->GetElement(SourceIndex)->GetChildHandle(Field);
}

// ---------------------------------------------------------------- tree

void FOscuMIDIMapDetails::RebuildRows()
{
	Rows.Reset();

	const UOscuMIDIMap* MapPtr = Map.Get();
	if (MapPtr == nullptr)
	{
		if (TreeView.IsValid())
		{
			TreeView->RequestTreeRefresh();
		}
		return;
	}

	// Gather the bindings that survive the filter, keyed by tag. One pass, because the
	// filter has to see a binding's sources and those are only reachable from it.
	TMap<FName, TArray<int32>> ByTag;

	for (int32 BindingIndex = 0; BindingIndex < MapPtr->Bindings.Num(); ++BindingIndex)
	{
		const FOscuMIDIBinding& Binding = MapPtr->Bindings[BindingIndex];

		if (!Filter.IsEmpty())
		{
			// One haystack rather than four comparisons, so "laser fire" is not a
			// search that can never match anything. Sources are in it because "which
			// row is on CC 7" is a question people ask.
			FString Haystack = FString::Printf(TEXT("%s %s"),
				*Binding.Tag.ToString(), *Binding.FunctionName.ToString());

			for (const FOscuMIDISource& Source : Binding.Sources)
			{
				Haystack += TEXT(" ");
				Haystack += Source.Summary;
			}

			if (!Haystack.Contains(Filter, ESearchCase::IgnoreCase))
			{
				continue;
			}
		}

		ByTag.FindOrAdd(Binding.Tag).Add(BindingIndex);
	}

	TArray<FName> TagOrder;
	ByTag.GetKeys(TagOrder);

	// Sections always alphabetical. The column sort orders rows WITHIN a section, which
	// is the only place it has anything to say once the rows are grouped.
	TagOrder.Sort([](const FName& A, const FName& B) { return A.Compare(B) < 0; });

	const bool bAscending = SortMode != EColumnSortMode::Descending;
	const FName Column = SortColumn;

	for (const FName& Tag : TagOrder)
	{
		TArray<int32>& Indices = ByTag[Tag];

		Indices.Sort([this, Column, bAscending](const int32& A, const int32& B)
		{
			const FOscuMIDIBinding* BindingA = GetBinding(A);
			const FOscuMIDIBinding* BindingB = GetBinding(B);
			if (BindingA == nullptr || BindingB == nullptr)
			{
				return false;
			}

			// Tag is the section now, so sorting by it inside one section is a no-op.
			// Function is the only column left that orders a section's rows.
			const FString Left = BindingA->FunctionName.ToString();
			const FString Right = BindingB->FunctionName.ToString();

			const int32 Compare = Left.Compare(Right, ESearchCase::IgnoreCase);
			return bAscending ? Compare < 0 : Compare > 0;
		});

		TSharedPtr<FOscuMapRow> TagRow =
			MakeShared<FOscuMapRow>(EOscuMapRowKind::Tag, Tag, INDEX_NONE, INDEX_NONE);

		for (const int32 BindingIndex : Indices)
		{
			const FOscuMIDIBinding& Binding = MapPtr->Bindings[BindingIndex];

			TSharedPtr<FOscuMapRow> BindingRow =
				MakeShared<FOscuMapRow>(EOscuMapRowKind::Binding, Tag, BindingIndex, INDEX_NONE);

			for (int32 SourceIndex = 0; SourceIndex < Binding.Sources.Num(); ++SourceIndex)
			{
				BindingRow->Children.Add(
					MakeShared<FOscuMapRow>(EOscuMapRowKind::Source, Tag, BindingIndex, SourceIndex));
			}

			++TagRow->NumBindings;
			if (Binding.Sources.Num() > 0)
			{
				++TagRow->NumAssigned;
			}
			if (Binding.Status == EOscuMIDIRowStatus::FunctionMissing
				|| Binding.Status == EOscuMIDIRowStatus::TagMissing)
			{
				++TagRow->NumBroken;
			}

			TagRow->Children.Add(BindingRow);
		}

		Rows.Add(TagRow);
	}

	// The section being added, if its tag is not already one of the real ones. Last, so
	// an empty section never pushes the populated ones down the panel.
	if (PendingTag.IsSet() && !ByTag.Contains(PendingTag.GetValue()))
	{
		TSharedPtr<FOscuMapRow> PendingRow =
			MakeShared<FOscuMapRow>(EOscuMapRowKind::Tag, PendingTag.GetValue(), INDEX_NONE, INDEX_NONE);
		PendingRow->bPending = true;
		Rows.Add(PendingRow);
	}

	if (TreeView.IsValid())
	{
		for (const TSharedPtr<FOscuMapRow>& TagRow : Rows)
		{
			// Open unless the user closed this one. Stored as collapsed rather than
			// expanded so a tag seen for the first time starts open.
			TreeView->SetItemExpansion(TagRow, !CollapsedTags.Contains(TagRow->Tag));

			for (const TSharedPtr<FOscuMapRow>& BindingRow : TagRow->Children)
			{
				// A source is the thing you came to edit, so a mapped row shows its
				// input rather than hiding it one click further away than the stock
				// panel did.
				TreeView->SetItemExpansion(BindingRow, BindingRow->Children.Num() > 0);
			}
		}
		TreeView->RequestTreeRefresh();
	}
}

void FOscuMIDIMapDetails::SetAllExpanded(bool bExpanded)
{
	CollapsedTags.Reset();

	if (!bExpanded)
	{
		for (const TSharedPtr<FOscuMapRow>& Row : Rows)
		{
			CollapsedTags.Add(Row->Tag);
		}
	}

	RebuildRows();
}

void FOscuMIDIMapDetails::OnGetChildren(TSharedPtr<FOscuMapRow> Row, TArray<TSharedPtr<FOscuMapRow>>& OutChildren)
{
	if (Row.IsValid())
	{
		OutChildren = Row->Children;
	}
}

TSharedRef<ITableRow> FOscuMIDIMapDetails::OnGenerateRow(
	TSharedPtr<FOscuMapRow> Row, const TSharedRef<STableViewBase>& OwnerTable)
{
	if (Row.IsValid() && Row->IsTag())
	{
		return SNew(SOscuSourceRowWidget, OwnerTable)
			[
				MakeTagRow(Row)
			];
	}

	if (Row.IsValid() && Row->IsSource())
	{
		return SNew(SOscuSourceRowWidget, OwnerTable)
			[
				SNew(SHorizontalBox)

				// Indented by hand: a spanning row has no column to hang an expander
				// arrow's indent on, and without this a source sits flush with its
				// binding instead of under it.
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SSpacer).Size(FVector2D(38.0f, 1.0f))
				]

				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					MakeSourceStrip(Row->BindingIndex, Row->SourceIndex)
				]
			];
	}

	return SNew(SOscuMapRowWidget, OwnerTable, Row, this);
}

TSharedRef<SWidget> FOscuMIDIMapDetails::GenerateCell(const FName& ColumnId, const TSharedPtr<FOscuMapRow>& Row)
{
	if (!Row.IsValid())
	{
		return SNullWidget::NullWidget;
	}

	const int32 B = Row->BindingIndex;
	const int32 S = Row->SourceIndex;

	if (ColumnId == ColStatus)   { return MakeStatusCell(B); }
	if (ColumnId == ColFunction) { return MakeNameCell(B, GET_MEMBER_NAME_CHECKED(FOscuMIDIBinding, FunctionName)); }
	if (ColumnId == ColType)     { return MakeTypeCell(B); }
	if (ColumnId == ColValue)    { return MakeParamCell(B, GET_MEMBER_NAME_CHECKED(FOscuMIDIBinding, VelocityParam)); }
	if (ColumnId == ColPitch)    { return MakeParamCell(B, GET_MEMBER_NAME_CHECKED(FOscuMIDIBinding, PitchParam)); }
	if (ColumnId == ColActions)  { return MakeBindingActionsCell(B); }
	return SNullWidget::NullWidget;
}

// ---------------------------------------------------------------- section cells

TSharedRef<SWidget> FOscuMIDIMapDetails::MakeTagRow(const TSharedPtr<FOscuMapRow>& Row)
{
	const FName Tag = Row->Tag;
	const bool bPending = Row->bPending;

	TSharedRef<SHorizontalBox> Box = SNew(SHorizontalBox)

		// Our own arrow rather than SExpanderArrow: a spanning row has no column for
		// the engine's one to indent against, and a section is always at depth 0 so
		// there is no indent to compute anyway.
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.ContentPadding(FMargin(4.0f, 1.0f))
			.Visibility(bPending ? EVisibility::Hidden : EVisibility::Visible)
			.OnClicked_Lambda([this, Row]()
			{
				if (TreeView.IsValid())
				{
					TreeView->SetItemExpansion(Row, !TreeView->IsItemExpanded(Row));
				}
				return FReply::Handled();
			})
			[
				SNew(STextBlock)
				.Text_Lambda([this, Row]()
				{
					const bool bOpen = TreeView.IsValid() && TreeView->IsItemExpanded(Row);
					return FText::FromString(bOpen ? TEXT("▾") : TEXT("▸"));
				})
			]
		]

		// Says what the box is. "cube" on its own read as a function name.
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(2.0f, 0.0f, 6.0f, 0.0f)
		[
			Caption(LOCTEXT("ActorTagLabel", "Actor tag"))
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SBox).MinDesiredWidth(160.0f)[ MakeTagNameCell(Row) ]
		]

		// Beside the field, because it acts on the thing you just typed.
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(6.0f, 0.0f, 0.0f, 0.0f)
		[
			MakeTagActionsCell(Tag)
		]

		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.VAlign(VAlign_Center)
		.Padding(14.0f, 0.0f, 0.0f, 0.0f)
		[
			MakeTagSummaryCell(Row)
		];

	// Only the section being added can be thrown away here. Deleting a populated one
	// means deleting every binding under it, which is not something a header button
	// should do quietly.
	if (bPending)
	{
		Box->AddSlot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.ContentPadding(FMargin(3.0f, 1.0f))
				.ToolTipText(LOCTEXT("DropPendingTip", "Discard this empty section."))
				.OnClicked_Lambda([this]() { DeletePendingTag(); return FReply::Handled(); })
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT("×")))
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				]
			];
	}

	return Box;
}

void FOscuMIDIMapDetails::DeletePendingTag()
{
	PendingTag.Reset();
	RebuildRows();
}

TSharedRef<SWidget> FOscuMIDIMapDetails::MakeTagNameCell(const TSharedPtr<FOscuMapRow>& Row)
{
	const FName Tag = Row->Tag;
	const bool bPending = Row->bPending;

	return SNew(SBox)
		.VAlign(VAlign_Center)
		.Padding(2.0f, 2.0f)
		[
			SNew(SEditableTextBox)
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
			// A binding with no tag is legal and reaches nothing. Grouped and named
			// rather than hidden, because a row that cannot fire should be findable.
			.Text(Tag.IsNone() ? FText::GetEmpty() : FText::FromName(Tag))
			.HintText(bPending
				? LOCTEXT("TypeTagHint", "type the actor's tag, then Scan")
				: LOCTEXT("NoTagSection", "(no tag)"))
			.SelectAllTextWhenFocused(true)
			.ToolTipText(Tag.IsNone()
				? LOCTEXT("NoTagSectionTip",
					"These bindings have no tag, so nothing can reach them. Type one here to give it "
					"to all of them at once.")
				: FText::Format(LOCTEXT("TagSectionTip",
					"Everything mapped under the tag \"{0}\" -- every actor in the level tagged {1}{0} "
					"answers here. Editing this renames the tag on every row in the section."),
					FText::FromName(Tag), FText::FromString(UOscuSettings::Get()->TagPrefix)))
			.OnTextCommitted_Lambda([this, Tag, bPending](const FText& NewText, ETextCommit::Type)
			{
				const FString Trimmed = NewText.ToString().TrimStartAndEnd();
				const FName Typed = Trimmed.IsEmpty() ? NAME_None : FName(*Trimmed);

				if (bPending)
				{
					// Nothing is under it yet, so there is nothing to rename -- the
					// section simply becomes the tag you typed.
					PendingTag = Typed;
					RebuildRows();
					return;
				}

				RenameTag(Tag, Typed);
			})
		];
}

TSharedRef<SWidget> FOscuMIDIMapDetails::MakeTagSummaryCell(const TSharedPtr<FOscuMapRow>& Row) const
{
	if (Row->bPending)
	{
		return SNew(SBox)
			.VAlign(VAlign_Center)
			.Padding(2.0f, 0.0f)
			[
				Caption(LOCTEXT("PendingHint", "nothing yet"))
			];
	}

	const int32 Total = Row->NumBindings;
	const int32 Assigned = Row->NumAssigned;
	const int32 Broken = Row->NumBroken;

	// Unassigned is the number that matters at this scale: auto-mapping an actor with
	// forty functions lists forty rows, and the question is how many still need an input.
	FString Text = FString::Printf(TEXT("%d function%s   \u00B7   %d mapped"),
		Total, Total == 1 ? TEXT("") : TEXT("s"), Assigned);

	if (Total > Assigned)
	{
		Text += FString::Printf(TEXT("   \u00B7   %d unassigned"), Total - Assigned);
	}
	if (Broken > 0)
	{
		Text += FString::Printf(TEXT("   \u00B7   %d broken"), Broken);
	}

	return SNew(SBox)
		.VAlign(VAlign_Center)
		.Padding(2.0f, 0.0f)
		[
			SNew(STextBlock)
			.Text(FText::FromString(Text))
			.ColorAndOpacity(Broken > 0
				? FSlateColor(FLinearColor(0.9f, 0.45f, 0.3f))
				: FSlateColor::UseSubduedForeground())
		];
}

TSharedRef<SWidget> FOscuMIDIMapDetails::MakeTagActionsCell(FName Tag)
{
	return SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.0f, 0.0f, 4.0f, 0.0f)
		[
			SNew(SButton)
			.ContentPadding(FMargin(6.0f, 1.0f))
			.ToolTipText(LOCTEXT("ScanTagTip",
				"Lists every function this tag exposes in the open level, as rows ready to be given an "
				"input. Additive: your existing rows and their inputs are never touched, so scanning "
				"again after adding a Blueprint event just adds the new one."))
			// A tag that has not been typed yet has nothing to scan for.
			.IsEnabled_Lambda([Tag]() { return !Tag.IsNone(); })
			.OnClicked_Lambda([this, Tag]() { ScanTag(Tag); return FReply::Handled(); })
			[
				SNew(STextBlock).Text(LOCTEXT("ScanTag", "Scan"))
			]
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.ContentPadding(FMargin(3.0f, 1.0f))
			.ToolTipText(LOCTEXT("AddBindingForTagTip",
				"Adds one empty function row under this tag, for a name Scan cannot find -- a native "
				"function, or a Blueprint event you have not compiled yet."))
			.IsEnabled_Lambda([Tag]() { return !Tag.IsNone(); })
			.OnClicked_Lambda([this, Tag]() { AddBindingForTag(Tag); return FReply::Handled(); })
			[
				SNew(STextBlock).Text(LOCTEXT("AddFunction", "+ function"))
			]
		];
}

void FOscuMIDIMapDetails::AddPendingTag()
{
	PendingTag = NAME_None;
	RebuildRows();
}

void FOscuMIDIMapDetails::ScanTag(FName Tag)
{
	UOscuMIDIMap* MapPtr = Map.Get();
	if (MapPtr == nullptr || Tag.IsNone())
	{
		return;
	}

	UWorld* World = EditorWorld();
	UOscuRouterSubsystem* Router = UOscuRouterSubsystem::Get(World);
	if (Router == nullptr)
	{
		UE_LOG(LogOSCulator, Warning, TEXT("%s: Scan needs an open level."), *MapPtr->GetName());
		return;
	}

	// The editor world never runs OnWorldBeginPlay, so the registry has to be filled
	// before it can be asked anything.
	Router->ScanWorld();

	// Blueprint-authored only, and one tag. A fully exposed actor drags in a couple of
	// hundred inherited engine functions, and offering those as pads would be absurd.
	const TArray<FOscuExposedTagInfo> Found =
		Router->Introspect(EOscuIntrospectFilter::Custom, Tag);

	if (Found.Num() == 0)
	{
		UE_LOG(LogOSCulator, Warning,
			TEXT("%s: nothing in the open level carries the tag '%s%s'. Check the actor's Tags, and "
				 "that its Blueprint is compiled."),
			*MapPtr->GetName(), *UOscuSettings::Get()->TagPrefix, *Tag.ToString());
		return;
	}

	FScopedTransaction Transaction(LOCTEXT("OscuScanTag", "Scan OSCulator tag"));
	MapPtr->Modify();

	int32 Added = 0;
	int32 Assigned = 0;

	for (const FOscuExposedTagInfo& TagInfo : Found)
	{
		TArray<FName> Names;
		TMap<FName, TArray<FOscuExposedParamInfo>> ParamsByFunction;
		Names.Reserve(TagInfo.Functions.Num());

		for (const FOscuExposedFunctionInfo& Function : TagInfo.Functions)
		{
			Names.Add(Function.FunctionName);
			ParamsByFunction.Add(Function.FunctionName, Function.Params);
		}

		Added += MapPtr->MergeFunctions(TagInfo.Tag, Names, ParamsByFunction, &Assigned);
	}

	UE_LOG(LogOSCulator, Log, TEXT("%s: scanned '%s' -- +%d function(s), %d input(s) assigned."),
		*MapPtr->GetName(), *Tag.ToString(), Added, Assigned);

	if (Added > 0)
	{
		MapPtr->MarkPackageDirty();
	}

	// So the rows arrive with a status instead of a row of grey circles.
	MapPtr->Validate(World);
	MapPtr->PostEditChange();

	// It has real rows now, so it is no longer the section being added.
	if (PendingTag.IsSet() && PendingTag.GetValue() == Tag)
	{
		PendingTag.Reset();
	}

	RebuildRows();
}

void FOscuMIDIMapDetails::RenameTag(FName OldTag, FName NewTag)
{
	if (OldTag == NewTag)
	{
		return;
	}

	UOscuMIDIMap* MapPtr = Map.Get();
	if (MapPtr == nullptr || !BindingsArray.IsValid())
	{
		return;
	}

	// One transaction around the lot, so renaming a forty-row section is one undo
	// rather than forty. Each write still goes through its handle, which is what runs
	// the asset's Refresh.
	FScopedTransaction Transaction(LOCTEXT("OscuRenameTag", "Rename OSCulator tag"));

	for (int32 Index = 0; Index < MapPtr->Bindings.Num(); ++Index)
	{
		if (MapPtr->Bindings[Index].Tag != OldTag)
		{
			continue;
		}

		if (const TSharedPtr<IPropertyHandle> Handle =
			GetBindingField(Index, GET_MEMBER_NAME_CHECKED(FOscuMIDIBinding, Tag)))
		{
			Handle->SetValue(NewTag);
		}
	}

	// The section keeps whatever open state it had under its old name.
	if (CollapsedTags.Remove(OldTag) > 0)
	{
		CollapsedTags.Add(NewTag);
	}

	RebuildRows();
}

void FOscuMIDIMapDetails::AddBindingForTag(FName Tag)
{
	if (!BindingsArray.IsValid())
	{
		return;
	}

	BindingsArray->AddItem();

	if (PendingTag.IsSet() && PendingTag.GetValue() == Tag)
	{
		PendingTag.Reset();
	}

	// The new element is the last one. Filling its tag is what keeps it in the section
	// it was added from -- without this it lands under "(no tag)", which reads as the
	// button having done nothing.
	const UOscuMIDIMap* MapPtr = Map.Get();
	if (MapPtr != nullptr && !Tag.IsNone() && MapPtr->Bindings.Num() > 0)
	{
		if (const TSharedPtr<IPropertyHandle> Handle =
			GetBindingField(MapPtr->Bindings.Num() - 1, GET_MEMBER_NAME_CHECKED(FOscuMIDIBinding, Tag)))
		{
			Handle->SetValue(Tag);
		}
	}

	RebuildRows();
}

// ---------------------------------------------------------------- binding cells

TSharedRef<SWidget> FOscuMIDIMapDetails::MakeStatusCell(int32 B) const
{
	// Lambdas throughout, so a later Validate reaches the cell without the tree being
	// rebuilt underneath the user's scroll position.
	return SNew(SBox)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text_Lambda([this, B]()
			{
				const FOscuMIDIBinding* Binding = GetBinding(B);
				if (Binding == nullptr)
				{
					return FText::GetEmpty();
				}
				switch (Binding->Status)
				{
				case EOscuMIDIRowStatus::FunctionMissing: return FText::FromString(TEXT("▲"));
				case EOscuMIDIRowStatus::TagMissing:      return FText::FromString(TEXT("△"));
				case EOscuMIDIRowStatus::Found:
					// Found with nothing to fire it is not a fault and should not read
					// as one: it is what Auto-Map produces before you assign.
					return FText::FromString(Binding->Sources.Num() == 0 ? TEXT("○") : TEXT("●"));
				default:                                  return FText::FromString(TEXT("○"));
				}
			})
			.ColorAndOpacity_Lambda([this, B]()
			{
				const FOscuMIDIBinding* Binding = GetBinding(B);
				if (Binding == nullptr)
				{
					return FSlateColor::UseSubduedForeground();
				}
				switch (Binding->Status)
				{
				case EOscuMIDIRowStatus::FunctionMissing: return FSlateColor(FLinearColor(0.9f, 0.25f, 0.2f));
				case EOscuMIDIRowStatus::TagMissing:      return FSlateColor(FLinearColor(0.95f, 0.7f, 0.1f));
				case EOscuMIDIRowStatus::Found:
					return Binding->Sources.Num() == 0
						? FSlateColor(FLinearColor(0.4f, 0.65f, 0.95f))
						: FSlateColor(FLinearColor(0.3f, 0.8f, 0.35f));
				default:                                  return FSlateColor::UseSubduedForeground();
				}
			})
			.ToolTipText_Lambda([this, B]()
			{
				const FOscuMIDIBinding* Binding = GetBinding(B);
				if (Binding == nullptr)
				{
					return FText::GetEmpty();
				}
				switch (Binding->Status)
				{
				case EOscuMIDIRowStatus::FunctionMissing:
					return LOCTEXT("TipFunctionMissing",
						"Actors carry this tag, but none has this function. Prune Missing Functions deletes "
						"rows in this state; retyping the name keeps the row's inputs and ranges.");
				case EOscuMIDIRowStatus::TagMissing:
					return LOCTEXT("TipTagMissing",
						"Nothing in the open level carries this tag, so the row cannot be judged. Probably "
						"for another level -- Prune leaves these alone.");
				case EOscuMIDIRowStatus::Found:
					return Binding->Sources.Num() == 0
						? LOCTEXT("TipUnassigned", "The function exists, but nothing fires it yet. Add an input with + or Learn.")
						: LOCTEXT("TipFound", "The function was found on at least one actor carrying this tag.");
				default:
					return LOCTEXT("TipUnchecked", "Not checked against a level yet. Click Validate Against Level.");
				}
			})
		];
}

TSharedRef<SWidget> FOscuMIDIMapDetails::MakeNameCell(int32 B, FName Field) const
{
	const bool bIsTag = Field == GET_MEMBER_NAME_CHECKED(FOscuMIDIBinding, Tag);

	return SNew(SBox)
		.VAlign(VAlign_Center)
		.Padding(2.0f, 1.0f)
		[
			SNew(SEditableTextBox)
			.Text_Lambda([this, B, bIsTag]()
			{
				const FOscuMIDIBinding* Binding = GetBinding(B);
				if (Binding == nullptr)
				{
					return FText::GetEmpty();
				}
				const FName Value = bIsTag ? Binding->Tag : Binding->FunctionName;
				return Value.IsNone() ? FText::GetEmpty() : FText::FromName(Value);
			})
			.HintText(bIsTag ? LOCTEXT("TagHint", "tag") : LOCTEXT("FunctionHint", "function"))
			.SelectAllTextWhenFocused(true)
			.OnTextCommitted_Lambda([this, B, Field](const FText& NewText, ETextCommit::Type)
			{
				// Through the handle, so the write runs PostEditChangeProperty and the
				// map's Refresh with it. Setting the member would look identical and
				// leave the lookup pointing at the old name.
				if (const TSharedPtr<IPropertyHandle> Handle = GetBindingField(B, Field))
				{
					const FString Trimmed = NewText.ToString().TrimStartAndEnd();
					Handle->SetValue(Trimmed.IsEmpty() ? NAME_None : FName(*Trimmed));
				}
			})
		];
}

TSharedRef<SWidget> FOscuMIDIMapDetails::MakeTypeCell(int32 B)
{
	return SNew(SBox)
		.VAlign(VAlign_Center)
		.Padding(2.0f, 1.0f)
		[
			SNew(SComboBox<TSharedPtr<FString>>)
			.OptionsSource(&TypeOptions)
			.ToolTipText(LOCTEXT("TypeTip",
				"What kind of message fires this row. Per binding rather than per input, because a note "
				"carries a pitch and a release and a controller carries neither. Changing it re-points "
				"every input on the row -- note 36 becomes CC 36."))
			.OnGenerateWidget_Lambda([](TSharedPtr<FString> Option)
			{
				return SNew(STextBlock).Text(FText::FromString(*Option));
			})
			.OnSelectionChanged_Lambda([this, B](TSharedPtr<FString> Option, ESelectInfo::Type SelectInfo)
			{
				if (SelectInfo == ESelectInfo::Direct || !Option.IsValid())
				{
					return;
				}

				const int32 Chosen = TypeOptions.IndexOfByKey(Option);
				if (!TypeValues.IsValidIndex(Chosen))
				{
					return;
				}

				if (const TSharedPtr<IPropertyHandle> Handle =
					GetBindingField(B, GET_MEMBER_NAME_CHECKED(FOscuMIDIBinding, Type)))
				{
					Handle->SetValue(static_cast<uint8>(TypeValues[Chosen]));
				}

				// The input cell is a different WIDGET per kind -- a note name box, a
				// CC spinner, a program spinner -- so the rows have to be regenerated
				// rather than just redrawn.
				RebuildRows();
			})
			[
				SNew(STextBlock)
				.Text_Lambda([this, B]()
				{
					const FOscuMIDIBinding* Binding = GetBinding(B);
					return Binding != nullptr
						? FText::FromString(TypeDisplayName(Binding->Type))
						: FText::GetEmpty();
				})
			]
		];
}

TSharedRef<SWidget> FOscuMIDIMapDetails::MakeParamCell(int32 B, FName Field)
{
	const bool bPitch = Field == GET_MEMBER_NAME_CHECKED(FOscuMIDIBinding, PitchParam);

	const FOscuMIDIBinding* Binding = GetBinding(B);
	if (Binding == nullptr)
	{
		return SNullWidget::NullWidget;
	}

	// Pitch is a note idea. A control change carries a value and a controller number that
	// means nothing on a scale, and a program change is an identity -- so the cell goes
	// away rather than offering a choice that cannot do anything.
	if (bPitch && Binding->Type != EOscuMIDIInputType::NoteRange)
	{
		return SNew(SBox)
			.VAlign(VAlign_Center)
			.Padding(6.0f, 0.0f)
			[
				Caption(LOCTEXT("PitchNA", "\u2014"))
			];
	}

	const FText Tip = bPitch
		? LOCTEXT("PitchParamTip",
			"Which parameter the note's PITCH drives, by name. Empty means pitch is not sent at all, "
			"which is what a binding on a single note wants -- one pitch carries no information. Tick "
			"Range on an input below to give pitch something to say. Remap Pitch and Pitch Out Min/Max "
			"are under Bindings below.")
		: LOCTEXT("ValueParamTip",
			"Which parameter the message's value drives, by name. Empty means the first one. The list is "
			"read from the live signature in the open level -- which is why the stock panel could only "
			"ever show the names as text. Remap and Out Min/Max are under Bindings below.");

	TSharedRef<SHorizontalBox> Box = SNew(SHorizontalBox);

	Box->AddSlot()
		.FillWidth(0.52f)
		.VAlign(VAlign_Center)
		[
			SNew(SComboButton)
			.ContentPadding(FMargin(4.0f, 2.0f))
			.ToolTipText(Tip)
			.OnGetMenuContent_Lambda([this, B, Field]() -> TSharedRef<SWidget>
			{
				// Built on click rather than held per row: the answer is a fact about
				// whichever level is open, and a stored one would be a claim about the
				// level that was open when the panel was built.
				FMenuBuilder Menu(true, nullptr);

				Menu.AddMenuEntry(
					LOCTEXT("ParamNoneEntry", "(none)"),
					LOCTEXT("ParamNoneEntryTip", "Clears the assignment."),
					FSlateIcon(),
					FUIAction(FExecuteAction::CreateLambda([this, B, Field]()
					{
						if (const TSharedPtr<IPropertyHandle> Handle = GetBindingField(B, Field))
						{
							Handle->SetValue(NAME_None);
						}
					})));

				const TArray<FName> Names = GetParamNames(B);
				if (Names.Num() == 0)
				{
					Menu.AddMenuEntry(
						LOCTEXT("ParamNone", "nothing drivable in this level"),
						LOCTEXT("ParamNoneTip",
							"Either no actor in the open level exposes this function, or it offers no "
							"float, int or byte parameter. Click Validate Against Level."),
						FSlateIcon(),
						FUIAction(FExecuteAction(), FCanExecuteAction::CreateLambda([]() { return false; })));
					return Menu.MakeWidget();
				}

				Menu.BeginSection(NAME_None, LOCTEXT("ParamSection", "Parameters"));
				for (const FName& Name : Names)
				{
					Menu.AddMenuEntry(
						FText::FromName(Name),
						FText::GetEmpty(),
						FSlateIcon(),
						FUIAction(FExecuteAction::CreateLambda([this, B, Field, Name]()
						{
							if (const TSharedPtr<IPropertyHandle> Handle = GetBindingField(B, Field))
							{
								Handle->SetValue(Name);
							}
						})));
				}
				Menu.EndSection();

				return Menu.MakeWidget();
			})
			.ButtonContent()
			[
				SNew(STextBlock)
				.Text_Lambda([this, B, bPitch]()
				{
					const FOscuMIDIBinding* Row = GetBinding(B);
					if (Row == nullptr)
					{
						return FText::GetEmpty();
					}
					const FName Value = bPitch ? Row->PitchParam : Row->VelocityParam;
					if (!Value.IsNone())
					{
						return FText::FromName(Value);
					}
					// An unset VELOCITY still lands somewhere -- the first parameter --
					// while an unset PITCH is simply not sent. Different defaults, so
					// they must not read the same.
					return bPitch ? LOCTEXT("PitchNotSent", "(not sent)") : LOCTEXT("ParamFirstShort", "(first)");
				})
				.ColorAndOpacity_Lambda([this, B, bPitch]()
				{
					const FOscuMIDIBinding* Row = GetBinding(B);
					if (Row == nullptr)
					{
						return FSlateColor::UseSubduedForeground();
					}
					const FName Value = bPitch ? Row->PitchParam : Row->VelocityParam;
					return Value.IsNone() ? FSlateColor::UseSubduedForeground() : FSlateColor::UseForeground();
				})
			]
		];

	// A program number is an identity, not a magnitude: squashing patch 5 into 0.039 is
	// never what anyone meant, so the range is absent rather than greyed. Same reasoning
	// the asset uses to hide it with EditConditionHides.
	if (!bPitch && Binding->Type == EOscuMIDIInputType::ProgramChange)
	{
		return Box;
	}

	// Pitch and velocity get their OWN remap. They are separate measurements that
	// routinely want different ranges -- a pitch sweeping 0..1 while velocity drives a
	// 0..10 intensity -- which is why the asset keeps two sets of fields and why one
	// shared range here would have been wrong.
	const FName RemapField = bPitch
		? GET_MEMBER_NAME_CHECKED(FOscuMIDIBinding, bRemapPitch)
		: GET_MEMBER_NAME_CHECKED(FOscuMIDIBinding, bRemap);
	const FName MinField = bPitch
		? GET_MEMBER_NAME_CHECKED(FOscuMIDIBinding, PitchOutMin)
		: GET_MEMBER_NAME_CHECKED(FOscuMIDIBinding, OutMin);
	const FName MaxField = bPitch
		? GET_MEMBER_NAME_CHECKED(FOscuMIDIBinding, PitchOutMax)
		: GET_MEMBER_NAME_CHECKED(FOscuMIDIBinding, OutMax);

	auto IsRemapped = [this, B, bPitch]()
	{
		const FOscuMIDIBinding* Row = GetBinding(B);
		return Row != nullptr && (bPitch ? Row->bRemapPitch : Row->bRemap);
	};

	auto Bound = [this, B, bPitch](FName Field, const FText& Tip) -> TSharedRef<SWidget>
	{
		return SNew(SSpinBox<float>)
			.MinDesiredWidth(42.0f)
			.ToolTipText(Tip)
			.Value_Lambda([this, B, bPitch, Field]()
			{
				const FOscuMIDIBinding* Row = GetBinding(B);
				if (Row == nullptr)
				{
					return 0.0f;
				}
				const bool bMin = Field == GET_MEMBER_NAME_CHECKED(FOscuMIDIBinding, OutMin)
					|| Field == GET_MEMBER_NAME_CHECKED(FOscuMIDIBinding, PitchOutMin);
				if (bPitch)
				{
					return bMin ? Row->PitchOutMin : Row->PitchOutMax;
				}
				return bMin ? Row->OutMin : Row->OutMax;
			})
			// Interactive while dragging, so a drag is one undo step rather than a
			// transaction per tick; the commit is the one that counts.
			.OnValueChanged_Lambda([this, B, Field](float NewValue)
			{
				if (const TSharedPtr<IPropertyHandle> Handle = GetBindingField(B, Field))
				{
					Handle->SetValue(NewValue, EPropertyValueSetFlags::InteractiveChange);
				}
			})
			.OnValueCommitted_Lambda([this, B, Field](float NewValue, ETextCommit::Type)
			{
				if (const TSharedPtr<IPropertyHandle> Handle = GetBindingField(B, Field))
				{
					Handle->SetValue(NewValue);
				}
			});
	};

	Box->AddSlot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(8.0f, 0.0f, 4.0f, 0.0f)
		[
			SNew(SCheckBox)
			.ToolTipText(bPitch
				? LOCTEXT("RemapPitchTip",
					"Rescale the pitch's position in its span into the range beside this. Untick it to "
					"get the raw note number instead, which is also how you ask for pitch as an identity "
					"rather than a position.")
				: LOCTEXT("RemapTip",
					"Rescale the incoming 0-127 into the range beside this before calling. On by "
					"default, because 0-1 is what the rest of the pipeline speaks and a raw 0-127 "
					"landing in a function expecting a normalised float is a silent hundredfold error."))
			.IsChecked_Lambda([IsRemapped]()
			{
				return IsRemapped() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
			})
			.OnCheckStateChanged_Lambda([this, B, RemapField](ECheckBoxState NewState)
			{
				if (const TSharedPtr<IPropertyHandle> Handle = GetBindingField(B, RemapField))
				{
					const bool bChecked = NewState == ECheckBoxState::Checked;
					Handle->SetValue(bChecked);
				}
			})
		];

	Box->AddSlot()
		.FillWidth(0.48f)
		.VAlign(VAlign_Center)
		[
			// Hidden rather than greyed when remap is off: an unused range is noise.
			SNew(SHorizontalBox)
			.Visibility_Lambda([IsRemapped]()
			{
				return IsRemapped() ? EVisibility::Visible : EVisibility::Hidden;
			})

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				Bound(MinField, LOCTEXT("OutMinTip",
					"The value sent at the bottom. May exceed the max, which inverts."))
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(3.0f, 0.0f)
			[
				Caption(LOCTEXT("RangeTo", "→"))
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				Bound(MaxField, LOCTEXT("OutMaxTip", "The value sent at the top."))
			]
		];

	return Box;
}

TSharedRef<SWidget> FOscuMIDIMapDetails::MakeBindingActionsCell(int32 B)
{
	return SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.VAlign(VAlign_Center)
		[
			SNew(SButton)
			.HAlign(HAlign_Center)
			.ContentPadding(FMargin(4.0f, 1.0f))
			// Hidden once the row HAS an input, because each input carries its own
			// Learn. Two buttons with the same word on one row, one of them meaning
			// "the first one", is a question nobody should have to answer.
			.Visibility_Lambda([this, B]()
			{
				const FOscuMIDIBinding* Row = GetBinding(B);
				return (Row != nullptr && Row->Sources.Num() == 0)
					? EVisibility::Visible : EVisibility::Collapsed;
			})
			.ToolTipText(LOCTEXT("LearnTip",
				"Gives this function an input and arms it: play a note or turn a knob and it captures "
				"the device, channel, kind and number in one gesture."))
			.OnClicked_Lambda([this, B]() { ToggleLearn(B, INDEX_NONE); return FReply::Handled(); })
			[
				SNew(STextBlock)
				.Text_Lambda([this, B]()
				{
					return IsArmed(B, INDEX_NONE) ? LOCTEXT("Listening", "● …") : LOCTEXT("Learn", "Learn");
				})
				.ColorAndOpacity_Lambda([this, B]()
				{
					return IsArmed(B, INDEX_NONE)
						? FSlateColor(FLinearColor(0.95f, 0.4f, 0.25f))
						: FSlateColor::UseForeground();
				})
			]
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(2.0f, 0.0f, 0.0f, 0.0f)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.ContentPadding(FMargin(3.0f, 1.0f))
			.ToolTipText(LOCTEXT("AddSourceTip",
				"Adds an input to this function. Several inputs mean a pad AND a second controller can "
				"reach it without anything about the target being duplicated."))
			.OnClicked_Lambda([this, B]() { AddSource(B); return FReply::Handled(); })
			[
				SNew(STextBlock).Text(LOCTEXT("AddInput", "+ input"))
			]
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.ContentPadding(FMargin(3.0f, 1.0f))
			.ToolTipText(LOCTEXT("DeleteBindingTip", "Delete this binding and all its inputs."))
			.OnClicked_Lambda([this, B]() { DeleteBinding(B); return FReply::Handled(); })
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("×")))
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			]
		];
}

// ---------------------------------------------------------------- source cells

TSharedRef<SWidget> FOscuMIDIMapDetails::MakeChannelCell(int32 B, int32 S)
{
	return SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(2.0f, 0.0f, 4.0f, 0.0f)
		[
			Caption(LOCTEXT("ChannelCaption", "ch"))
		]

		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.VAlign(VAlign_Center)
		[
			SNew(SSpinBox<int32>)
			.MinValue(1)
			.MaxValue(16)
			.MinDesiredWidth(36.0f)
			.ToolTipText(LOCTEXT("ChannelTip", "1-16, as every DAW displays it."))
			.Value_Lambda([this, B, S]()
			{
				const FOscuMIDISource* Source = GetSource(B, S);
				return Source != nullptr ? static_cast<int32>(Source->Channel) : 1;
			})
			.OnValueChanged_Lambda([this, B, S](int32 NewValue)
			{
				if (const TSharedPtr<IPropertyHandle> Handle =
					GetSourceField(B, S, GET_MEMBER_NAME_CHECKED(FOscuMIDISource, Channel)))
				{
					Handle->SetValue(static_cast<uint8>(NewValue), EPropertyValueSetFlags::InteractiveChange);
				}
			})
			.OnValueCommitted_Lambda([this, B, S](int32 NewValue, ETextCommit::Type)
			{
				if (const TSharedPtr<IPropertyHandle> Handle =
					GetSourceField(B, S, GET_MEMBER_NAME_CHECKED(FOscuMIDISource, Channel)))
				{
					Handle->SetValue(static_cast<uint8>(NewValue));
				}
			})
		];
}

TSharedRef<SWidget> FOscuMIDIMapDetails::MakeInputCell(int32 B, int32 S)
{
	const FOscuMIDIBinding* Binding = GetBinding(B);
	if (Binding == nullptr)
	{
		return SNullWidget::NullWidget;
	}

	// A different WIDGET per kind rather than one box that means three things: a note is
	// spelled ("C#2", or a bare number), and a controller or a program is a number with
	// a hard 0-127 range that a spinner can enforce.
	if (!OscuIsNoteType(Binding->Type))
	{
		const bool bProgram = Binding->Type == EOscuMIDIInputType::ProgramChange;
		const FName Field = bProgram
			? GET_MEMBER_NAME_CHECKED(FOscuMIDISource, ProgramNumber)
			: GET_MEMBER_NAME_CHECKED(FOscuMIDISource, ControlNumber);

		return SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(2.0f, 0.0f, 4.0f, 0.0f)
			[
				Caption(bProgram ? LOCTEXT("ProgCaption", "prog") : LOCTEXT("CCCaption", "CC"))
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SNew(SSpinBox<int32>)
				.MinValue(0)
				.MaxValue(127)
				.MinDesiredWidth(44.0f)
				.ToolTipText(bProgram
					? LOCTEXT("ProgTip",
						"Which program, numbered from 0 as it is on the wire. Hardware disagrees with "
						"itself about this -- plenty of synths show patch 1 for program 0 -- so this is "
						"the number that arrives, not the number on the box's screen.")
					: LOCTEXT("CCTip", "Controller number, 0-127."))
				.Value_Lambda([this, B, S, bProgram]()
				{
					const FOscuMIDISource* Source = GetSource(B, S);
					if (Source == nullptr)
					{
						return 0;
					}
					return static_cast<int32>(bProgram ? Source->ProgramNumber : Source->ControlNumber);
				})
				.OnValueChanged_Lambda([this, B, S, Field](int32 NewValue)
				{
					if (const TSharedPtr<IPropertyHandle> Handle = GetSourceField(B, S, Field))
					{
						Handle->SetValue(static_cast<uint8>(NewValue), EPropertyValueSetFlags::InteractiveChange);
					}
				})
				.OnValueCommitted_Lambda([this, B, S, Field](int32 NewValue, ETextCommit::Type)
				{
					if (const TSharedPtr<IPropertyHandle> Handle = GetSourceField(B, S, Field))
					{
						Handle->SetValue(static_cast<uint8>(NewValue));
					}
				})
			];
	}

	// Note. The text is stored as typed and RESOLVED by the asset's Refresh, so the box
	// accepts "C3", "C#2", "Db2" or a bare "61" and the resolved number appears in the
	// column at the end of the row. Writing through the handle is what runs that.
	auto NoteBox = [this, B, S](FName Field, const FText& Hint, const FText& Tip) -> TSharedRef<SWidget>
	{
		return SNew(SEditableTextBox)
			.HintText(Hint)
			.ToolTipText(Tip)
			.SelectAllTextWhenFocused(true)
			.Text_Lambda([this, B, S, Field]()
			{
				const FOscuMIDISource* Source = GetSource(B, S);
				if (Source == nullptr)
				{
					return FText::GetEmpty();
				}
				const bool bHigh = Field == GET_MEMBER_NAME_CHECKED(FOscuMIDISource, NoteHigh);
				return FText::FromString(bHigh ? Source->NoteHigh : Source->Note);
			})
			.OnTextCommitted_Lambda([this, B, S, Field](const FText& NewText, ETextCommit::Type)
			{
				if (const TSharedPtr<IPropertyHandle> Handle = GetSourceField(B, S, Field))
				{
					Handle->SetValue(NewText.ToString().TrimStartAndEnd());
				}
			});
	};

	// From the ROW's kind now. A source cannot disagree with its binding about this.
	const bool bRange = Binding->Type == EOscuMIDIInputType::NoteRange;

	TSharedRef<SHorizontalBox> Box = SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(2.0f, 0.0f, 4.0f, 0.0f)
		[
			Caption(LOCTEXT("NoteCaption", "note"))
		]

		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.VAlign(VAlign_Center)
		[
			NoteBox(GET_MEMBER_NAME_CHECKED(FOscuMIDISource, Note),
				LOCTEXT("NoteHint", "C3"),
				LOCTEXT("NoteTip", "\"C3\", \"C#2\", \"Db2\", or a bare \"61\". Normalised to sharps when edited. "
					"Note 60 is C3 in Ableton and C4 in scientific pitch notation -- Middle C Octave in "
					"Project Settings decides which this means."))
		];

	if (bRange)
	{
		Box->AddSlot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(3.0f, 0.0f)
			[
				Caption(LOCTEXT("NoteTo", "→"))
			];

		Box->AddSlot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				NoteBox(GET_MEMBER_NAME_CHECKED(FOscuMIDISource, NoteHigh),
					LOCTEXT("NoteHighHint", "C4"),
					LOCTEXT("NoteHighTip", "The top of the span, inclusive. Same spelling rules. An inverted "
						"pair is swapped rather than left matching nothing."))
			];
	}

	return Box;
}

TSharedRef<SWidget> FOscuMIDIMapDetails::MakeDeviceCell(int32 B, int32 S)
{
	return SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(2.0f, 0.0f, 4.0f, 0.0f)
		[
			Caption(LOCTEXT("FromCaption", "from"))
		]

		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.VAlign(VAlign_Center)
		[
			SNew(SEditableTextBox)
			// Empty is the useful default and the hint says so, rather than the row
			// carrying a device name that was only ever typed to stop it looking blank.
			.HintText_Lambda([this]()
			{
				const UOscuMIDIMap* MapPtr = Map.Get();
				if (MapPtr != nullptr && !MapPtr->DefaultDevice.IsNone())
				{
					return FText::Format(LOCTEXT("DeviceInherits", "{0}"), FText::FromName(MapPtr->DefaultDevice));
				}
				return LOCTEXT("DeviceAny", "any device");
			})
			.ToolTipText(LOCTEXT("DeviceTip",
				"Which device this must arrive from. Empty inherits the asset's Default Device, and an "
				"empty one of those means any device -- which is what a one-controller setup wants. "
				"Fill it in when two controllers send the same note and you want only one of them. "
				"OSCulator.MIDIDevices prints the names."))
			.Text_Lambda([this, B, S]()
			{
				const FOscuMIDISource* Source = GetSource(B, S);
				return (Source != nullptr && !Source->Device.IsNone())
					? FText::FromName(Source->Device) : FText::GetEmpty();
			})
			.OnTextCommitted_Lambda([this, B, S](const FText& NewText, ETextCommit::Type)
			{
				if (const TSharedPtr<IPropertyHandle> Handle =
					GetSourceField(B, S, GET_MEMBER_NAME_CHECKED(FOscuMIDISource, Device)))
				{
					const FString Trimmed = NewText.ToString().TrimStartAndEnd();
					Handle->SetValue(Trimmed.IsEmpty() ? NAME_None : FName(*Trimmed));
				}
			})
		];
}

TSharedRef<SWidget> FOscuMIDIMapDetails::MakeResolvedCell(int32 B, int32 S) const
{
	// What the spelling actually resolved to. Always visible, never editable: a name
	// alone is ambiguous across octave conventions, and showing the number next to it
	// removes the ambiguity without making anyone go and check the setting.
	return SNew(SBox)
		.VAlign(VAlign_Center)
		.Padding(4.0f, 0.0f)
		[
			SNew(STextBlock)
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.Text_Lambda([this, B, S]()
			{
				const FOscuMIDIBinding* Binding = GetBinding(B);
				const FOscuMIDISource* Source = GetSource(B, S);
				if (Binding == nullptr || Source == nullptr || !OscuIsNoteType(Binding->Type))
				{
					return FText::GetEmpty();
				}

				if (Source->IsNoteRange())
				{
					return FText::FromString(FString::Printf(TEXT("= %d–%d  (%d notes)"),
						Source->GetNoteLow(), Source->GetNoteHigh(),
						Source->GetNoteHigh() - Source->GetNoteLow() + 1));
				}
				return FText::FromString(FString::Printf(TEXT("= %d"), Source->ResolvedNote));
			})
		];
}

TSharedRef<SWidget> FOscuMIDIMapDetails::MakeSourceActionsCell(int32 B, int32 S)
{
	return SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.VAlign(VAlign_Center)
		[
			SNew(SButton)
			.HAlign(HAlign_Center)
			.ContentPadding(FMargin(4.0f, 1.0f))
			.ToolTipText(LOCTEXT("LearnSourceTip",
				"Arm THIS input, then play a note or turn a knob. It captures the device, channel, kind "
				"and number in one gesture. Only one input across the asset can be armed at a time."))
			.OnClicked_Lambda([this, B, S]() { ToggleLearn(B, S); return FReply::Handled(); })
			[
				SNew(STextBlock)
				.Text_Lambda([this, B, S]()
				{
					return IsArmed(B, S) ? LOCTEXT("Listening2", "● …") : LOCTEXT("Learn2", "Learn");
				})
				.ColorAndOpacity_Lambda([this, B, S]()
				{
					return IsArmed(B, S)
						? FSlateColor(FLinearColor(0.95f, 0.4f, 0.25f))
						: FSlateColor::UseForeground();
				})
			]
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(2.0f, 0.0f, 0.0f, 0.0f)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.ContentPadding(FMargin(3.0f, 1.0f))
			.ToolTipText(LOCTEXT("DeleteSourceTip", "Remove this input. The binding and its value settings stay."))
			.OnClicked_Lambda([this, B, S]() { DeleteSource(B, S); return FReply::Handled(); })
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("×")))
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			]
		];
}

TSharedRef<SWidget> FOscuMIDIMapDetails::MakeSourceStrip(int32 B, int32 S)
{
	return SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.0f, 0.0f, 10.0f, 0.0f)
		[
			SNew(SBox).MinDesiredWidth(74.0f)[ MakeChannelCell(B, S) ]
		]

		+ SHorizontalBox::Slot()
		.FillWidth(0.40f)
		.VAlign(VAlign_Center)
		[
			MakeInputCell(B, S)
		]

		+ SHorizontalBox::Slot()
		.FillWidth(0.33f)
		.VAlign(VAlign_Center)
		[
			MakeDeviceCell(B, S)
		]

		+ SHorizontalBox::Slot()
		.FillWidth(0.27f)
		.VAlign(VAlign_Center)
		[
			MakeResolvedCell(B, S)
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SBox).MinDesiredWidth(112.0f)[ MakeSourceActionsCell(B, S) ]
		];
}

// ---------------------------------------------------------------- actions

void FOscuMIDIMapDetails::AddBinding()
{
	if (BindingsArray.IsValid())
	{
		BindingsArray->AddItem();
		RebuildRows();
	}
}

void FOscuMIDIMapDetails::DeleteBinding(int32 B)
{
	if (BindingsArray.IsValid() && GetBinding(B) != nullptr)
	{
		BindingsArray->DeleteItem(B);
		RebuildRows();
	}
}

void FOscuMIDIMapDetails::AddSource(int32 B)
{
	if (const TSharedPtr<IPropertyHandleArray> Sources = GetSourcesArray(B))
	{
		// The new source's kind is mirrored down from the binding by the asset's
		// Refresh, which the handle write runs. Nothing to set here.
		Sources->AddItem();
		RebuildRows();
	}
}

void FOscuMIDIMapDetails::DeleteSource(int32 B, int32 S)
{
	if (const TSharedPtr<IPropertyHandleArray> Sources = GetSourcesArray(B))
	{
		if (GetSource(B, S) != nullptr)
		{
			Sources->DeleteItem(S);
			RebuildRows();
		}
	}
}

bool FOscuMIDIMapDetails::IsArmed(int32 B, int32 S) const
{
	const FOscuMIDIBinding* Binding = GetBinding(B);
	if (Binding == nullptr)
	{
		return false;
	}

	if (S == INDEX_NONE)
	{
		return Binding->Sources.ContainsByPredicate([](const FOscuMIDISource& Source) { return Source.bLearn; });
	}

	const FOscuMIDISource* Source = GetSource(B, S);
	return Source != nullptr && Source->bLearn;
}

void FOscuMIDIMapDetails::ToggleLearn(int32 B, int32 S)
{
	UOscuMIDIMap* MapPtr = Map.Get();
	if (MapPtr == nullptr || !MapPtr->Bindings.IsValidIndex(B))
	{
		return;
	}

	const bool bWasArmed = IsArmed(B, S);
	const bool bNeedsSource = !bWasArmed && S == INDEX_NONE && MapPtr->Bindings[B].Sources.Num() == 0;

	// Only an added source is a real edit -- bLearn is Transient and never saved -- so
	// the transaction is opened for the case that changes the asset.
	FScopedTransaction Transaction(LOCTEXT("OscuToggleLearn", "Arm OSCulator MIDI Learn"));
	MapPtr->Modify();

	// One armed source across the whole asset. The same invariant UpdateLearnArming
	// enforces when the checkboxes are used, applied here so the two cannot disagree.
	for (FOscuMIDIBinding& Binding : MapPtr->Bindings)
	{
		for (FOscuMIDISource& Source : Binding.Sources)
		{
			Source.bLearn = false;
		}
	}

	if (!bWasArmed)
	{
		if (bNeedsSource)
		{
			// Arming an unassigned row is the moment to give it an input: that is the
			// state Auto-Map leaves every row in, and there is nothing to teach without
			// one. The kind comes from the binding when Refresh mirrors it down.
			MapPtr->Bindings[B].Sources.AddDefaulted();
		}

		const int32 Target = (S == INDEX_NONE) ? 0 : S;
		if (MapPtr->Bindings[B].Sources.IsValidIndex(Target))
		{
			MapPtr->Bindings[B].Sources[Target].bLearn = true;
		}
	}

	MapPtr->Refresh();
	MapPtr->UpdateLearnArming();

	if (bNeedsSource)
	{
		MapPtr->MarkPackageDirty();
		RebuildRows();
	}
}

TArray<FName> FOscuMIDIMapDetails::GetParamNames(int32 B) const
{
	TArray<FName> Names;

	const FOscuMIDIBinding* Binding = GetBinding(B);
	if (Binding == nullptr)
	{
		return Names;
	}

	const UOscuRouterSubsystem* Router = UOscuRouterSubsystem::Get(EditorWorld());
	if (Router == nullptr)
	{
		return Names;
	}

	TArray<FOscuExposedParamInfo> Params;
	TArray<int32> Slots;
	if (!Router->DescribeParams(Binding->Tag, Binding->FunctionName, Params, Slots))
	{
		return Names;
	}

	for (const FOscuExposedParamInfo& Param : Params)
	{
		// Only what a value can drive. An output pin takes nothing from the message, and
		// a vec3 or a string cannot be filled from one knob -- offering them would read
		// as a list of valid choices and not be one.
		if (!Param.bOutputOnly && Param.bContinuous)
		{
			Names.Add(Param.Name);
		}
	}

	return Names;
}

// ---------------------------------------------------------------- sort and filter

EColumnSortMode::Type FOscuMIDIMapDetails::GetSortMode(FName ColumnId) const
{
	return (ColumnId == SortColumn) ? SortMode : EColumnSortMode::None;
}

void FOscuMIDIMapDetails::OnSortColumn(
	EColumnSortPriority::Type Priority, const FName& ColumnId, EColumnSortMode::Type NewMode)
{
	SortColumn = ColumnId;
	SortMode = NewMode;
	RebuildRows();
}

void FOscuMIDIMapDetails::OnFilterChanged(const FText& NewText)
{
	Filter = NewText.ToString().TrimStartAndEnd();
	RebuildRows();
}

#undef LOCTEXT_NAMESPACE
