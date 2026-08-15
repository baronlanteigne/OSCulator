// Copyright Baron Lanteigne. All Rights Reserved.

#include "OscuOSCReceiver.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "OscuOSCCodec.h"
#include "OscuOSCSender.h"
#include "OscuOSCSubsystem.h"

#include "Common/UdpSocketBuilder.h"
#include "IPAddress.h"
#include "Misc/AutomationTest.h"
#include "SocketSubsystem.h"
#include "Sockets.h"

namespace OscuNetTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

	/** Sends one datagram to loopback and cleans itself up. */
	struct FScopedSender
	{
		FSocket* Socket = nullptr;

		FScopedSender()
		{
			Socket = FUdpSocketBuilder(TEXT("OSCulator Test Sender")).AsReusable().Build();
		}

		~FScopedSender()
		{
			if (Socket != nullptr)
			{
				Socket->Close();
				if (ISocketSubsystem* Subsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM))
				{
					Subsystem->DestroySocket(Socket);
				}
			}
		}

		bool SendTo(int32 Port, const TArray<uint8>& Bytes)
		{
			if (Socket == nullptr)
			{
				return false;
			}
			ISocketSubsystem* Subsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
			const TSharedRef<FInternetAddr> Destination = Subsystem->CreateInternetAddr();
			Destination->SetIp(0x7F000001);   // 127.0.0.1
			Destination->SetPort(Port);

			int32 Sent = 0;
			return Socket->SendTo(Bytes.GetData(), Bytes.Num(), Sent, *Destination) && Sent == Bytes.Num();
		}
	};

	/** Waits up to TimeoutSeconds for a message to cross from the receive thread. */
	bool WaitForMessage(FOscuOSCReceiver& Receiver, FOscuMessage& Out, double TimeoutSeconds = 5.0)
	{
		const double Deadline = FPlatformTime::Seconds() + TimeoutSeconds;
		while (FPlatformTime::Seconds() < Deadline)
		{
			if (Receiver.Dequeue(Out))
			{
				return true;
			}
			FPlatformProcess::Sleep(0.005f);
		}
		return false;
	}

	FOscuMessage MakeFireMessage()
	{
		FOscuMessage Message;
		Message.Address = TEXT("/laser/Fire");
		Message.Args.Add(FOscuValue::MakeFloat(0.0));
		Message.Args.Add(FOscuValue::MakeFloat(0.0));
		Message.Args.Add(FOscuValue::MakeFloat(1.0));
		Message.Args.Add(FOscuValue::MakeString(TEXT("burst")));
		Message.Args.Add(FOscuValue::MakeFloat(0.5));
		return Message;
	}
}

//////////////////////////////////////////////////////////////////////////
// A real datagram over a real socket, onto the SPSC queue

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOscuNetLoopbackTest,
	"OSCulator.OSC.Net.Loopback",
	OscuNetTest::Flags)

bool FOscuNetLoopbackTest::RunTest(const FString& Parameters)
{
	using namespace OscuNetTest;

	FOscuOSCReceiver Receiver;

	// Port 0 lets the OS pick a free one, so the test cannot collide with a real
	// sender or with a previous run that has not fully released 8000 yet.
	if (!TestTrue(TEXT("Receiver bound to loopback"), Receiver.Start(TEXT("127.0.0.1"), 0, {}, 1 << 16)))
	{
		return false;
	}

	const int32 Port = Receiver.GetBoundPort();
	TestTrue(TEXT("A real port was assigned"), Port > 0);

	TArray<uint8> Bytes;
	if (!TestTrue(TEXT("Serialize succeeded"), OscuOSCCodec::Serialize(MakeFireMessage(), Bytes)))
	{
		return false;
	}

	FScopedSender Sender;
	if (!TestTrue(TEXT("Datagram sent"), Sender.SendTo(Port, Bytes)))
	{
		return false;
	}

	FOscuMessage Received;
	if (!TestTrue(TEXT("Message crossed to the game thread within the timeout"), WaitForMessage(Receiver, Received)))
	{
		return false;
	}

	TestEqual(TEXT("Address survived the wire"), Received.Address, FString(TEXT("/laser/Fire")));
	if (TestEqual(TEXT("Argument count"), Received.Args.Num(), 5))
	{
		TestEqual(TEXT("Vector z"), Received.Args[2].AsNumber(), 1.0);
		TestEqual(TEXT("String argument"), Received.Args[3].AsString(), FString(TEXT("burst")));
		TestEqual(TEXT("Trailing float"), Received.Args[4].AsNumber(), 0.5);
	}

	TestEqual(TEXT("One packet counted"), Received.SourceIP, static_cast<uint32>(0x7F000001));
	TestTrue(TEXT("Receive time was stamped"), Received.ReceiveTime > 0.0);
	TestEqual(TEXT("Packet counter"), Receiver.GetPacketsReceived(), static_cast<uint64>(1));
	TestEqual(TEXT("Nothing was rejected"), Receiver.GetPacketsRejected(), static_cast<uint64>(0));

	// Shutdown must join the thread and close the socket without hanging. The
	// destructor does it too; calling it early proves it is idempotent.
	Receiver.Shutdown();
	TestFalse(TEXT("No longer listening after shutdown"), Receiver.IsListening());

	return true;
}

//////////////////////////////////////////////////////////////////////////
// The sender allowlist

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOscuNetAllowlistTest,
	"OSCulator.OSC.Net.Allowlist",
	OscuNetTest::Flags)

bool FOscuNetAllowlistTest::RunTest(const FString& Parameters)
{
	using namespace OscuNetTest;

	FOscuOSCReceiver Receiver;

	// Loopback is deliberately NOT on the list, so our own packet is the wrong
	// machine as far as the receiver is concerned.
	const TArray<FString> Allowed = { TEXT("10.99.99.99") };
	if (!TestTrue(TEXT("Receiver bound"), Receiver.Start(TEXT("127.0.0.1"), 0, Allowed, 1 << 16)))
	{
		return false;
	}

	TArray<uint8> Bytes;
	OscuOSCCodec::Serialize(MakeFireMessage(), Bytes);

	FScopedSender Sender;
	if (!TestTrue(TEXT("Datagram sent"), Sender.SendTo(Receiver.GetBoundPort(), Bytes)))
	{
		return false;
	}

	// Give the receive thread a real chance to have wrongly enqueued it.
	FOscuMessage Received;
	TestFalse(TEXT("Blocked sender's message never reaches the queue"), WaitForMessage(Receiver, Received, 1.0));

	TestEqual(TEXT("The packet did arrive at the socket"), Receiver.GetPacketsReceived(), static_cast<uint64>(1));
	TestEqual(TEXT("...and was dropped as a blocked sender"), Receiver.GetPacketsFromBlockedSenders(), static_cast<uint64>(1));
	TestEqual(TEXT("...not as malformed"), Receiver.GetPacketsRejected(), static_cast<uint64>(0));

	return true;
}

//////////////////////////////////////////////////////////////////////////
// Malformed traffic is dropped without disturbing the queue

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOscuNetGarbageTest,
	"OSCulator.OSC.Net.Garbage",
	OscuNetTest::Flags)

bool FOscuNetGarbageTest::RunTest(const FString& Parameters)
{
	using namespace OscuNetTest;

	FOscuOSCReceiver Receiver;
	if (!TestTrue(TEXT("Receiver bound"), Receiver.Start(TEXT("127.0.0.1"), 0, {}, 1 << 16)))
	{
		return false;
	}

	FScopedSender Sender;

	// Not OSC at all, and not even a multiple of four bytes.
	const TArray<uint8> Garbage = { 0xDE, 0xAD, 0xBE, 0xEF, 0x01 };
	TestTrue(TEXT("Garbage sent"), Sender.SendTo(Receiver.GetBoundPort(), Garbage));

	FOscuMessage Received;
	TestFalse(TEXT("Nothing was enqueued from garbage"), WaitForMessage(Receiver, Received, 1.0));
	TestEqual(TEXT("It was counted as malformed"), Receiver.GetPacketsRejected(), static_cast<uint64>(1));

	// A good packet after a bad one must still get through -- one malformed
	// datagram must not wedge the receive loop.
	TArray<uint8> Good;
	OscuOSCCodec::Serialize(MakeFireMessage(), Good);
	TestTrue(TEXT("Good packet sent"), Sender.SendTo(Receiver.GetBoundPort(), Good));
	TestTrue(TEXT("The receiver recovered"), WaitForMessage(Receiver, Received));
	TestEqual(TEXT("And delivered it intact"), Received.Address, FString(TEXT("/laser/Fire")));

	return true;
}

//////////////////////////////////////////////////////////////////////////
// Output: our own sender, over a real socket, into our own receiver

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOscuNetSendReceiveTest,
	"OSCulator.OSC.Net.SendReceive",
	OscuNetTest::Flags)

bool FOscuNetSendReceiveTest::RunTest(const FString& Parameters)
{
	using namespace OscuNetTest;

	FOscuOSCReceiver Receiver;
	if (!TestTrue(TEXT("Receiver bound"), Receiver.Start(TEXT("127.0.0.1"), 0, {}, 1 << 16)))
	{
		return false;
	}

	FOscuOSCSender Sender;
	if (!TestTrue(TEXT("Sender opened"), Sender.Open(FName("test"), TEXT("127.0.0.1"), Receiver.GetBoundPort())))
	{
		return false;
	}
	TestTrue(TEXT("Sender reports ready"), Sender.IsReady());

	// The wire half of the Phase 7 acceptance criterion: a float, a float and an
	// FVector go in as three arguments and come out as five values, because the
	// vector flattens on serialisation.
	FOscuMessage Message;
	Message.Address = TEXT("/laser/Aim");
	Message.Args.Add(FOscuValue::MakeFloat(0.25));
	Message.Args.Add(FOscuValue::MakeFloat(0.5));
	Message.Args.Add(FOscuValue::MakeVector(FVector(1.0, -2.0, 3.0)));

	TestEqual(TEXT("Three arguments become five on the wire"), Message.NumWireArgs(), 5);

	if (!TestTrue(TEXT("Message sent"), Sender.Send(Message)))
	{
		return false;
	}

	FOscuMessage Received;
	if (!TestTrue(TEXT("It arrived"), WaitForMessage(Receiver, Received)))
	{
		return false;
	}

	TestEqual(TEXT("Address survived"), Received.Address, FString(TEXT("/laser/Aim")));
	if (TestEqual(TEXT("Five flat values arrived"), Received.Args.Num(), 5))
	{
		TestEqual(TEXT("[0]"), Received.Args[0].AsNumber(), 0.25);
		TestEqual(TEXT("[1]"), Received.Args[1].AsNumber(), 0.5);
		TestEqual(TEXT("[2] vector x"), Received.Args[2].AsNumber(), 1.0);
		TestEqual(TEXT("[3] vector y"), Received.Args[3].AsNumber(), -2.0);
		TestEqual(TEXT("[4] vector z"), Received.Args[4].AsNumber(), 3.0);

		// Every one of them is a plain float. The receiving software has no idea a
		// vector was ever involved, which is the point.
		for (int32 Index = 0; Index < Received.Args.Num(); ++Index)
		{
			TestEqual(FString::Printf(TEXT("[%d] is a float on the wire"), Index),
				static_cast<int32>(Received.Args[Index].Type), static_cast<int32>(EOscuValueType::Float));
		}
	}

	TestEqual(TEXT("Sender counted it"), Sender.GetMessagesSent(), static_cast<uint64>(1));
	TestEqual(TEXT("Nothing failed"), Sender.GetSendFailures(), static_cast<uint64>(0));

	// A malformed address is refused before it reaches the socket.
	FOscuMessage Bad;
	Bad.Address = TEXT("no-leading-slash");
	Bad.Args.Add(FOscuValue::MakeFloat(1.0));
	TestFalse(TEXT("An address without a leading slash is refused"), Sender.Send(Bad));
	TestEqual(TEXT("...and counted as a failure"), Sender.GetSendFailures(), static_cast<uint64>(1));

	// A zero-argument trigger round-trips too.
	FOscuMessage Trigger;
	Trigger.Address = TEXT("/laser/Stop");
	TestTrue(TEXT("Trigger sent"), Sender.Send(Trigger));
	if (TestTrue(TEXT("Trigger arrived"), WaitForMessage(Receiver, Received)))
	{
		TestEqual(TEXT("Trigger address"), Received.Address, FString(TEXT("/laser/Stop")));
		TestEqual(TEXT("Trigger has no arguments"), Received.Args.Num(), 0);
	}

	Sender.Close();
	TestFalse(TEXT("Closed sender is not ready"), Sender.IsReady());

	return true;
}

//////////////////////////////////////////////////////////////////////////
// Coalescing, tested without a socket in the way

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOscuCoalescingTest,
	"OSCulator.OSC.Coalescing",
	OscuNetTest::Flags)

bool FOscuCoalescingTest::RunTest(const FString& Parameters)
{
	auto Message = [](const TCHAR* Address, double Value)
	{
		FOscuMessage Result;
		Result.Address = Address;
		Result.Args.Add(FOscuValue::MakeFloat(Value));
		return Result;
	};

	auto Trigger = [](const TCHAR* Address)
	{
		FOscuMessage Result;
		Result.Address = Address;
		return Result;
	};

	// Stands in for the router's IsTriggerAddress. Anything with no arguments is a
	// trigger, and so is /laser/Stop even when a sender appends surplus values --
	// which is the case the message's own argument count cannot answer.
	auto IsTrigger = [](const FOscuMessage& Message)
	{
		return Message.Args.Num() == 0 || Message.Address == TEXT("/laser/Stop");
	};

	{
		// A frame's worth of a 60 Hz stream across two parameters. Only the last
		// value of each can matter, so six messages become two dispatches.
		TArray<FOscuMessage> Batch = {
			Message(TEXT("/laser/X"), 1.0),
			Message(TEXT("/laser/Y"), 10.0),
			Message(TEXT("/laser/X"), 2.0),
			Message(TEXT("/laser/Y"), 20.0),
			Message(TEXT("/laser/X"), 3.0),
			Message(TEXT("/laser/Y"), 30.0),
		};

		OscuOSC::CoalesceBatch(Batch, {}, IsTrigger);

		if (TestEqual(TEXT("Six messages collapse to two"), Batch.Num(), 2))
		{
			// Position comes from first appearance, value from last.
			TestEqual(TEXT("X kept its position"), Batch[0].Address, FString(TEXT("/laser/X")));
			TestEqual(TEXT("X kept the LAST value"), Batch[0].Args[0].AsNumber(), 3.0);
			TestEqual(TEXT("Y kept its position"), Batch[1].Address, FString(TEXT("/laser/Y")));
			TestEqual(TEXT("Y kept the LAST value"), Batch[1].Args[0].AsNumber(), 30.0);
		}
	}

	{
		// A no-arg function is a trigger, and every hit has to fire.
		TArray<FOscuMessage> Batch = {
			Trigger(TEXT("/laser/Stop")),
			Trigger(TEXT("/laser/Stop")),
			Trigger(TEXT("/laser/Stop")),
		};

		OscuOSC::CoalesceBatch(Batch, {}, IsTrigger);
		TestEqual(TEXT("Zero-argument triggers never coalesce"), Batch.Num(), 3);
	}

	{
		// The case the message alone cannot answer. Now that surplus arguments are
		// tolerated, a sender that cannot emit an empty message hits a trigger with
		// a value attached. Judged by argument count that would look coalescable;
		// judged by the target function's arity it is still a trigger.
		TArray<FOscuMessage> Batch = {
			Message(TEXT("/laser/Stop"), 0.0),
			Message(TEXT("/laser/Stop"), 0.0),
			Message(TEXT("/laser/Stop"), 0.0),
		};

		OscuOSC::CoalesceBatch(Batch, {}, IsTrigger);
		TestEqual(TEXT("A trigger carrying surplus arguments still never coalesces"), Batch.Num(), 3);
	}

	{
		// The explicit opt-out, for anything else where every hit matters.
		TArray<FOscuMessage> Batch = {
			Message(TEXT("/drum/Hit"), 0.4),
			Message(TEXT("/drum/Hit"), 0.9),
			Message(TEXT("/laser/X"), 1.0),
			Message(TEXT("/laser/X"), 2.0),
		};

		TSet<FString> NoCoalesce;
		NoCoalesce.Add(TEXT("/drum/Hit"));

		OscuOSC::CoalesceBatch(Batch, NoCoalesce, IsTrigger);

		if (TestEqual(TEXT("Exempt address kept both hits, the other collapsed"), Batch.Num(), 3))
		{
			TestEqual(TEXT("First hit survives"), Batch[0].Args[0].AsNumber(), 0.4);
			TestEqual(TEXT("Second hit survives"), Batch[1].Args[0].AsNumber(), 0.9);
			TestEqual(TEXT("Coalesced address kept its last value"), Batch[2].Args[0].AsNumber(), 2.0);
		}
	}

	{
		// Interleaving between different addresses must not be reordered.
		TArray<FOscuMessage> Batch = {
			Message(TEXT("/a/One"), 1.0),
			Message(TEXT("/b/Two"), 2.0),
			Message(TEXT("/c/Three"), 3.0),
		};
		OscuOSC::CoalesceBatch(Batch, {}, IsTrigger);
		if (TestEqual(TEXT("Distinct addresses all survive"), Batch.Num(), 3))
		{
			TestEqual(TEXT("Order preserved"), Batch[0].Address, FString(TEXT("/a/One")));
			TestEqual(TEXT("Order preserved"), Batch[1].Address, FString(TEXT("/b/Two")));
			TestEqual(TEXT("Order preserved"), Batch[2].Address, FString(TEXT("/c/Three")));
		}
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
