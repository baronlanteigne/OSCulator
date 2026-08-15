// Copyright Baron Lanteigne. All Rights Reserved.

#include "OscuOSCCodec.h"
#include "Misc/StringBuilder.h"

namespace
{
	/** Bundles may nest. Nothing legitimate nests deeply; this stops a hostile packet. */
	constexpr int32 GMaxBundleDepth = 8;

	/**
	 * Bounds-checked big-endian cursor over one packet or one bundle element.
	 *
	 * Every reader is constructed on the start of the element it reads, so OSC's
	 * "pad to a multiple of 4" is always measured from the right origin.
	 */
	struct FOscReader
	{
		const uint8* Data = nullptr;
		int32 Len = 0;
		int32 Pos = 0;

		FOscReader(const uint8* InData, int32 InLen)
			: Data(InData)
			, Len(InLen)
		{
		}

		int32 Remaining() const { return Len - Pos; }

		bool Skip(int32 Count)
		{
			if (Count < 0 || Remaining() < Count)
			{
				return false;
			}
			Pos += Count;
			return true;
		}

		bool ReadUInt32(uint32& Out)
		{
			if (Remaining() < 4)
			{
				return false;
			}
			Out = (static_cast<uint32>(Data[Pos]) << 24)
				| (static_cast<uint32>(Data[Pos + 1]) << 16)
				| (static_cast<uint32>(Data[Pos + 2]) << 8)
				| (static_cast<uint32>(Data[Pos + 3]));
			Pos += 4;
			return true;
		}

		bool ReadUInt64(uint64& Out)
		{
			if (Remaining() < 8)
			{
				return false;
			}
			Out = 0;
			for (int32 i = 0; i < 8; ++i)
			{
				Out = (Out << 8) | static_cast<uint64>(Data[Pos + i]);
			}
			Pos += 8;
			return true;
		}

		bool ReadInt32(int32& Out)
		{
			uint32 Bits = 0;
			if (!ReadUInt32(Bits))
			{
				return false;
			}
			Out = static_cast<int32>(Bits);
			return true;
		}

		bool ReadInt64(int64& Out)
		{
			uint64 Bits = 0;
			if (!ReadUInt64(Bits))
			{
				return false;
			}
			Out = static_cast<int64>(Bits);
			return true;
		}

		bool ReadFloat32(float& Out)
		{
			uint32 Bits = 0;
			if (!ReadUInt32(Bits))
			{
				return false;
			}
			FMemory::Memcpy(&Out, &Bits, sizeof(float));
			return true;
		}

		bool ReadFloat64(double& Out)
		{
			uint64 Bits = 0;
			if (!ReadUInt64(Bits))
			{
				return false;
			}
			FMemory::Memcpy(&Out, &Bits, sizeof(double));
			return true;
		}

		/** OSC-string: content, one null, then 0-3 more nulls to a multiple of 4. */
		bool ReadString(FString& Out)
		{
			int32 End = Pos;
			while (End < Len && Data[End] != 0)
			{
				++End;
			}
			if (End >= Len)
			{
				// Ran off the end without finding a terminator.
				return false;
			}

			const int32 ContentLen = End - Pos;

			// Content plus at least one null, rounded up to a multiple of 4.
			const int32 PaddedLen = (ContentLen + 4) & ~3;
			if (Remaining() < PaddedLen)
			{
				return false;
			}

			Out = FString::ConstructFromPtrSize(reinterpret_cast<const UTF8CHAR*>(Data + Pos), ContentLen);
			Pos += PaddedLen;
			return true;
		}

		/** OSC-blob: int32 count, that many bytes, then 0-3 nulls to a multiple of 4. */
		bool ReadBlob(TArray<uint8>& Out)
		{
			int32 Count = 0;
			if (!ReadInt32(Count) || Count < 0)
			{
				return false;
			}

			// Bound Count against the buffer BEFORE rounding it up. For a Count near
			// MAX_int32 -- which a hostile or corrupt packet can simply declare --
			// (Count + 3) overflows to a negative, and a negative padded length
			// passes any "is it too long" test that comes after it.
			if (Remaining() < Count)
			{
				return false;
			}

			const int32 PaddedLen = (Count + 3) & ~3;
			if (Remaining() < PaddedLen)
			{
				return false;
			}

			Out.Append(Data + Pos, Count);
			Pos += PaddedLen;
			return true;
		}
	};

	void WriteUInt32BE(TArray<uint8>& Out, uint32 Value)
	{
		Out.Add(static_cast<uint8>(Value >> 24));
		Out.Add(static_cast<uint8>(Value >> 16));
		Out.Add(static_cast<uint8>(Value >> 8));
		Out.Add(static_cast<uint8>(Value));
	}

	void WriteUInt64BE(TArray<uint8>& Out, uint64 Value)
	{
		for (int32 Shift = 56; Shift >= 0; Shift -= 8)
		{
			Out.Add(static_cast<uint8>(Value >> Shift));
		}
	}

	void WriteFloat32BE(TArray<uint8>& Out, float Value)
	{
		uint32 Bits = 0;
		FMemory::Memcpy(&Bits, &Value, sizeof(float));
		WriteUInt32BE(Out, Bits);
	}

	void WritePadding(TArray<uint8>& Out, int32 Count)
	{
		for (int32 i = 0; i < Count; ++i)
		{
			Out.Add(0);
		}
	}

	void WriteOSCString(TArray<uint8>& Out, const FString& Value)
	{
		auto Converted = StringCast<UTF8CHAR>(*Value);
		const int32 ContentLen = Converted.Length();

		Out.Append(reinterpret_cast<const uint8*>(Converted.Get()), ContentLen);
		WritePadding(Out, ((ContentLen + 4) & ~3) - ContentLen);
	}

	void WriteOSCBlob(TArray<uint8>& Out, const TArray<uint8>& Value)
	{
		WriteUInt32BE(Out, static_cast<uint32>(Value.Num()));
		Out.Append(Value.GetData(), Value.Num());
		WritePadding(Out, ((Value.Num() + 3) & ~3) - Value.Num());
	}

	bool ParseMessage(const uint8* Data, int32 Len, FOscuMessage& Out, FString& OutError)
	{
		FOscReader Reader(Data, Len);

		if (!Reader.ReadString(Out.Address))
		{
			OutError = TEXT("truncated or unterminated address");
			return false;
		}
		if (Out.Address.IsEmpty() || Out.Address[0] != TEXT('/'))
		{
			OutError = FString::Printf(TEXT("address '%s' does not begin with '/'"), *Out.Address);
			return false;
		}

		// A bare address with no type tag string is a zero-argument message. The tag
		// string is nominally required, but it is historically optional and real
		// senders omit it -- TouchDesigner's OSC Out does, so refusing this makes
		// zero-argument triggers unreachable from a mainstream tool.
		//
		// The cost is that a packet truncated exactly at the address boundary now
		// looks like a valid trigger. Over UDP that is not a real case: datagrams
		// arrive whole or not at all.
		if (Reader.Remaining() == 0)
		{
			return true;
		}

		FString TypeTags;
		if (!Reader.ReadString(TypeTags))
		{
			OutError = FString::Printf(TEXT("%s: truncated type tag string"), *Out.Address);
			return false;
		}
		if (TypeTags.IsEmpty() || TypeTags[0] != TEXT(','))
		{
			OutError = FString::Printf(TEXT("%s: type tag string '%s' does not begin with ','"), *Out.Address, *TypeTags);
			return false;
		}

		Out.Args.Reserve(TypeTags.Len() - 1);

		for (int32 TagIndex = 1; TagIndex < TypeTags.Len(); ++TagIndex)
		{
			const TCHAR Tag = TypeTags[TagIndex];
			bool bRead = true;

			switch (Tag)
			{
			case TEXT('i'):
			{
				int32 Value = 0;
				bRead = Reader.ReadInt32(Value);
				if (bRead) { Out.Args.Add(FOscuValue::MakeInt(Value)); }
				break;
			}

			case TEXT('h'):
			{
				int64 Value = 0;
				bRead = Reader.ReadInt64(Value);
				if (bRead) { Out.Args.Add(FOscuValue::MakeInt(Value)); }
				break;
			}

			case TEXT('f'):
			{
				float Value = 0.0f;
				bRead = Reader.ReadFloat32(Value);
				if (bRead) { Out.Args.Add(FOscuValue::MakeFloat(Value)); }
				break;
			}

			case TEXT('d'):
			{
				double Value = 0.0;
				bRead = Reader.ReadFloat64(Value);
				if (bRead) { Out.Args.Add(FOscuValue::MakeFloat(Value)); }
				break;
			}

			case TEXT('s'):
			{
				FString Value;
				bRead = Reader.ReadString(Value);
				if (bRead) { Out.Args.Add(FOscuValue::MakeString(Value)); }
				break;
			}

			case TEXT('b'):
			{
				TArray<uint8> Value;
				bRead = Reader.ReadBlob(Value);
				if (bRead) { Out.Args.Add(FOscuValue::MakeBlob(MoveTemp(Value))); }
				break;
			}

			// The payload-less tags. Nothing to read, so nothing to bounds check.
			case TEXT('T'):
				Out.Args.Add(FOscuValue::MakeBool(true));
				break;

			case TEXT('F'):
				Out.Args.Add(FOscuValue::MakeBool(false));
				break;

			case TEXT('N'):
				Out.Args.Add(FOscuValue::MakeFloat(0.0));
				break;

			case TEXT('I'):
				// Infinitum carries no value and has no sensible numeric form. Skipped.
				break;

			default:
				OutError = FString::Printf(TEXT("%s: unsupported type tag '%c' at position %d"), *Out.Address, Tag, TagIndex);
				return false;
			}

			if (!bRead)
			{
				OutError = FString::Printf(TEXT("%s: truncated payload for type tag '%c' at position %d"), *Out.Address, Tag, TagIndex);
				return false;
			}
		}

		return true;
	}

	bool ParsePacket(const uint8* Data, int32 Len, TArray<FOscuMessage>& OutMessages, FString& OutError, int32 Depth)
	{
		if (Len < 4)
		{
			OutError = FString::Printf(TEXT("packet too short (%d bytes)"), Len);
			return false;
		}
		if ((Len & 3) != 0)
		{
			// Every OSC packet and bundle element is a multiple of four bytes.
			// A packet that is not is truncated or corrupt.
			OutError = FString::Printf(TEXT("packet length %d is not a multiple of 4"), Len);
			return false;
		}

		if (Data[0] != '#')
		{
			FOscuMessage Message;
			if (!ParseMessage(Data, Len, Message, OutError))
			{
				return false;
			}
			OutMessages.Add(MoveTemp(Message));
			return true;
		}

		if (Depth >= GMaxBundleDepth)
		{
			OutError = FString::Printf(TEXT("bundle nested deeper than %d levels"), GMaxBundleDepth);
			return false;
		}

		FOscReader Reader(Data, Len);

		FString BundleTag;
		if (!Reader.ReadString(BundleTag) || BundleTag != TEXT("#bundle"))
		{
			OutError = TEXT("packet begins with '#' but is not a '#bundle'");
			return false;
		}

		// Timetag. Read past it -- scheduling is out of scope by design.
		if (!Reader.Skip(8))
		{
			OutError = TEXT("bundle is missing its timetag");
			return false;
		}

		while (Reader.Remaining() > 0)
		{
			int32 ElementSize = 0;
			if (!Reader.ReadInt32(ElementSize))
			{
				OutError = TEXT("truncated bundle element size");
				return false;
			}
			if (ElementSize < 0 || ElementSize > Reader.Remaining())
			{
				OutError = FString::Printf(TEXT("bundle element size %d exceeds the %d bytes remaining"), ElementSize, Reader.Remaining());
				return false;
			}

			if (!ParsePacket(Data + Reader.Pos, ElementSize, OutMessages, OutError, Depth + 1))
			{
				return false;
			}

			Reader.Pos += ElementSize;
		}

		return true;
	}
}

namespace OscuOSCCodec
{
	bool Parse(const uint8* Data, int32 Len, TArray<FOscuMessage>& OutMessages, FString& OutError)
	{
		OutError.Reset();

		if (Data == nullptr)
		{
			OutError = TEXT("null buffer");
			return false;
		}

		// Every length calculation below rounds up to a multiple of four, so a Len
		// within a few bytes of MAX_int32 would overflow before it could be checked.
		// No datagram is anywhere near this; the guard exists so the arithmetic
		// inside the reader is provably safe rather than merely unlikely to break.
		if (Len < 0 || Len > MAX_int32 - 8)
		{
			OutError = FString::Printf(TEXT("implausible buffer length %d"), Len);
			return false;
		}

		return ParsePacket(Data, Len, OutMessages, OutError, 0);
	}

	bool Serialize(const FOscuMessage& Msg, TArray<uint8>& OutBytes)
	{
		OutBytes.Reset();

		if (Msg.Address.IsEmpty() || Msg.Address[0] != TEXT('/'))
		{
			return false;
		}

		WriteOSCString(OutBytes, Msg.Address);

		// Build the whole tag string first -- it has to precede every payload.
		TStringBuilder<64> TypeTags;
		TypeTags.Append(TEXT(","));

		for (const FOscuValue& Value : Msg.Args)
		{
			switch (Value.Type)
			{
			case EOscuValueType::Float:
				TypeTags.Append(TEXT("f"));
				break;

			case EOscuValueType::Int:
			{
				const int64 AsInt = static_cast<int64>(Value.Num);
				const bool bFitsInt32 = AsInt >= static_cast<int64>(MIN_int32) && AsInt <= static_cast<int64>(MAX_int32);
				TypeTags.Append(bFitsInt32 ? TEXT("i") : TEXT("h"));
				break;
			}

			case EOscuValueType::Bool:
				TypeTags.Append((Value.Num != 0.0) ? TEXT("T") : TEXT("F"));
				break;

			case EOscuValueType::String:
				TypeTags.Append(TEXT("s"));
				break;

			case EOscuValueType::Vector:
				// Flattened to three floats. This is the whole reason the Vector
				// type exists, and it exists on the send side only.
				TypeTags.Append(TEXT("fff"));
				break;

			case EOscuValueType::Blob:
				TypeTags.Append(TEXT("b"));
				break;

			default:
				return false;
			}
		}

		WriteOSCString(OutBytes, FString(TypeTags.ToString()));

		for (const FOscuValue& Value : Msg.Args)
		{
			switch (Value.Type)
			{
			case EOscuValueType::Float:
				WriteFloat32BE(OutBytes, static_cast<float>(Value.Num));
				break;

			case EOscuValueType::Int:
			{
				const int64 AsInt = static_cast<int64>(Value.Num);
				if (AsInt >= static_cast<int64>(MIN_int32) && AsInt <= static_cast<int64>(MAX_int32))
				{
					WriteUInt32BE(OutBytes, static_cast<uint32>(static_cast<int32>(AsInt)));
				}
				else
				{
					WriteUInt64BE(OutBytes, static_cast<uint64>(AsInt));
				}
				break;
			}

			case EOscuValueType::Bool:
				// T and F carry their value in the tag. No payload.
				break;

			case EOscuValueType::String:
				WriteOSCString(OutBytes, Value.Str);
				break;

			case EOscuValueType::Vector:
				WriteFloat32BE(OutBytes, static_cast<float>(Value.Vec.X));
				WriteFloat32BE(OutBytes, static_cast<float>(Value.Vec.Y));
				WriteFloat32BE(OutBytes, static_cast<float>(Value.Vec.Z));
				break;

			case EOscuValueType::Blob:
				WriteOSCBlob(OutBytes, Value.Blob);
				break;

			default:
				return false;
			}
		}

		return true;
	}
}
