// Copyright Baron Lanteigne. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IPropertyTypeCustomization.h"

class IPropertyHandle;

/**
 * One Player Menu > Controls row in Project Settings: the Tag and Function fields get
 * a picker filled from the open level, so the creator chooses from what is really
 * tagged rather than typing names that might not match.
 */
class FOscuPlayerMenuEntryCustomization : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow,
		IPropertyTypeCustomizationUtils& CustomizationUtils) override;

	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder,
		IPropertyTypeCustomizationUtils& CustomizationUtils) override;

private:
	TSharedRef<SWidget> MakeTagMenu();
	TSharedRef<SWidget> MakeFunctionMenu();
	TSharedRef<SWidget> MakeDispatcherMenu(FName Tag);

	/** Customizing an FOscuEventOutput (a dispatcher) rather than an FOscuPlayerMenuEntry. */
	bool bOutput = false;

	TSharedPtr<IPropertyHandle> TagHandle;
	TSharedPtr<IPropertyHandle> FunctionHandle;
	TSharedPtr<IPropertyHandle> LabelHandle;
};
