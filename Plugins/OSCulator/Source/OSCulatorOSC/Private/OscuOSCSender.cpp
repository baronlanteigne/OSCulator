// Copyright Baron Lanteigne. All Rights Reserved.

#include "OscuOSCSender.h"

#include "OSCulatorCore.h"
#include "OscuOSCCodec.h"

#include "Common/UdpSocketBuilder.h"
#include "IPAddress.h"
#include "SocketSubsystem.h"
#include "Sockets.h"

namespace
{
	/** A destination that has gone away fails on every single send. */
	constexpr double GSendFailureLogInterval = 5.0;
}

FOscuOSCSender::~FOscuOSCSender()
{
	Close();
}

bool FOscuOSCSender::Open(FName InName, const FString& InHost, int32 InPort)
{
	Close();

	Name = InName;
	Host = InHost;
	Port = InPort;

	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (SocketSubsystem == nullptr)
	{
		UE_LOG(LogOSCulator, Error, TEXT("No socket subsystem; OSC output is unavailable."));
		return false;
	}

	Destination = SocketSubsystem->CreateInternetAddr();

	// A dotted address first, then a name lookup. Shows are usually wired by IP,
	// but a hostname should not be a silent failure.
	bool bValidIP = false;
	Destination->SetIp(*Host, bValidIP);

	if (!bValidIP)
	{
		FAddressInfoResult Resolved = SocketSubsystem->GetAddressInfo(
			*Host, nullptr, EAddressInfoFlags::Default, NAME_None);

		if (Resolved.ReturnCode != SE_NO_ERROR || Resolved.Results.Num() == 0)
		{
			UE_LOG(LogOSCulator, Error, TEXT("OSC target '%s': host '%s' could not be resolved."),
				*Name.ToString(), *Host);
			Destination.Reset();
			return false;
		}

		Destination = Resolved.Results[0].Address->Clone();
	}

	Destination->SetPort(Port);

	Socket = FUdpSocketBuilder(*FString::Printf(TEXT("OSCulator OSC Out (%s)"), *Name.ToString()))
		.AsReusable()
		.Build();

	if (Socket == nullptr)
	{
		UE_LOG(LogOSCulator, Error, TEXT("OSC target '%s': could not create a socket."), *Name.ToString());
		Destination.Reset();
		return false;
	}

	UE_LOG(LogOSCulator, Log, TEXT("OSC output ready: '%s' -> %s:%d"), *Name.ToString(), *Host, Port);
	return true;
}

void FOscuOSCSender::Close()
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
	Destination.Reset();
}

bool FOscuOSCSender::Send(const FOscuMessage& Message)
{
	if (Socket == nullptr || !Destination.IsValid())
	{
		return false;
	}

	if (!OscuOSCCodec::Serialize(Message, ScratchBuffer))
	{
		// A malformed address is the author's mistake, not the network's, so it is
		// worth naming every time rather than throttling.
		UE_LOG(LogOSCulator, Warning, TEXT("OSC target '%s': '%s' could not be serialised. Not sent."),
			*Name.ToString(), *Message.Address);
		++SendFailures;
		return false;
	}

	int32 BytesSent = 0;
	const bool bSent = Socket->SendTo(ScratchBuffer.GetData(), ScratchBuffer.Num(), BytesSent, *Destination)
		&& BytesSent == ScratchBuffer.Num();

	if (!bSent)
	{
		++SendFailures;

		const double Now = FPlatformTime::Seconds();
		if (Now - LastFailureLogTime >= GSendFailureLogInterval)
		{
			LastFailureLogTime = Now;
			UE_LOG(LogOSCulator, Warning, TEXT("OSC target '%s' (%s:%d): send failed (%llu failure(s) so far)."),
				*Name.ToString(), *Host, Port, SendFailures);
		}
		return false;
	}

	++MessagesSent;
	return true;
}
