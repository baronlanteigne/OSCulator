// Copyright Baron Lanteigne. All Rights Reserved.

#include "OscuValue.h"
#include "Misc/StringBuilder.h"

FOscuValue FOscuValue::MakeFloat(double In)
{
	FOscuValue V;
	V.Type = EOscuValueType::Float;
	V.Num = In;
	return V;
}

FOscuValue FOscuValue::MakeInt(int64 In)
{
	FOscuValue V;
	V.Type = EOscuValueType::Int;
	V.Num = static_cast<double>(In);
	return V;
}

FOscuValue FOscuValue::MakeBool(bool In)
{
	FOscuValue V;
	V.Type = EOscuValueType::Bool;
	V.Num = In ? 1.0 : 0.0;
	return V;
}

FOscuValue FOscuValue::MakeString(const FString& In)
{
	FOscuValue V;
	V.Type = EOscuValueType::String;
	V.Str = In;
	return V;
}

FOscuValue FOscuValue::MakeVector(const FVector& In)
{
	FOscuValue V;
	V.Type = EOscuValueType::Vector;
	V.Vec = In;
	return V;
}

FOscuValue FOscuValue::MakeBlob(TArray<uint8> In)
{
	FOscuValue V;
	V.Type = EOscuValueType::Blob;
	V.Blob = MoveTemp(In);
	return V;
}

double FOscuValue::AsNumber() const
{
	switch (Type)
	{
	case EOscuValueType::String:
	{
		const FString Trimmed = Str.TrimStartAndEnd();
		if (Trimmed.IsEmpty())
		{
			return 0.0;
		}
		// Senders that spell booleans out as words are common enough to be worth handling.
		if (Trimmed.Equals(TEXT("true"), ESearchCase::IgnoreCase)
			|| Trimmed.Equals(TEXT("on"), ESearchCase::IgnoreCase)
			|| Trimmed.Equals(TEXT("yes"), ESearchCase::IgnoreCase))
		{
			return 1.0;
		}
		if (Trimmed.Equals(TEXT("false"), ESearchCase::IgnoreCase)
			|| Trimmed.Equals(TEXT("off"), ESearchCase::IgnoreCase)
			|| Trimmed.Equals(TEXT("no"), ESearchCase::IgnoreCase))
		{
			return 0.0;
		}
		return FCString::Atod(*Trimmed);
	}

	// Degenerate but defined. Vector is send side only, so this is only reachable
	// if a Blueprint author reads a value back out of a message it built itself.
	case EOscuValueType::Vector:
		return Vec.X;

	case EOscuValueType::Blob:
		return static_cast<double>(Blob.Num());

	default:
		return Num;
	}
}

FString FOscuValue::AsString() const
{
	switch (Type)
	{
	case EOscuValueType::String:
		return Str;

	case EOscuValueType::Int:
		return LexToString(static_cast<int64>(Num));

	case EOscuValueType::Bool:
		return (Num != 0.0) ? TEXT("true") : TEXT("false");

	case EOscuValueType::Vector:
		return FString::Printf(TEXT("%g %g %g"), Vec.X, Vec.Y, Vec.Z);

	case EOscuValueType::Blob:
		return FString::Printf(TEXT("<blob %d bytes>"), Blob.Num());

	case EOscuValueType::Float:
	default:
		return FString::SanitizeFloat(Num);
	}
}

bool FOscuValue::operator==(const FOscuValue& Other) const
{
	if (Type != Other.Type)
	{
		return false;
	}

	switch (Type)
	{
	case EOscuValueType::String: return Str == Other.Str;
	case EOscuValueType::Vector: return Vec == Other.Vec;
	case EOscuValueType::Blob:   return Blob == Other.Blob;
	default:                     return Num == Other.Num;
	}
}

FString FOscuValue::ToLogString() const
{
	switch (Type)
	{
	case EOscuValueType::String: return FString::Printf(TEXT("\"%s\""), *Str);
	case EOscuValueType::Vector: return FString::Printf(TEXT("(%g, %g, %g)"), Vec.X, Vec.Y, Vec.Z);
	case EOscuValueType::Blob:   return FString::Printf(TEXT("<blob %d>"), Blob.Num());
	case EOscuValueType::Int:    return LexToString(static_cast<int64>(Num));
	case EOscuValueType::Bool:   return (Num != 0.0) ? TEXT("true") : TEXT("false");
	case EOscuValueType::Float:
	default:                     return FString::Printf(TEXT("%g"), Num);
	}
}

int32 FOscuMessage::NumWireArgs() const
{
	int32 Total = 0;
	for (const FOscuValue& V : Args)
	{
		Total += V.WireArgCount();
	}
	return Total;
}

FString FOscuMessage::ToLogString() const
{
	TStringBuilder<256> Builder;
	Builder.Append(Address);
	for (const FOscuValue& V : Args)
	{
		Builder.Append(TEXT(" "));
		Builder.Append(V.ToLogString());
	}
	return FString(Builder.ToString());
}
