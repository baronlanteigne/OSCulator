// Copyright Baron Lanteigne. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Queue.h"
#include "HAL/Runnable.h"
#include "OscuValue.h"

#include <atomic>

class FInternetAddr;
class FRunnableThread;
class FSocket;

/**
 * Owns the UDP socket and the thread that reads it.
 *
 * Everything here exists to make the hop from socket to game thread cost as
 * little as possible. Parsing happens on this thread because it is pure and
 * touches no UObject; only the finished FOscuMessage crosses over.
 *
 * The socket is deliberately NOT polled from the game thread. Polling reintroduces
 * up to a full frame of OS buffering, which is exactly the latency this is here
 * to avoid.
 */
class OSCULATOROSC_API FOscuOSCReceiver : public FRunnable
{
public:
	FOscuOSCReceiver();
	virtual ~FOscuOSCReceiver() override;

	FOscuOSCReceiver(const FOscuOSCReceiver&) = delete;
	FOscuOSCReceiver& operator=(const FOscuOSCReceiver&) = delete;

	/**
	 * Opens the socket and starts the thread. Returns false and logs a reason if
	 * the port is taken or the bind address is unusable.
	 *
	 * AllowedIPs empty means accept from anywhere.
	 */
	bool Start(const FString& BindAddress, int32 Port, const TArray<FString>& AllowedIPs, int32 ReceiveBufferSize);

	/** Stops the thread and closes the socket. Safe to call twice. */
	void Shutdown();

	// FRunnable
	virtual uint32 Run() override;
	virtual void Stop() override;

	/**
	 * Single consumer, game thread only. Returns false when the queue is empty.
	 */
	bool Dequeue(FOscuMessage& OutMessage) { return InboundQueue.Dequeue(OutMessage); }

	bool IsListening() const { return Socket != nullptr; }
	int32 GetBoundPort() const { return BoundPort; }

	uint64 GetPacketsReceived() const { return PacketsReceived.load(std::memory_order_relaxed); }
	uint64 GetPacketsRejected() const { return PacketsRejected.load(std::memory_order_relaxed); }
	uint64 GetPacketsFromBlockedSenders() const { return PacketsFromBlockedSenders.load(std::memory_order_relaxed); }

private:
	void CloseSocket();

	FSocket* Socket = nullptr;
	FRunnableThread* Thread = nullptr;
	std::atomic<bool> bStopRequested{ false };

	int32 BoundPort = 0;

	/** Single producer (this thread), single consumer (game thread). No locks. */
	TQueue<FOscuMessage, EQueueMode::Spsc> InboundQueue;

	/** Sized once for the largest possible datagram, so no per-packet resizing. */
	TArray<uint8> ReceiveBuffer;

	/** Empty means accept all. Raw host-order addresses, resolved once at Start. */
	TSet<uint32> AllowedSenderIPs;

	std::atomic<uint64> PacketsReceived{ 0 };
	std::atomic<uint64> PacketsRejected{ 0 };
	std::atomic<uint64> PacketsFromBlockedSenders{ 0 };

	/** Malformed traffic is reachable from the network, so its logging is throttled. */
	double LastParseErrorLogTime = 0.0;
	uint64 ParseErrorsSinceLastLog = 0;
};
