// Copyright Baron Lanteigne. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "OscuValue.h"

/**
 * OSC 1.0 wire codec, both directions in one place.
 *
 * Having the serializer next to the parser lets the parser be round-trip tested
 * against itself, and makes OSC output a wiring job rather than new protocol code.
 *
 * Everything on the wire is big-endian. Reads are explicit shift-and-or so there
 * is no byte-swap macro to go hunting for on a new platform.
 *
 * Supported type tags:
 *   i  int32     -> Int
 *   f  float32   -> Float
 *   s  string    -> String
 *   b  blob      -> Blob
 *   h  int64     -> Int
 *   d  float64   -> Float
 *   T  true      -> Bool          (no payload)
 *   F  false     -> Bool          (no payload)
 *   N  nil       -> Float 0       (no payload)
 *   I  infinitum -> skipped       (no payload, produces no argument)
 *
 * Any other tag aborts that message: an unknown tag has an unknown width, so
 * there is no safe way to step over it.
 */
namespace OscuOSCCodec
{
	/**
	 * Parses one received datagram.
	 *
	 * A bundle yields several entries in OutMessages, flattened in order. The
	 * timetag is read past and ignored -- scheduling is explicitly out of scope.
	 *
	 * Returns false on malformed input, leaving a reason in OutError. Every read
	 * is bounds-checked; a truncated buffer is rejected, never read past. This is
	 * the one function in the plugin reachable directly from the network, so it
	 * is the one place where sloppiness becomes a crash rather than a log line.
	 *
	 * On failure OutMessages may already hold messages recovered from earlier
	 * elements of a bundle; callers that care should check the return value first.
	 */
	OSCULATOROSC_API bool Parse(const uint8* Data, int32 Len, TArray<FOscuMessage>& OutMessages, FString& OutError);

	/**
	 * Serializes a single message packet. Bundles are not produced.
	 *
	 * Type mapping is 1:1 except for two cases:
	 *   - Vector writes three consecutive 'f' args (and three 'f' tags).
	 *   - Int writes 'i' when the value fits in an int32 and 'h' when it does
	 *     not, so a large value is never silently truncated.
	 *
	 * Float always writes 'f'. A value that arrived as 'd' therefore comes back
	 * out as 'f' -- values survive a round trip, the exact tag does not.
	 */
	OSCULATOROSC_API bool Serialize(const FOscuMessage& Msg, TArray<uint8>& OutBytes);
}
