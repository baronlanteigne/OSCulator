// Copyright Baron Lanteigne. All Rights Reserved.

#include "OscuMarshal.h"

#include "UObject/Class.h"
#include "UObject/UnrealType.h"

namespace
{
	/** Human-readable type name for a rejection message. */
	FString DescribePropertyType(const FProperty* Property)
	{
		if (const FStructProperty* AsStruct = CastField<FStructProperty>(Property))
		{
			return FString::Printf(TEXT("struct F%s"), AsStruct->Struct ? *AsStruct->Struct->GetName() : TEXT("<null>"));
		}
		if (const FObjectPropertyBase* AsObject = CastField<FObjectPropertyBase>(Property))
		{
			return FString::Printf(TEXT("object %s"), AsObject->PropertyClass ? *AsObject->PropertyClass->GetName() : TEXT("<null>"));
		}
		return Property->GetClass()->GetName();
	}

	FOscuParamClass MakeAccepted(int32 ArgCount, FString TypeLabel)
	{
		FOscuParamClass Result;
		Result.bMarshallable = true;
		Result.ArgCount = ArgCount;
		Result.TypeLabel = MoveTemp(TypeLabel);
		return Result;
	}

	FOscuParamClass MakeRejected(FString Reason)
	{
		FOscuParamClass Result;
		Result.bMarshallable = false;
		Result.RejectReason = MoveTemp(Reason);
		return Result;
	}

	/** An enum accepts either its index or one of its names. */
	int64 ResolveEnumValue(const UEnum* Enum, const FOscuValue& Value)
	{
		if (Enum != nullptr && Value.Type == EOscuValueType::String)
		{
			const int64 ByName = Enum->GetValueByNameString(Value.Str);
			if (ByName != INDEX_NONE)
			{
				return ByName;
			}
		}
		return static_cast<int64>(Value.AsNumber());
	}

	/**
	 * Writes one single-argument property at a raw value pointer.
	 *
	 * Every write goes through the FProperty rather than the raw pointer, because
	 * FBoolProperty may be a bitfield and cannot be assigned by memcpy.
	 */
	void SetScalarValue(const FProperty* Property, void* ValuePtr, const FOscuValue& Value)
	{
		// Blueprint "Float" pins are doubles in UE5, so this is the common case.
		if (const FDoubleProperty* AsDouble = CastField<FDoubleProperty>(Property))
		{
			AsDouble->SetPropertyValue(ValuePtr, Value.AsNumber());
			return;
		}
		if (const FFloatProperty* AsFloat = CastField<FFloatProperty>(Property))
		{
			AsFloat->SetPropertyValue(ValuePtr, static_cast<float>(Value.AsNumber()));
			return;
		}
		if (const FIntProperty* AsInt = CastField<FIntProperty>(Property))
		{
			// A C++ double-to-int conversion truncates toward zero, which is what
			// the type table promises.
			AsInt->SetPropertyValue(ValuePtr, static_cast<int32>(Value.AsNumber()));
			return;
		}
		if (const FInt64Property* AsInt64 = CastField<FInt64Property>(Property))
		{
			AsInt64->SetPropertyValue(ValuePtr, static_cast<int64>(Value.AsNumber()));
			return;
		}
		if (const FBoolProperty* AsBool = CastField<FBoolProperty>(Property))
		{
			AsBool->SetPropertyValue(ValuePtr, Value.AsNumber() != 0.0);
			return;
		}
		if (const FByteProperty* AsByte = CastField<FByteProperty>(Property))
		{
			const int64 Resolved = (AsByte->Enum != nullptr)
				? ResolveEnumValue(AsByte->Enum, Value)
				: static_cast<int64>(Value.AsNumber());
			AsByte->SetPropertyValue(ValuePtr, static_cast<uint8>(FMath::Clamp<int64>(Resolved, 0, 255)));
			return;
		}
		if (const FEnumProperty* AsEnum = CastField<FEnumProperty>(Property))
		{
			AsEnum->GetUnderlyingProperty()->SetIntPropertyValue(ValuePtr, ResolveEnumValue(AsEnum->GetEnum(), Value));
			return;
		}
		if (const FStrProperty* AsStr = CastField<FStrProperty>(Property))
		{
			AsStr->SetPropertyValue(ValuePtr, Value.AsString());
			return;
		}
		if (const FNameProperty* AsName = CastField<FNameProperty>(Property))
		{
			AsName->SetPropertyValue(ValuePtr, FName(*Value.AsString()));
			return;
		}
		if (const FTextProperty* AsText = CastField<FTextProperty>(Property))
		{
			AsText->SetPropertyValue(ValuePtr, FText::FromString(Value.AsString()));
			return;
		}
	}

	/** Zero past the end, so a partially-supplied struct under Lenient is sensibly
	 *  zeroed rather than reading off the message. */
	double ArgAt(const TArray<FOscuValue>& Args, int32 Index)
	{
		return Args.IsValidIndex(Index) ? Args[Index].AsNumber() : 0.0;
	}

	/** Reassembles a struct from consecutive numbers. This is the only place that
	 *  logic lives -- the codec deliberately knows nothing about it. */
	void SetStructValue(const UScriptStruct* Struct, void* ValuePtr, const TArray<FOscuValue>& Args, int32 Start)
	{
		if (Struct == TBaseStructure<FVector>::Get())
		{
			FVector& Out = *static_cast<FVector*>(ValuePtr);
			Out.X = ArgAt(Args, Start);
			Out.Y = ArgAt(Args, Start + 1);
			Out.Z = ArgAt(Args, Start + 2);
			return;
		}
		if (Struct == TBaseStructure<FVector2D>::Get())
		{
			FVector2D& Out = *static_cast<FVector2D*>(ValuePtr);
			Out.X = ArgAt(Args, Start);
			Out.Y = ArgAt(Args, Start + 1);
			return;
		}
		if (Struct == TBaseStructure<FRotator>::Get())
		{
			// Struct member order: Pitch, Yaw, Roll. Not X/Y/Z.
			FRotator& Out = *static_cast<FRotator*>(ValuePtr);
			Out.Pitch = ArgAt(Args, Start);
			Out.Yaw = ArgAt(Args, Start + 1);
			Out.Roll = ArgAt(Args, Start + 2);
			return;
		}
		if (Struct == TBaseStructure<FLinearColor>::Get())
		{
			// FLinearColor components are float, not double like the vector types.
			FLinearColor& Out = *static_cast<FLinearColor*>(ValuePtr);
			Out.R = static_cast<float>(ArgAt(Args, Start));
			Out.G = static_cast<float>(ArgAt(Args, Start + 1));
			Out.B = static_cast<float>(ArgAt(Args, Start + 2));
			Out.A = static_cast<float>(ArgAt(Args, Start + 3));
			return;
		}
		if (Struct == TBaseStructure<FQuat>::Get())
		{
			FQuat& Out = *static_cast<FQuat*>(ValuePtr);
			Out.X = ArgAt(Args, Start);
			Out.Y = ArgAt(Args, Start + 1);
			Out.Z = ArgAt(Args, Start + 2);
			Out.W = ArgAt(Args, Start + 3);
			return;
		}
		if (Struct == TBaseStructure<FTransform>::Get())
		{
			const FVector Location(ArgAt(Args, Start), ArgAt(Args, Start + 1), ArgAt(Args, Start + 2));
			const FRotator Rotation(ArgAt(Args, Start + 3), ArgAt(Args, Start + 4), ArgAt(Args, Start + 5));
			const FVector Scale(ArgAt(Args, Start + 6), ArgAt(Args, Start + 7), ArgAt(Args, Start + 8));
			*static_cast<FTransform*>(ValuePtr) = FTransform(Rotation, Location, Scale);
			return;
		}
	}
}

namespace OscuMarshal
{
	bool IsPureOutputParam(const FProperty* Property)
	{
		if (Property == nullptr)
		{
			return false;
		}

		// CPF_ReferenceParm means the caller's value is passed in as well as written
		// back, so a UPARAM(ref) or non-const C++ reference still takes an argument.
		return Property->HasAnyPropertyFlags(CPF_OutParm)
			&& !Property->HasAnyPropertyFlags(CPF_ReturnParm)
			&& !Property->HasAnyPropertyFlags(CPF_ReferenceParm);
	}

	FOscuParamClass ClassifyParam(const FProperty* Property)
	{
		if (Property == nullptr)
		{
			return MakeRejected(TEXT("null property"));
		}

		// Blueprint "Float" pins are double-precision in UE5, so FDoubleProperty is
		// the common case by a wide margin. It is tested first to make that obvious;
		// the two classes are siblings, so the order is documentation, not behaviour.
		if (CastField<FDoubleProperty>(Property))
		{
			return MakeAccepted(1, TEXT("float"));
		}
		if (CastField<FFloatProperty>(Property))
		{
			return MakeAccepted(1, TEXT("float"));
		}

		if (CastField<FIntProperty>(Property) || CastField<FInt64Property>(Property))
		{
			return MakeAccepted(1, TEXT("int"));
		}

		// Bitfields cannot be memcpy'd, so the fill side must go through
		// SetPropertyValue_InContainer. Classification does not care.
		if (CastField<FBoolProperty>(Property))
		{
			return MakeAccepted(1, TEXT("bool"));
		}

		if (const FByteProperty* AsByte = CastField<FByteProperty>(Property))
		{
			if (AsByte->Enum != nullptr)
			{
				return MakeAccepted(1, FString::Printf(TEXT("enum(%s)"), *AsByte->Enum->GetName()));
			}
			return MakeAccepted(1, TEXT("byte"));
		}

		if (const FEnumProperty* AsEnum = CastField<FEnumProperty>(Property))
		{
			const UEnum* Enum = AsEnum->GetEnum();
			return MakeAccepted(1, FString::Printf(TEXT("enum(%s)"), Enum ? *Enum->GetName() : TEXT("?")));
		}

		if (CastField<FStrProperty>(Property))
		{
			return MakeAccepted(1, TEXT("string"));
		}
		if (CastField<FNameProperty>(Property))
		{
			return MakeAccepted(1, TEXT("name"));
		}
		if (CastField<FTextProperty>(Property))
		{
			return MakeAccepted(1, TEXT("text"));
		}

		if (const FStructProperty* AsStruct = CastField<FStructProperty>(Property))
		{
			const UScriptStruct* Struct = AsStruct->Struct;

			if (Struct == TBaseStructure<FVector>::Get())
			{
				return MakeAccepted(3, TEXT("vec3"));
			}
			if (Struct == TBaseStructure<FVector2D>::Get())
			{
				return MakeAccepted(2, TEXT("vec2"));
			}
			if (Struct == TBaseStructure<FRotator>::Get())
			{
				// Consumed in struct member order, which is Pitch, Yaw, Roll. Anyone
				// arriving from TouchDesigner or Blender thinks in X/Y/Z, which reads
				// as Roll/Pitch/Yaw -- the opposite. The label spells the order out so
				// a sender never has to go read the source to find out.
				return MakeAccepted(3, TEXT("rot(pitch,yaw,roll)"));
			}
			if (Struct == TBaseStructure<FLinearColor>::Get())
			{
				return MakeAccepted(4, TEXT("color(r,g,b,a)"));
			}
			if (Struct == TBaseStructure<FQuat>::Get())
			{
				return MakeAccepted(4, TEXT("quat(x,y,z,w)"));
			}
			if (Struct == TBaseStructure<FTransform>::Get())
			{
				return MakeAccepted(9, TEXT("transform(loc3,rot(pitch,yaw,roll),scale3)"));
			}

			return MakeRejected(FString::Printf(TEXT("%s is not a supported struct"), *DescribePropertyType(Property)));
		}

		if (const FArrayProperty* AsArray = CastField<FArrayProperty>(Property))
		{
			// The caller enforces "must be the final parameter"; it needs the whole
			// parameter list to know that, and this function sees one at a time.
			const FOscuParamClass Inner = ClassifyParam(AsArray->Inner);
			if (!Inner.bMarshallable)
			{
				return MakeRejected(FString::Printf(TEXT("array element %s"), *Inner.RejectReason));
			}
			if (Inner.ArgCount != 1)
			{
				// An array of vec3 would be unpackable in principle, but "all remaining
				// arguments, in groups of three" is a rule senders get wrong silently.
				return MakeRejected(FString::Printf(TEXT("array of multi-argument element type '%s' is ambiguous"), *Inner.TypeLabel));
			}
			return MakeAccepted(OscuVariadicArgCount, FString::Printf(TEXT("array<%s>"), *Inner.TypeLabel));
		}

		return MakeRejected(FString::Printf(TEXT("%s is not marshallable"), *DescribePropertyType(Property)));
	}

	bool CheckArgCount(
		const FString& Address,
		const FOscuExposedFunctionInfo& Info,
		int32 SuppliedCount,
		EOscuArgPolicy Policy,
		FString& OutError)
	{
		// Lenient fills what it can and zeroes the rest, so no count can be wrong.
		if (Policy == EOscuArgPolicy::Lenient)
		{
			return true;
		}

		// Too few and too many are not equally wrong, so Strict does not treat them
		// alike. Too few means the function runs on values the sender never supplied
		// -- silently, since an unfilled parameter is simply zero. Too many means it
		// runs on exactly the values it asked for, with ignorable data trailing.
		//
		// Senders routinely append things: a TouchDesigner CHOP emits every channel
		// it has, and there is no way to make it emit none for a trigger. Rejecting
		// that would make zero-argument functions unreachable in practice, so the
		// surplus is discarded and the call goes ahead.
		if (SuppliedCount >= Info.TotalArgCount)
		{
			return true;
		}

		// The expected signature is generated from reflection data we already hold,
		// so the sender is told what to fix rather than just that something broke.
		OutError = FString::Printf(TEXT("%s expects %s%d arg%s (%s) -- got %d. Ignored."),
			*Address,
			Info.bVariadic ? TEXT("at least ") : TEXT(""),
			Info.TotalArgCount,
			(Info.TotalArgCount == 1) ? TEXT("") : TEXT("s"),
			*Info.GetSignatureString(),
			SuppliedCount);
		return false;
	}

	void FillFrame(const UFunction* Function, const TArray<FOscuValue>& Args, EOscuArgPolicy Policy, uint8* Frame)
	{
		int32 ArgIndex = 0;

		// TFieldIterator yields parameters in declaration order, which is exactly
		// the order the message supplies them in. That correspondence is the whole
		// design: the signature is the schema, and the sender declares no types.
		for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			const FProperty* Param = *It;
			if (Param->HasAnyPropertyFlags(CPF_ReturnParm))
			{
				continue;
			}
			if (IsPureOutputParam(Param))
			{
				// Written by the call, never read from the message.
				continue;
			}

			const FOscuParamClass Classified = ClassifyParam(Param);
			if (!Classified.bMarshallable)
			{
				// Registration excludes such functions outright, so reaching here
				// would mean the registry and the marshaller disagree.
				continue;
			}

			void* ValuePtr = Param->ContainerPtrToValuePtr<void>(Frame);

			if (Classified.ArgCount == OscuVariadicArgCount)
			{
				const FArrayProperty* AsArray = CastField<FArrayProperty>(Param);
				if (AsArray == nullptr)
				{
					continue;
				}

				const int32 Count = FMath::Max(0, Args.Num() - ArgIndex);
				FScriptArrayHelper Helper(AsArray, ValuePtr);
				Helper.Resize(Count);
				for (int32 Element = 0; Element < Count; ++Element)
				{
					SetScalarValue(AsArray->Inner, Helper.GetRawPtr(Element), Args[ArgIndex + Element]);
				}

				ArgIndex += Count;
				continue;
			}

			if (const FStructProperty* AsStruct = CastField<FStructProperty>(Param))
			{
				SetStructValue(AsStruct->Struct, ValuePtr, Args, ArgIndex);
			}
			else if (Args.IsValidIndex(ArgIndex))
			{
				SetScalarValue(Param, ValuePtr, Args[ArgIndex]);
			}
			// Otherwise we are in Lenient mode and have run out of arguments. The
			// zeroed, initialised frame already holds 0 and "", which is the whole
			// specified behaviour: Blueprint parameter defaults live in editor-only
			// metadata and are baked into the call node, so ProcessEvent never
			// applies them. An author who wants a MIDI-triggerable event should put
			// the velocity-relevant parameter first.

			ArgIndex += Classified.ArgCount;
		}
	}
}
