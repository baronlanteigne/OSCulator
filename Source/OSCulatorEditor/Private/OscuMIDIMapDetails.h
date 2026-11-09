// Copyright Baron Lanteigne. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorUndoClient.h"
#include "IDetailCustomization.h"
#include "Widgets/Views/STreeView.h"

class IDetailLayoutBuilder;
class IPropertyHandle;
class IPropertyHandleArray;
class UOscuMIDIMap;
class UWorld;
struct FOscuMIDIBinding;
struct FOscuMIDISource;

// Scoped with an explicit underlying type, so the name is enough here and the panel
// does not have to pull in the whole asset header.
enum class EOscuMIDIInputType : uint8;

/** What a row stands for. The three share column widths; each fills them differently. */
enum class EOscuMapRowKind : uint8
{
	/**
	 * A tag's section header. Holds no binding of its own; its children are the
	 * bindings under that tag.
	 *
	 * Grouping is by TAG rather than by actor, which is the same thing in most levels
	 * and deliberately not in all: a tag may be carried by several actors, and one actor
	 * may answer to several tags. The binding stores a tag, so the tag is the only thing
	 * the asset can honestly be grouped by.
	 */
	Tag,
	Binding,
	Source,
};

/**
 * One row: a tag section, a binding, or one of a binding's sources.
 *
 * INDICES into the asset, never pointers and never copies: the arrays reallocate when
 * they are edited, and a copy would be a snapshot that silently stopped agreeing with
 * the asset the moment anything wrote to it. Same reasoning as the map's own lookup.
 */
struct FOscuMapRow
{
	EOscuMapRowKind Kind = EOscuMapRowKind::Binding;

	/** The section this row is in. Set on every kind, so a child knows its group. */
	FName Tag;

	int32 BindingIndex = INDEX_NONE;
	int32 SourceIndex = INDEX_NONE;

	/** Rebuilt whenever the tree is; the view holds these, so they outlive the call. */
	TArray<TSharedPtr<FOscuMapRow>> Children;

	// Counts for a Tag row's one-line summary. Meaningless on the other kinds.
	int32 NumBindings = 0;
	int32 NumAssigned = 0;
	int32 NumBroken = 0;

	/**
	 * A section the user is in the middle of adding, which no binding is under yet.
	 *
	 * It exists only in the panel until it is scanned or given a function, so clicking
	 * "+ Actor" and changing your mind leaves nothing behind in the asset.
	 */
	bool bPending = false;

	bool IsTag() const { return Kind == EOscuMapRowKind::Tag; }
	bool IsSource() const { return Kind == EOscuMapRowKind::Source; }

	FOscuMapRow(EOscuMapRowKind InKind, FName InTag, int32 InBinding, int32 InSource)
		: Kind(InKind), Tag(InTag), BindingIndex(InBinding), SourceIndex(InSource) {}
};

/**
 * An editable table for the MIDI map, in place of the stock nested array.
 *
 * The stock widget buries a row's input three expanders deep, under
 * Bindings -> [n] -> Sources -> [n]. Fine for five rows, unusable for fifty, and it puts
 * the two things an author does most -- give a row an input, and say which parameter the
 * value drives -- behind the most clicks.
 *
 * The shape here follows the asset's own: a binding is the target and everything about
 * the VALUE it delivers; a source is one input that fires it, and a binding may have
 * several. So bindings are top-level rows and sources are their children, and each kind
 * edits exactly the fields it owns. Nothing has to be taken away for this -- the stock
 * array stays underneath for the long tail (pitch routing, note-off, send-source-number).
 *
 * Every write goes through an IPropertyHandle rather than the struct. Not fastidiousness:
 * a handle write runs PostEditChangeProperty, which is what calls UOscuMIDIMap::Refresh --
 * re-resolving note names, mirroring each binding's Type onto its sources, rebuilding the
 * lookup. Writing the member directly would look identical and behave stale.
 */
class FOscuMIDIMapDetails : public IDetailCustomization, public FSelfRegisteringEditorUndoClient
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

	/**
	 * Rebuilds the tree after an undo or redo.
	 *
	 * Rows hold indices, and the tree is only rebuilt when something changes the LENGTH
	 * of the bindings array -- which the array handle reports, but only for its own
	 * array. An undo that puts back or takes away a SOURCE changes a nested array that
	 * nothing is listening to, so without this the row count went stale: undoing an
	 * added input left its row on screen, drawing blanks because the index behind it no
	 * longer resolved.
	 *
	 * Self-registering, so the client is attached and detached with the customization
	 * rather than leaking a registration into GEditor when the panel closes.
	 */
	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override;

	/** One cell. Public because the row widget calls back into it per column. */
	TSharedRef<SWidget> GenerateCell(const FName& ColumnId, const TSharedPtr<FOscuMapRow>& Row);

private:
	// ---- Tree ----

	void RebuildRows();
	TSharedRef<ITableRow> OnGenerateRow(TSharedPtr<FOscuMapRow> Row, const TSharedRef<STableViewBase>& OwnerTable);
	void OnGetChildren(TSharedPtr<FOscuMapRow> Row, TArray<TSharedPtr<FOscuMapRow>>& OutChildren);

	/** Null when an index has gone stale, which is why every cell checks. */
	const FOscuMIDIBinding* GetBinding(int32 BindingIndex) const;
	const FOscuMIDISource* GetSource(int32 BindingIndex, int32 SourceIndex) const;

	// ---- Tag section cells ----

	/**
	 * A whole section header, spanning the row.
	 *
	 * Not cells in the binding columns: a tag is not a function, and putting it in the
	 * Function column made it read as one. A section header belongs across the row,
	 * with its own label saying what it is.
	 */
	TSharedRef<SWidget> MakeTagRow(const TSharedPtr<FOscuMapRow>& Row);

	/** Drops the section being added. Nothing in the asset to undo -- it never got there. */
	void DeletePendingTag();

	TSharedRef<SWidget> MakeTagNameCell(const TSharedPtr<FOscuMapRow>& Row);
	TSharedRef<SWidget> MakeTagSummaryCell(const TSharedPtr<FOscuMapRow>& Row) const;
	TSharedRef<SWidget> MakeTagActionsCell(FName Tag);

	/**
	 * Retags every binding in a section at once.
	 *
	 * The tag moved OUT of the rows when they were grouped -- repeating "cube" on forty
	 * rows inside a section headed "cube" was the single biggest piece of noise in the
	 * panel -- so this is where a tag is now edited. Renaming the section is also the
	 * edit people actually want: fixing a typo used to mean retyping it on every row.
	 *
	 * Moving ONE binding to a different tag is no longer possible here; the stock
	 * Bindings array below still does it, and it is rare enough to be worth the trade.
	 */
	void RenameTag(FName OldTag, FName NewTag);

	/** Opens an empty section for a tag you are about to name. Writes nothing yet. */
	void AddPendingTag();

	/**
	 * Lists everything the open level exposes under ONE tag, as bindings.
	 *
	 * Auto-Map From Level does this for every tag at once, which is the right tool when
	 * you already have a map and the wrong one when you are adding a single actor: it
	 * answers a question you did not ask about the rest of the level. This is the same
	 * machinery -- rescan, introspect, MergeFunctions -- aimed at one tag.
	 *
	 * Additive only, like Auto-Map: existing rows are never moved or rewritten, so
	 * scanning twice cannot reshuffle which pad triggers what.
	 */
	void ScanTag(FName Tag);

	/**
	 * A source row, built whole instead of column by column.
	 *
	 * Bindings and sources have nothing in common column-wise -- a channel is not a
	 * function and a device is not a parameter -- so sharing the columns forced every
	 * header to name two unrelated things ("Tag / Channel", "Type / Range"). A source
	 * spans the row instead, which lets every header say one thing.
	 */
	TSharedRef<SWidget> MakeSourceStrip(int32 BindingIndex, int32 SourceIndex);

	/** Adds a binding already carrying this section's tag, so one field is filled in. */
	void AddBindingForTag(FName Tag);

	void SetAllExpanded(bool bExpanded);

	// ---- Binding cells ----

	TSharedRef<SWidget> MakeStatusCell(int32 BindingIndex) const;
	TSharedRef<SWidget> MakeNameCell(int32 BindingIndex, FName Field) const;
	TSharedRef<SWidget> MakeTypeCell(int32 BindingIndex);

	/**
	 * One "which parameter does this drive" dropdown, for velocity or for pitch.
	 *
	 * Shared because the two differ only in which name they write and what the row is
	 * allowed to call them. What the message's VALUE is depends on the row's kind and
	 * the three are not the same thing -- velocity for a note, the controller value for
	 * a control change, the PROGRAM NUMBER for a program change -- so the caption is
	 * per row rather than in the column header, which can only ever say one of them.
	 */
	TSharedRef<SWidget> MakeParamCell(int32 BindingIndex, FName Field);

	TSharedRef<SWidget> MakeBindingActionsCell(int32 BindingIndex);

	// ---- Source cells ----

	TSharedRef<SWidget> MakeChannelCell(int32 BindingIndex, int32 SourceIndex);
	TSharedRef<SWidget> MakeInputCell(int32 BindingIndex, int32 SourceIndex);
	TSharedRef<SWidget> MakeDeviceCell(int32 BindingIndex, int32 SourceIndex);
	TSharedRef<SWidget> MakeResolvedCell(int32 BindingIndex, int32 SourceIndex) const;
	TSharedRef<SWidget> MakeSourceActionsCell(int32 BindingIndex, int32 SourceIndex);

	// ---- Handles ----

	TSharedPtr<IPropertyHandle> GetBindingField(int32 BindingIndex, FName Field) const;
	TSharedPtr<IPropertyHandleArray> GetSourcesArray(int32 BindingIndex) const;
	TSharedPtr<IPropertyHandle> GetSourceField(int32 BindingIndex, int32 SourceIndex, FName Field) const;

	// ---- Actions ----

	void AddBinding();
	void DeleteBinding(int32 BindingIndex);
	void AddSource(int32 BindingIndex);
	void DeleteSource(int32 BindingIndex, int32 SourceIndex);

	/**
	 * Arms one source for Learn, adding one first when the binding has none.
	 *
	 * SourceIndex INDEX_NONE means "this row's input", which is the gesture a binding
	 * straight out of Auto-Map needs: it has no source at all, and there is nothing to
	 * teach until it does.
	 */
	void ToggleLearn(int32 BindingIndex, int32 SourceIndex);
	bool IsArmed(int32 BindingIndex, int32 SourceIndex) const;

	/** The parameter names this binding's function offers, asked of the live level. */
	TArray<FName> GetParamNames(int32 BindingIndex) const;

	/** The editor's open level, the only level any of this can be true of. */
	static UWorld* EditorWorld();

	// ---- Sorting and filtering ----

	EColumnSortMode::Type GetSortMode(FName ColumnId) const;
	void OnSortColumn(EColumnSortPriority::Type Priority, const FName& ColumnId, EColumnSortMode::Type NewMode);
	void OnFilterChanged(const FText& NewText);

	TWeakObjectPtr<UOscuMIDIMap> Map;

	TSharedPtr<IPropertyHandle> BindingsHandle;
	TSharedPtr<IPropertyHandleArray> BindingsArray;

	TArray<TSharedPtr<FOscuMapRow>> Rows;
	TSharedPtr<STreeView<TSharedPtr<FOscuMapRow>>> TreeView;

	/** Kept alive for the Type combo, which holds a pointer to its options. */
	TArray<TSharedPtr<FString>> TypeOptions;

	/**
	 * The enum value behind each entry of TypeOptions, in the same order.
	 *
	 * Parallel rather than casting the index: Note Range is appended LAST in the enum so
	 * existing assets keep their serialised values, but it reads next to Single Note in
	 * the list. Those two orders are allowed to differ now.
	 */
	TArray<EOscuMIDIInputType> TypeValues;

	/**
	 * Sections the user has closed, by tag.
	 *
	 * Persisted across rebuilds because the row objects are recreated every time the
	 * tree is rebuilt -- a filter keystroke, an undo, an added input -- and expansion
	 * lives on the row object. Without this, closing a forty-function section reopened
	 * it on the next edit, which is the opposite of what closing it was for.
	 *
	 * Stored as COLLAPSED rather than expanded so a tag that has never been seen starts
	 * open: a section that hid itself on first sight would read as missing.
	 */
	TSet<FName> CollapsedTags;

	/**
	 * The section being added, before it has a name or any functions.
	 *
	 * One at a time, because adding an actor is a thing you finish before starting the
	 * next. Transient: nothing reaches the asset until Scan or "+ function" runs.
	 */
	TOptional<FName> PendingTag;

	FString Filter;
	FName SortColumn;
	EColumnSortMode::Type SortMode = EColumnSortMode::Ascending;
};
