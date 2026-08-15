# OSCulator — Build Status

**As of 2026-08-15. All seven phases built. 35 automation tests green.**

Run the suite headless:

```
& "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" `
  "A:\_UE\OSCulator\OSCulator.uproject" `
  -ExecCmds="Automation RunTests OSCulator;Quit" `
  -unattended -nopause -nosplash -NullRHI -NoSound -log `
  -testexit="Automation Test Queue Empty"
```

Exit code 0 means everything passed. Results land in `Saved/Logs/OSCulator.log`;
grep `Test Completed`. A crash shows as `Critical error` with a symbolicated stack.

**The editor must be closed to build.** A plugin with new modules cannot hot-reload
through Live Coding, and adding a `UPROPERTY`/`UFUNCTION` needs a full rebuild
regardless.

---

## What is built

### Phase 1 — Codec
`FOscuValue`, `FOscuMessage`, `IOscuSink`, and `OscuOSCCodec` parsing *and*
serialising OSC 1.0. All ten type tags, bundles flattened with the timetag dropped,
explicit big-endian reads, every read bounds-checked.

*Tests:* byte-identical round trip, every type tag from hand-built packets, truncation
of every prefix, malformed rejection, bundles.

### Phase 2 — Registry and introspection
`UOscuRouterSubsystem`, tag scan on begin play, spawn handler, lazy purge of destroyed
actors, per-class exposure cache, `Introspect()`, the `OSCulator.List` family and
`OSCulator.Export`.

*Tests:* tag scanning, runtime spawn, stale purge, the whole §6 type table as reported
signatures, the three filters.

### Phase 3 — Dispatch
The marshaller's fill side, Strict/Lenient policy, per-class grouping, the out-param
branch, `OSCulator.Send`.

*Tests:* value correctness including struct reassembly, multiple actors per tag,
Strict rejection wording, Lenient zero-filling, trailing arrays, out parameters,
permissive coercion, enum-by-name.

### Phase 4 — Live input
`FRunnable` receive thread, UDP socket, lockless SPSC queue, drain on
`OnWorldPreActorTick`, coalescing, sender allowlist, `OSCulator.Status`,
`stat OSCulator`.

*Tests:* real loopback datagram, allowlist rejection, garbage recovery, coalescing
semantics.

*Measured:* 0.04 ms average / 0.08 ms max per frame under a light TouchDesigner
stream. Coalescing counters reconcile exactly.

### Phase 5 — Settings
All four gates with full `EditCondition`/`EditConditionHides` coverage, so an unused
branch vanishes from the panel rather than greying out. Multi-target OSC output and
multi-device MIDI lists. `OnSettingsChanged` so edits take effect live.

*Tests:* the input gate genuinely opens or does not open a socket.

### Phase 6 — MIDI input
Note names both directions, `UOscuMIDIMap` with an O(1) flat lookup,
`UOscuMIDISubsystem` (an **engine** subsystem — devices are global hardware and Learn
must work at edit time), ingest in Lenient mode, auto-populate, per-row Learn,
`OSCulator.MIDIDevices` / `MIDIStatus` / `MIDIRestart` / `MIDIMonitor`.

*Tests:* exhaustive note-name round trip across all 128 notes and all 6 octave
conventions, ingest values, all three value modes, note off, auto-populate
idempotency and additive-only behaviour.

### Phase 7 — Outputs
`FOscuOSCSender` (the first real `IOscuSink`), multi-target OSC out, `UOscuOSCLibrary`
with `Send OSC` / `Send OSC (Floats)` and six autocast conversions, `UOscuMIDILibrary`
with auto note-off scheduling, and OSC self-describe on `/_describe`.

*Tests:* real socket send/receive proving a float + float + vector arrives as five
floats, wire-channel conversion round trip, note-off scheduling and flushing,
describe reply shape.

### Helper nodes
Blueprint nodes layered on top that have nothing to do with OSC or MIDI — they
interpret values after delivery, so any transport can feed them. `UOscuMaterialLibrary`
with `Resolve Material Parameter Value`, which splits a variable-length float array
into a scalar or a colour so one event drives both material setters. See
[HELPERS.md](HELPERS.md).

*Tests:* every length from 0 to 6, alpha defaulting to opaque, outputs cleared on the
branch not taken, and end to end through a trailing-array signature.

---

## Verified by hand

- Blueprint class registers; parameters classify correctly (`vec3, name, float` → 5 args)
- `OSCulator.List Custom` narrows to authored events only
- `OSCulator.Send` fires a Blueprint event across two actors sharing a tag
- TouchDesigner drives a Blueprint event with no measurable frame cost
- `/_describe` returns the surface
- MIDI input triggers a mapped function from a real controller
- MIDI **Learn** captures a note off a pad
- Auto-populate maps a real Blueprint's events (`+3 new, 0 changed`)

---

## Not yet verified

### `BlueprintAutocast` on Make Array element pins

**The one item from the spec's verify-early list still open.** The intended experience
is wiring a float and an `FVector` straight into a `Make Array` feeding `Send OSC`
with no conversion nodes.

The doubt: `Make Array` is a wildcard node whose element type is fixed by whatever
connects first. Connect Make Array's *output to the Send node first* and the wildcard
should resolve to `FOscuValue`, letting the autocasts fire. Untested.

`Send OSC (Floats)` already exists as the escape hatch for all-float messages. If
autocast proves clumsy, the spec's fuller answer is a custom `UK2Node` with
user-addable wildcard pins — a Send node with a `+` button, the way Make Array works.
That is real work (pin management, `ExpandNode`) and is explicitly **not** to be
attempted until the simple version has proven insufficient.

### OSC output against real receiving software
The wire format is proven by test, but nothing has yet received an OSCulator message
in TouchDesigner or Max.

### MIDI output against real hardware
`ToWireChannel` is unit-tested, and the auto note-off bookkeeping is tested, but no
note has been sent to a physical device. **The channel conversion is the thing to
watch** — see the note below.

### Performance under real load
Measured only at ~1.5 messages/frame. Baron expects **10–20× heavier** use. Worth
re-running `stat OSCulator` at that scale before calling performance settled.

Known candidates if it ever gets uncomfortable, none currently justified:
`DispatchMessage` allocates per message via `Address.ParseIntoArray` and builds a
`TMap<UClass*, TArray<AActor*>>` per call. Both could use reusable scratch buffers.

---

## Noted for later

Wanted, not yet requested. Do not build without asking.

| Idea | Notes |
| --- | --- |
| A 7-value transform | loc3, rot3, then **one** scale value used for all three axes. Alongside the 9-value form, not replacing it |
| MIDI increment | A value that steps on each subsequent MIDI trigger, rather than every hit sending the same thing |

---

## Discussed but deliberately not built

| Idea | Why not |
| --- | --- |
| Multiple OSC input sockets | One socket on `0.0.0.0` already hears every sender. Only useful to separate traffic by port |
| Per-device MIDI maps | Devices merge into one stream. Use different MIDI channels instead. Would need a device field on the map row |
| Learn capturing the channel | Moving a row between channels would drag its siblings. Currently writes the note and warns on mismatch. Baron chose to leave it |
| Custom `UK2Node` Send node | Only if autocast proves insufficient — see above |
| A "Learn next row" walker button | Would beat expanding `Channels → [n] → Notes → [n]` per row. Raised as an option, not requested |

---

## Behaviours that surprised us

Each of these cost real debugging time and is worth remembering.

**`ExecuteUbergraph_*` is marshallable and must never be exposed.** It is a Blueprint's
entire event graph behind one `int32` bytecode offset, so it passes validation as an
ordinary one-argument function. Sending an integer jumps into the middle of the graph.
Filtered by flag *and* by name — `FUNC_UbergraphFunction` is documented as set "only
when using the persistent ubergraph frame", so a simple Blueprint may not carry it.

**UE's MIDIDevice plugin disagrees with itself about channels.** Input reports
`(Status % 16) + 1`, i.e. **1–16**. Output ORs the channel straight into the status
byte, i.e. **0–15**. OSCulator is 1–16 throughout and converts in `ToWireChannel`
alone. The spec assumed the error was on input; it is on output.

**A plain `float&` parameter consumes no argument; `UPARAM(ref) float&` consumes one.**
`CPF_ReferenceParm` is exactly the "this is also an input" marker. Both still force a
per-actor frame, because `ProcessEvent` writes back through either.

**MIDI ports are only released in the controller's destructor**, which is
garbage-collected. Dropping the reference leaves the port held indefinitely. Must call
`ShutdownDevice()` explicitly.

**TouchDesigner's OSC Out CHOP emits `/_samplerate`** alongside its channels, every
frame. Hence the reserved `/_` namespace. Its MIDI `n` labels are also **1-based**.

**Config properties load into the CDO**, so `GetDefault<>()` returns whatever the
project's ini says. There is no compile-time default left to assert against — a test
that checks header defaults is really testing the user's settings file.

**A bare OSC address with no type tag string is a legal zero-argument message.** The
tag string is nominally required but historically optional, and TouchDesigner omits it.
Refusing it makes triggers unreachable from a mainstream tool.

**Anything reachable from the network must never log per-message at Warning.**
Unroutable addresses are reported once per distinct address, capped at 64. Malformed
packets are throttled to one line per five seconds.
