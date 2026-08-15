// Copyright Baron Lanteigne. All Rights Reserved.

#include "OscuOSCReceiver.h"

#include "OSCulatorCore.h"
#include "OscuOSCCodec.h"

#include "Common/UdpSocketBuilder.h"
#include "HAL/RunnableThread.h"
#include "IPAddress.h"
#include "SocketSubsystem.h"
#include "Sockets.h"

namespace
{
	/** A UDP datagram cannot exceed this, so the buffer is sized once and reused. */
	constexpr int32 GMaxDatagramSize = 65536;

	/** How long Run() blocks before checking whether it has been asked to stop. */
	constexpr int32 GWaitMilliseconds = 100;

	/** Malformed traffic can arrive at line rate; do not let it own the log. */
	constexpr double GParseErrorLogInterval = 5.0;
}

FOscuOSCReceiver::FOscuOSCReceiver()
{
	ReceiveBuffer.SetNumUninitialized(GMaxDatagramSize);
}

FOscuOSCReceiver::~FOscuOSCReceiver()
{
	Shutdown();
}

bool FOscuOSCReceiver::Start(const FString& BindAddress, int32 Port, const TArray<FString>& AllowedIPs, int32 ReceiveBufferSize)
{
	check(Socket == nullptr);

	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (SocketSubsystem == nullptr)
	{
		UE_LOG(LogOSCulator, Error, TEXT("No socket subsystem; OSC input is unavailable."));
		return false;
	}

	// Resolve the allowlist once. A bad entry is worth shouting about, because the
	// symptom otherwise is silence that looks exactly like a wiring problem.
	AllowedSenderIPs.Reset();
	for (const FString& Entry : AllowedIPs)
	{
		const TSharedRef<FInternetAddr> Addr = SocketSubsystem->CreateInternetAddr();
		bool bValid = false;
		Addr->SetIp(*Entry, bValid);
		if (!bValid)
		{
			UE_LOG(LogOSCulator, Warning, TEXT("AllowedSenderIPs entry '%s' is not a valid address. Ignored."), *Entry);
			continue;
		}
		uint32 Raw = 0;
		Addr->GetIp(Raw);
		AllowedSenderIPs.Add(Raw);
	}

	FIPv4Address ParsedBind;
	if (!FIPv4Address::Parse(BindAddress, ParsedBind))
	{
		UE_LOG(LogOSCulator, Error, TEXT("BindAddress '%s' is not a valid IPv4 address. OSC input not started."), *BindAddress);
		return false;
	}

	Socket = FUdpSocketBuilder(TEXT("OSCulator OSC In"))
		.AsBlocking()
		.AsReusable()
		.BoundToAddress(ParsedBind)
		.BoundToPort(Port)
		.WithReceiveBufferSize(ReceiveBufferSize)
		.Build();

	if (Socket == nullptr)
	{
		UE_LOG(LogOSCulator, Error, TEXT("Could not bind UDP %s:%d. Is another application already listening there?"),
			*BindAddress, Port);
		return false;
	}

	// The builder requests a size; the OS decides. Report what was actually granted,
	// because a silently clamped buffer is the usual cause of drops under a burst.
	int32 ActualBufferSize = 0;
	Socket->SetReceiveBufferSize(ReceiveBufferSize, ActualBufferSize);

	// Ask the socket rather than trusting the request: port 0 means "any free port",
	// and the tests rely on discovering which one that turned out to be.
	BoundPort = Socket->GetPortNo();
	bStopRequested.store(false, std::memory_order_relaxed);

	Thread = FRunnableThread::Create(this, TEXT("OSCulatorOSCReceiver"), 128 * 1024, TPri_AboveNormal);
	if (Thread == nullptr)
	{
		UE_LOG(LogOSCulator, Error, TEXT("Could not start the OSC receive thread."));
		CloseSocket();
		return false;
	}

	UE_LOG(LogOSCulator, Log, TEXT("OSC input listening on %s:%d (receive buffer %d bytes, %s)."),
		*BindAddress, BoundPort, ActualBufferSize,
		AllowedSenderIPs.Num() > 0
			? *FString::Printf(TEXT("%d allowed sender(s)"), AllowedSenderIPs.Num())
			: TEXT("any sender"));

	return true;
}

void FOscuOSCReceiver::Stop()
{
	bStopRequested.store(true, std::memory_order_relaxed);
}

void FOscuOSCReceiver::Shutdown()
{
	Stop();

	if (Thread != nullptr)
	{
		// Run() wakes at least every GWaitMilliseconds, so this returns promptly.
		Thread->WaitForCompletion();
		delete Thread;
		Thread = nullptr;
	}

	CloseSocket();
}

void FOscuOSCReceiver::CloseSocket()
{
	if (Socket != nullptr)
	{
		Socket->Close();
		if (ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM))
		{
			SocketSubsystem->DestroySocket(Socket);
		}
		Socket = nullptr;
	}
	BoundPort = 0;
}

uint32 FOscuOSCReceiver::Run()
{
	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (SocketSubsystem == nullptr)
	{
		return 1;
	}

	const TSharedRef<FInternetAddr> Sender = SocketSubsystem->CreateInternetAddr();
	TArray<FOscuMessage> Parsed;

	while (!bStopRequested.load(std::memory_order_relaxed))
	{
		// A short blocking wait rather than a spin, so Stop() stays responsive
		// without burning a core.
		if (!Socket->Wait(ESocketWaitConditions::WaitForRead, FTimespan::FromMilliseconds(GWaitMilliseconds)))
		{
			continue;
		}

		uint32 PendingSize = 0;
		while (!bStopRequested.load(std::memory_order_relaxed) && Socket->HasPendingData(PendingSize))
		{
			int32 BytesRead = 0;
			if (!Socket->RecvFrom(ReceiveBuffer.GetData(), ReceiveBuffer.Num(), BytesRead, *Sender))
			{
				break;
			}
			if (BytesRead <= 0)
			{
				continue;
			}

			PacketsReceived.fetch_add(1, std::memory_order_relaxed);

			uint32 SenderIP = 0;
			Sender->GetIp(SenderIP);

			if (AllowedSenderIPs.Num() > 0 && !AllowedSenderIPs.Contains(SenderIP))
			{
				PacketsFromBlockedSenders.fetch_add(1, std::memory_order_relaxed);
				continue;
			}

			Parsed.Reset();
			FString Error;
			if (!OscuOSCCodec::Parse(ReceiveBuffer.GetData(), BytesRead, Parsed, Error))
			{
				PacketsRejected.fetch_add(1, std::memory_order_relaxed);
				++ParseErrorsSinceLastLog;

				const double Now = FPlatformTime::Seconds();
				if (Now - LastParseErrorLogTime >= GParseErrorLogInterval)
				{
					UE_LOG(LogOSCulator, Warning, TEXT("Dropped %llu malformed packet(s); most recent: %s"),
						ParseErrorsSinceLastLog, *Error);
					LastParseErrorLogTime = Now;
					ParseErrorsSinceLastLog = 0;
				}
				continue;
			}

			const double ReceiveTime = FPlatformTime::Seconds();
			for (FOscuMessage& Message : Parsed)
			{
				Message.SourceIP = SenderIP;
				Message.ReceiveTime = ReceiveTime;
				InboundQueue.Enqueue(MoveTemp(Message));
			}
		}
	}

	return 0;
}
