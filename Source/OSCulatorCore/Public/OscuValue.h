// Copyright Baron Lanteigne. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "OscuValue.generated.h"

/**
 * Wire value type.
 *
 * Float, Int and Bool all store their value in FOscuValue::Num. Keeping them in
 * a single double avoids a union and keeps the struct Blueprint-serializable.
 * Precision is not a concern: OSC floats and ints are both 32-bit and fit a
 * double exactly.
 *
 * Vector is SEND SIDE ONLY. It exists so a Blueprint author can wire an FVector
 * pin straight into a Send node and have it flatten to three floats on the wire.
 * The receive path never produces one -- incoming messages are flat, and it is
 * the marshaller's job (not the codec's) to reassemble structs from consecutive
 * numbers. Do not add vector reassembly to the codec.
 */
UENUM(BlueprintType)
enum class EOscuValueType : uint8
{
	Float,
	Int,
	Bool,
	String,
	Vector,
	Blob
};

/** One argument of an OSCulator message. */
USTRUCT(BlueprintType)
struct OSCULATORCORE_API FOscuValue
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OSCulator")
	EOscuValueType Type = EOscuValueType::Float;

	/** Holds the value for Float, Int and Bool. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OSCulator")
	double Num = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OSCulator")
	FString Str;

	/** Send side only. See EOscuValueType. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OSCulator")
	FVector Vec = FVector::ZeroVector;

	UPROPERTY()
	TArray<uint8> Blob;

	static FOscuValue MakeFloat(double In);
	static FOscuValue MakeInt(int64 In);
	static FOscuValue MakeBool(bool In);
	static FOscuValue MakeString(const FString& In);
	static FOscuValue MakeVector(const FVector& In);
	static FOscuValue MakeBlob(TArray<uint8> In);

	/**
	 * Permissive read as a number. A String parses itself; a Blob yields its
	 * byte count. Coercion never fails when there is an obvious answer -- Max
	 * sends ints where you expect floats constantly.
	 */
	double AsNumber() const;

	/** Permissive read as a string. Numerics format themselves. */
	FString AsString() const;

	bool AsBool() const { return AsNumber() != 0.0; }
	int64 AsInt() const { return static_cast<int64>(AsNumber()); }

	/** How many arguments this value occupies once serialized to the wire. */
	int32 WireArgCount() const { return Type == EOscuValueType::Vector ? 3 : 1; }

	bool operator==(const FOscuValue& Other) const;
	bool operator!=(const FOscuValue& Other) const { return !(*this == Other); }

	/** Short human-readable form, for log lines and introspection dumps. */
	FString ToLogString() const;
};

/** One addressed message. Flat argument list; the receiver's signature is the schema. */
USTRUCT(BlueprintType)
struct OSCULATORCORE_API FOscuMessage
{
	GENERATED_BODY()

	/** OSC address, e.g. "/laser/fire". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OSCulator")
	FString Address;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OSCulator")
	TArray<FOscuValue> Args;

	/** Receive side only, deliberately not reflected. */
	uint32 SourceIP = 0;

	/** Receive side only, deliberately not reflected. */
	double ReceiveTime = 0.0;

	/** Argument count as it appears on the wire, counting a Vector as three. */
	int32 NumWireArgs() const;

	FString ToLogString() const;
};
