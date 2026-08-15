// Copyright Baron Lanteigne. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "OscuValue.h"

/**
 * Anything that can carry an FOscuMessage outwards: an OSC sender, a MIDI
 * output, a log spy in a test.
 *
 * Defined up front even though outputs come last, so that the output work later
 * adds files rather than editing existing ones.
 */
// No API macro: this is a pure interface with nothing but a defaulted destructor
// and pure virtuals, so it has no symbols to export. Marking it dllimport makes
// MSVC expect an exported constructor and destructor that no .cpp ever provides.
class IOscuSink
{
public:
	virtual ~IOscuSink() = default;

	virtual bool Send(const FOscuMessage& Msg) = 0;
	virtual bool IsReady() const = 0;
};
