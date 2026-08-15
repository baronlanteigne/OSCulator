// Copyright Baron Lanteigne. All Rights Reserved.

#include "OscuOSCCodec.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

namespace OscuCodecTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

	/** Hand-builds wire bytes, so the tests exercise the parser against a source
	 *  other than our own serializer. Tags the serializer never emits -- h, d, N,
	 *  I -- are only reachable this way. */
	void AppendBE32(TArray<uint8>& Out, uint32 Value)
	{
		Out.Add(static_cast<uint8>(Value >> 24));
		Out.Add(static_cast<uint8>(Value >> 16));
		Out.Add(static_cast<uint8>(Value >> 8));
		Out.Add(static_cast<uint8>(Value));
	}

	void AppendBE64(TArray<uint8>& Out, uint64 Value)
	{
		for (int32 Shift = 56; Shift >= 0; Shift -= 8)
		{
			Out.Add(static_cast<uint8>(Value >> Shift));
		}
	}

	void AppendFloat32(TArray<uint8>& Out, float Value)
	{
		uint32 Bits = 0;
		FMemory::Memcpy(&Bits, &Value, sizeof(float));
		AppendBE32(Out, Bits);
	}

	void AppendFloat64(TArray<uint8>& Out, double Value)
	{
		uint64 Bits = 0;
		FMemory::Memcpy(&Bits, &Value, sizeof(double));
		AppendBE64(Out, Bits);
	}

	void AppendString(TArray<uint8>& Out, const ANSICHAR* Value)
	{
		const int32 ContentLen = FCStringAnsi::Strlen(Value);
		Out.Append(reinterpret_cast<const uint8*>(Value), ContentLen);
		for (int32 i = ContentLen; i < ((ContentLen + 4) & ~3); ++i)
		{
			Out.Add(0);
		}
	}

	/** TestEqual's generic template reports through ReportError, which has no
	 *  formatter for a scoped enum. Compare the underlying values instead. */
	int32 AsInt(EOscuValueType Type)
	{
		return static_cast<int32>(Type);
	}

	void AppendBlob(TArray<uint8>& Out, const TArray<uint8>& Value)
	{
		AppendBE32(Out, static_cast<uint32>(Value.Num()));
		Out.Append(Value);
		for (int32 i = Value.Num(); i < ((Value.Num() + 3) & ~3); ++i)
		{
			Out.Add(0);
		}
	}

	/** A well-formed single message using only tags the serializer also emits. */
	TArray<uint8> BuildSimpleMessage()
	{
		TArray<uint8> Bytes;
		AppendString(Bytes, "/laser/fire");
		AppendString(Bytes, ",ifs");
		AppendBE32(Bytes, static_cast<uint32>(7));
		AppendFloat32(Bytes, 0.5f);
		AppendString(Bytes, "burst");
		return Bytes;
	}
}

//////////////////////////////////////////////////////////////////////////
// Round trip

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOscuCodecRoundTripTest,
	"OSCulator.OSC.Codec.RoundTrip",
	OscuCodecTest::Flags)

bool FOscuCodecRoundTripTest::RunTest(const FString& Parameters)
{
	FOscuMessage Original;
	Original.Address = TEXT("/laser/fire");
	Original.Args.Add(FOscuValue::MakeFloat(0.5));                             // -> f
	Original.Args.Add(FOscuValue::MakeInt(42));                                // -> i
	Original.Args.Add(FOscuValue::MakeInt(8589934592LL));                      // -> h, too big for int32
	Original.Args.Add(FOscuValue::MakeBool(true));                             // -> T
	Original.Args.Add(FOscuValue::MakeBool(false));                            // -> F
	Original.Args.Add(FOscuValue::MakeString(TEXT("burst")));                  // -> s
	Original.Args.Add(FOscuValue::MakeVector(FVector(1.0, -2.5, 0.25)));       // -> fff
	Original.Args.Add(FOscuValue::MakeBlob({ 1, 2, 3, 4, 5 }));                // -> b

	TArray<uint8> FirstPass;
	if (!TestTrue(TEXT("Serialize succeeds"), OscuOSCCodec::Serialize(Original, FirstPass)))
	{
		return false;
	}
	TestEqual(TEXT("Serialized packet is a multiple of 4 bytes"), FirstPass.Num() % 4, 0);

	TArray<FOscuMessage> Parsed;
	FString Error;
	if (!TestTrue(FString::Printf(TEXT("Parse succeeds (%s)"), *Error), OscuOSCCodec::Parse(FirstPass.GetData(), FirstPass.Num(), Parsed, Error)))
	{
		AddError(FString::Printf(TEXT("Parse failed: %s"), *Error));
		return false;
	}

	if (!TestEqual(TEXT("One message parsed"), Parsed.Num(), 1))
	{
		return false;
	}

	const FOscuMessage& Result = Parsed[0];
	TestEqual(TEXT("Address survives"), Result.Address, Original.Address);

	// The Vector flattened to three floats on the wire, so the parsed message has
	// ten arguments where the original had eight. That asymmetry is by design.
	if (!TestEqual(TEXT("Arg count matches the wire count"), Result.Args.Num(), Original.NumWireArgs()))
	{
		return false;
	}

	TestEqual(TEXT("[0] float"), Result.Args[0].AsNumber(), 0.5);
	TestEqual(TEXT("[1] int32"), Result.Args[1].AsInt(), static_cast<int64>(42));
	TestEqual(TEXT("[2] int64"), Result.Args[2].AsInt(), static_cast<int64>(8589934592LL));
	TestTrue(TEXT("[3] true"), Result.Args[3].AsBool());
	TestFalse(TEXT("[4] false"), Result.Args[4].AsBool());
	TestEqual(TEXT("[5] string"), Result.Args[5].AsString(), FString(TEXT("burst")));
	TestEqual(TEXT("[6] vector.x"), Result.Args[6].AsNumber(), 1.0);
	TestEqual(TEXT("[7] vector.y"), Result.Args[7].AsNumber(), -2.5);
	TestEqual(TEXT("[8] vector.z"), Result.Args[8].AsNumber(), 0.25);
	TestEqual(TEXT("[9] blob type"), OscuCodecTest::AsInt(Result.Args[9].Type), OscuCodecTest::AsInt(EOscuValueType::Blob));
	TestTrue(TEXT("[9] blob contents"), Result.Args[9].Blob == TArray<uint8>({ 1, 2, 3, 4, 5 }));

	// The real assertion: re-serializing what we parsed must reproduce the exact
	// bytes. This is what makes the parser trustworthy -- it catches any padding,
	// endianness or tag-ordering mistake that a value-only comparison would miss.
	TArray<uint8> SecondPass;
	TestTrue(TEXT("Re-serialize succeeds"), OscuOSCCodec::Serialize(Result, SecondPass));
	TestTrue(TEXT("Round trip is byte-identical"), FirstPass == SecondPass);

	return true;
}

//////////////////////////////////////////////////////////////////////////
// Every supported type tag, including the ones the serializer never emits

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOscuCodecTypeTagsTest,
	"OSCulator.OSC.Codec.TypeTags",
	OscuCodecTest::Flags)

bool FOscuCodecTypeTagsTest::RunTest(const FString& Parameters)
{
	using namespace OscuCodecTest;

	TArray<uint8> Bytes;
	AppendString(Bytes, "/all");
	AppendString(Bytes, ",ifsbhdTFNI");
	AppendBE32(Bytes, static_cast<uint32>(42));                 // i
	AppendFloat32(Bytes, 0.5f);                                 // f
	AppendString(Bytes, "burst");                               // s
	AppendBlob(Bytes, { 1, 2, 3 });                             // b
	AppendBE64(Bytes, static_cast<uint64>(8589934592LL));       // h
	AppendFloat64(Bytes, 0.25);                                 // d
	// T, F, N and I carry no payload.

	TestEqual(TEXT("Hand-built packet is a multiple of 4 bytes"), Bytes.Num() % 4, 0);

	TArray<FOscuMessage> Parsed;
	FString Error;
	if (!TestTrue(TEXT("Parse succeeds"), OscuOSCCodec::Parse(Bytes.GetData(), Bytes.Num(), Parsed, Error)))
	{
		AddError(FString::Printf(TEXT("Parse failed: %s"), *Error));
		return false;
	}
	if (!TestEqual(TEXT("One message parsed"), Parsed.Num(), 1))
	{
		return false;
	}

	const FOscuMessage& Msg = Parsed[0];
	TestEqual(TEXT("Address"), Msg.Address, FString(TEXT("/all")));

	// Ten tags, nine arguments: 'I' (infinitum) carries no value and is skipped.
	if (!TestEqual(TEXT("Arg count -- 'I' produces no argument"), Msg.Args.Num(), 9))
	{
		return false;
	}

	TestEqual(TEXT("i -> Int"), AsInt(Msg.Args[0].Type), AsInt(EOscuValueType::Int));
	TestEqual(TEXT("i value"), Msg.Args[0].AsInt(), static_cast<int64>(42));

	TestEqual(TEXT("f -> Float"), AsInt(Msg.Args[1].Type), AsInt(EOscuValueType::Float));
	TestEqual(TEXT("f value"), Msg.Args[1].AsNumber(), 0.5);

	TestEqual(TEXT("s -> String"), AsInt(Msg.Args[2].Type), AsInt(EOscuValueType::String));
	TestEqual(TEXT("s value"), Msg.Args[2].Str, FString(TEXT("burst")));

	TestEqual(TEXT("b -> Blob"), AsInt(Msg.Args[3].Type), AsInt(EOscuValueType::Blob));
	TestTrue(TEXT("b value"), Msg.Args[3].Blob == TArray<uint8>({ 1, 2, 3 }));

	TestEqual(TEXT("h -> Int"), AsInt(Msg.Args[4].Type), AsInt(EOscuValueType::Int));
	TestEqual(TEXT("h value"), Msg.Args[4].AsInt(), static_cast<int64>(8589934592LL));

	TestEqual(TEXT("d -> Float"), AsInt(Msg.Args[5].Type), AsInt(EOscuValueType::Float));
	TestEqual(TEXT("d value"), Msg.Args[5].AsNumber(), 0.25);

	TestEqual(TEXT("T -> Bool"), AsInt(Msg.Args[6].Type), AsInt(EOscuValueType::Bool));
	TestTrue(TEXT("T value"), Msg.Args[6].AsBool());

	TestEqual(TEXT("F -> Bool"), AsInt(Msg.Args[7].Type), AsInt(EOscuValueType::Bool));
	TestFalse(TEXT("F value"), Msg.Args[7].AsBool());

	TestEqual(TEXT("N -> Float"), AsInt(Msg.Args[8].Type), AsInt(EOscuValueType::Float));
	TestEqual(TEXT("N value"), Msg.Args[8].AsNumber(), 0.0);

	return true;
}

//////////////////////////////////////////////////////////////////////////
// Truncation. This is the path reachable from the network.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOscuCodecTruncationTest,
	"OSCulator.OSC.Codec.Truncation",
	OscuCodecTest::Flags)

bool FOscuCodecTruncationTest::RunTest(const FString& Parameters)
{
	const TArray<uint8> Full = OscuCodecTest::BuildSimpleMessage();

	// No prefix may read past the buffer it was handed -- run under ASan to make
	// that bite. Almost all of them must also be rejected outright.
	//
	// The one exception is a cut landing exactly on the padded address boundary.
	// Since a bare address is a legal zero-argument message, that prefix is a
	// well-formed packet and there is no information left to tell it apart from
	// one. Accepted deliberately: over UDP a datagram arrives whole or not at all,
	// so the case is theoretical, and refusing it would make zero-argument
	// triggers unreachable from senders that omit the type tag string.
	int32 AcceptedPrefixes = 0;

	for (int32 Length = 0; Length < Full.Num(); ++Length)
	{
		TArray<FOscuMessage> Parsed;
		FString Error;

		if (OscuOSCCodec::Parse(Full.GetData(), Length, Parsed, Error))
		{
			++AcceptedPrefixes;

			// If it parsed, it must have parsed into something coherent.
			if (TestEqual(FString::Printf(TEXT("Prefix of %d bytes yields one message"), Length), Parsed.Num(), 1))
			{
				TestEqual(FString::Printf(TEXT("Prefix of %d bytes carries the address"), Length),
					Parsed[0].Address, FString(TEXT("/laser/fire")));
				TestEqual(FString::Printf(TEXT("Prefix of %d bytes has no arguments"), Length),
					Parsed[0].Args.Num(), 0);
			}
		}
		else
		{
			TestFalse(FString::Printf(TEXT("Rejection at %d bytes carries a reason"), Length), Error.IsEmpty());
		}
	}

	// Exactly one: the address boundary. Anything more means a payload prefix is
	// being accepted, which would be a genuine bounds failure.
	TestEqual(TEXT("Only the address-boundary prefix is accepted"), AcceptedPrefixes, 1);

	// The untruncated packet still has to work, or the loop above proves nothing.
	TArray<FOscuMessage> Parsed;
	FString Error;
	TestTrue(TEXT("The full packet parses"), OscuOSCCodec::Parse(Full.GetData(), Full.Num(), Parsed, Error));
	TestEqual(TEXT("The full packet yields one message"), Parsed.Num(), 1);

	// A null buffer is a caller bug, not a network condition, but it must not crash.
	TArray<FOscuMessage> FromNull;
	FString NullError;
	TestFalse(TEXT("Null buffer is rejected"), OscuOSCCodec::Parse(nullptr, 16, FromNull, NullError));

	return true;
}

//////////////////////////////////////////////////////////////////////////
// Malformed input that is the right length but the wrong shape

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOscuCodecRejectionTest,
	"OSCulator.OSC.Codec.Rejection",
	OscuCodecTest::Flags)

bool FOscuCodecRejectionTest::RunTest(const FString& Parameters)
{
	using namespace OscuCodecTest;

	auto ExpectRejected = [this](const TCHAR* What, const TArray<uint8>& Bytes)
	{
		TArray<FOscuMessage> Parsed;
		FString Error;
		const bool bParsed = OscuOSCCodec::Parse(Bytes.GetData(), Bytes.Num(), Parsed, Error);
		TestFalse(FString::Printf(TEXT("Rejected: %s"), What), bParsed);
	};

	{
		// An unknown tag has an unknown width, so the message cannot be recovered.
		TArray<uint8> Bytes;
		AppendString(Bytes, "/laser/fire");
		AppendString(Bytes, ",fr");
		AppendFloat32(Bytes, 1.0f);
		AppendBE32(Bytes, 0);
		ExpectRejected(TEXT("unsupported type tag 'r'"), Bytes);
	}

	{
		TArray<uint8> Bytes;
		AppendString(Bytes, "laser/fire");   // no leading slash
		AppendString(Bytes, ",f");
		AppendFloat32(Bytes, 1.0f);
		ExpectRejected(TEXT("address without a leading slash"), Bytes);
	}

	{
		TArray<uint8> Bytes;
		AppendString(Bytes, "/laser/fire");
		AppendString(Bytes, "f");            // type tag string missing its comma
		AppendFloat32(Bytes, 1.0f);
		ExpectRejected(TEXT("type tag string without a leading comma"), Bytes);
	}

	{
		TArray<uint8> Bytes = BuildSimpleMessage();
		Bytes.Add(0);   // 4n + 1 bytes
		ExpectRejected(TEXT("packet length not a multiple of 4"), Bytes);
	}

	{
		// A blob claiming more bytes than the packet holds.
		TArray<uint8> Bytes;
		AppendString(Bytes, "/laser/fire");
		AppendString(Bytes, ",b");
		AppendBE32(Bytes, 0x7FFFFFFF);
		ExpectRejected(TEXT("blob length beyond the end of the packet"), Bytes);
	}

	// Both spellings of a zero-argument message must parse. The canonical form has
	// a "," type tag string; the bare form omits it entirely. The tag string is
	// nominally required but historically optional, and real senders -- TouchDesigner
	// among them -- emit the bare form, so refusing it makes triggers unreachable.
	{
		TArray<uint8> Canonical;
		AppendString(Canonical, "/laser/stop");
		AppendString(Canonical, ",");

		TArray<FOscuMessage> Parsed;
		FString Error;
		TestTrue(TEXT("Zero-argument message with a ',' parses"),
			OscuOSCCodec::Parse(Canonical.GetData(), Canonical.Num(), Parsed, Error));
		if (Parsed.Num() == 1)
		{
			TestEqual(TEXT("...and has no args"), Parsed[0].Args.Num(), 0);
			TestEqual(TEXT("...with the right address"), Parsed[0].Address, FString(TEXT("/laser/stop")));
		}
	}

	{
		TArray<uint8> Bare;
		AppendString(Bare, "/laser/stop");

		TArray<FOscuMessage> Parsed;
		FString Error;
		TestTrue(TEXT("Bare address with no type tag string parses"),
			OscuOSCCodec::Parse(Bare.GetData(), Bare.Num(), Parsed, Error));
		if (Parsed.Num() == 1)
		{
			TestEqual(TEXT("...and has no args"), Parsed[0].Args.Num(), 0);
			TestEqual(TEXT("...with the right address"), Parsed[0].Address, FString(TEXT("/laser/stop")));
		}
	}

	return true;
}

//////////////////////////////////////////////////////////////////////////
// Bundles flatten; the timetag is read past and dropped

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOscuCodecBundleTest,
	"OSCulator.OSC.Codec.Bundle",
	OscuCodecTest::Flags)

bool FOscuCodecBundleTest::RunTest(const FString& Parameters)
{
	using namespace OscuCodecTest;

	TArray<uint8> ElementA;
	AppendString(ElementA, "/laser/fire");
	AppendString(ElementA, ",f");
	AppendFloat32(ElementA, 0.5f);

	TArray<uint8> ElementB;
	AppendString(ElementB, "/laser/stop");
	AppendString(ElementB, ",");

	TArray<uint8> Bundle;
	AppendString(Bundle, "#bundle");
	AppendBE64(Bundle, 1);            // timetag, ignored
	AppendBE32(Bundle, static_cast<uint32>(ElementA.Num()));
	Bundle.Append(ElementA);
	AppendBE32(Bundle, static_cast<uint32>(ElementB.Num()));
	Bundle.Append(ElementB);

	TArray<FOscuMessage> Parsed;
	FString Error;
	if (!TestTrue(TEXT("Bundle parses"), OscuOSCCodec::Parse(Bundle.GetData(), Bundle.Num(), Parsed, Error)))
	{
		AddError(FString::Printf(TEXT("Parse failed: %s"), *Error));
		return false;
	}

	if (!TestEqual(TEXT("Bundle flattens to two messages"), Parsed.Num(), 2))
	{
		return false;
	}
	TestEqual(TEXT("First message in order"), Parsed[0].Address, FString(TEXT("/laser/fire")));
	TestEqual(TEXT("First message arg"), Parsed[0].Args[0].AsNumber(), 0.5);
	TestEqual(TEXT("Second message in order"), Parsed[1].Address, FString(TEXT("/laser/stop")));
	TestEqual(TEXT("Second message has no args"), Parsed[1].Args.Num(), 0);

	// Truncating a bundle must never crash. Note it cannot always be *detected*:
	// a cut landing exactly on an element boundary is indistinguishable from a
	// shorter bundle, because OSC-over-UDP takes its length from the datagram.
	// So the assertion here is containment, not rejection.
	for (int32 Length = 0; Length < Bundle.Num(); ++Length)
	{
		TArray<FOscuMessage> Partial;
		FString PartialError;
		if (OscuOSCCodec::Parse(Bundle.GetData(), Length, Partial, PartialError))
		{
			if (Partial.Num() > Parsed.Num())
			{
				AddError(FString::Printf(TEXT("Truncated bundle of %d bytes produced %d messages, more than the whole bundle's %d."), Length, Partial.Num(), Parsed.Num()));
			}
			for (int32 i = 0; i < Partial.Num(); ++i)
			{
				TestEqual(FString::Printf(TEXT("Truncated bundle at %d bytes yields a prefix of the full result"), Length), Partial[i].Address, Parsed[i].Address);
			}
		}
	}

	{
		TArray<uint8> Bytes;
		AppendString(Bytes, "#bundle");
		AppendBE64(Bytes, 1);
		AppendBE32(Bytes, 0x7FFFFFFF);   // element larger than the packet
		TArray<FOscuMessage> Rejected;
		FString RejectError;
		TestFalse(TEXT("Oversized bundle element is rejected"), OscuOSCCodec::Parse(Bytes.GetData(), Bytes.Num(), Rejected, RejectError));
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
