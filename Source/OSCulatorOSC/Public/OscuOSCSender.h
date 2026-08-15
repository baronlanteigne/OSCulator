// Copyright Baron Lanteigne. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "OscuSink.h"
#include "OscuValue.h"

class FInternetAddr;
class FSocket;

/**
 * One UDP socket pointed at one destination.
 *
 * The first thing to actually implement IOscuSink, which was defined back when the
 * codec was written precisely so that outputs would add files rather than edit
 * existing ones.
 *
 * One sender per configured target. Sockets are cheap and the count is small, and
 * a socket per destination keeps failure isolated -- an unreachable host cannot
 * take the others down.
 */
class OSCULATOROSC_API FOscuOSCSender final : public IOscuSink
{
public:
	FOscuOSCSender() = default;
	virtual ~FOscuOSCSender() override;

	FOscuOSCSender(const FOscuOSCSender&) = delete;
	FOscuOSCSender& operator=(const FOscuOSCSender&) = delete;

	/**
	 * Resolves the destination and opens the socket.
	 *
	 * Host may be a dotted address or a hostname. Returns false and logs a reason
	 * if it resolves to nothing.
	 */
	bool Open(FName InName, const FString& InHost, int32 InPort);

	void Close();

	// IOscuSink
	virtual bool Send(const FOscuMessage& Message) override;
	virtual bool IsReady() const override { return Socket != nullptr; }

	FName GetName() const { return Name; }
	const FString& GetHost() const { return Host; }
	int32 GetPort() const { return Port; }
	uint64 GetMessagesSent() const { return MessagesSent; }
	uint64 GetSendFailures() const { return SendFailures; }

private:
	FSocket* Socket = nullptr;
	TSharedPtr<FInternetAddr> Destination;

	FName Name;
	FString Host;
	int32 Port = 0;

	/** Reused across sends so a steady stream allocates nothing after warmup. */
	TArray<uint8> ScratchBuffer;

	uint64 MessagesSent = 0;
	uint64 SendFailures = 0;

	/** Send failures are throttled: a host that has gone away fails every time. */
	double LastFailureLogTime = 0.0;
};
