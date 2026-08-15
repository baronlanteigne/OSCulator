// Copyright Baron Lanteigne. All Rights Reserved.

#include "OscuMaterialLibrary.h"

#include "OSCulatorCore.h"

void UOscuMaterialLibrary::ResolveMaterialParameterValue(
	const TArray<double>& Values,
	EOscuMaterialParamForm& OutForm,
	double& OutScalar,
	FLinearColor& OutColor)
{
	// Written on every path. An output pin left alone keeps whatever the previous
	// call put there, and a stale colour on the scalar branch is the kind of bug
	// that only shows up on the second message.
	OutScalar = 0.0;
	OutColor = FLinearColor::Black;

	const int32 Count = Values.Num();

	if (Count == 0)
	{
		UE_LOG(LogOSCulator, Warning,
			TEXT("Resolve Material Parameter Value: no values arrived, so there is nothing to set."));
		OutForm = EOscuMaterialParamForm::Invalid;
		return;
	}

	if (Count <= 2)
	{
		// Two is neither a scalar nor a colour. It is nearly always a sender with one
		// channel too many -- a CHOP carries every channel it has -- so the surplus is
		// dropped and the call goes ahead, which is what the argument-count check does
		// with a surplus everywhere else in the plugin.
		if (Count == 2)
		{
			UE_LOG(LogOSCulator, Warning,
				TEXT("Resolve Material Parameter Value: got 2 values, which is neither a scalar nor a colour. ")
				TEXT("Used the first as a scalar and ignored the rest."));
		}

		OutScalar = Values[0];
		OutForm = EOscuMaterialParamForm::Scalar;
		return;
	}

	if (Count > 4)
	{
		UE_LOG(LogOSCulator, Warning,
			TEXT("Resolve Material Parameter Value: got %d values. Used the first 4 as a colour and ignored the rest."),
			Count);
	}

	// FLinearColor components are float, not double like the array they come from.
	OutColor = FLinearColor(
		static_cast<float>(Values[0]),
		static_cast<float>(Values[1]),
		static_cast<float>(Values[2]),
		(Count >= 4) ? static_cast<float>(Values[3]) : 1.0f);
	OutForm = EOscuMaterialParamForm::Color;
}
