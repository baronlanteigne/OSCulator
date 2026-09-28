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
Note names both directions, `UOscuMIDIMap` — target-first `Bindings[]` with `Sources[]`,
an O(1) multimap lookup, per-binding value remapping — `UOscuMIDISubsystem` (an
**engine** subsystem: devices are global hardware and Learn must work at edit time),
notes and control change, ingest in Lenient mode, Auto-Map from per-tag rules, Validate,
Prune, per-source Learn that captures device/channel/type/number in one gesture, and
several maps live at once.

Input runs on **OSCulator's own PortMidi instance** (`FOscuMIDIPort`), not the engine's
MIDIDevice plugin — see the surprises below for why. Output still uses
`UMIDIDeviceOutputController`.

`OSCulator.MIDIDevices` / `MIDIStatus` / `MIDIValidate` / `MIDIRestart` / `MIDIMonitor`.

*Tests:* exhaustive note-name round trip across all 128 notes and all 6 octave
conventions, ingest values, value shaping and inverted ranges, note off, control
change, shared inputs, the device filter, several live maps, auto-map
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
- Auto-map lists a real Blueprint's events additively (`0 changed` on a second run)
- Three devices open through OSCulator's own PortMidi instance, notes decoding with the
  right channels, **zero** engine MIDI errors and no queue overflow
- `Pm_SetFilter` removing 100% of clock (340 messages in six seconds → 0) while the note
  rate stayed identical, on an Elektron TM-1
- Two PortMidi instances side by side: the engine's copy terminated and reinitialised
  while an OSCulator stream was open and reading, 0 read errors

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

### Control change from real hardware
The CC path is covered end to end by automation — decode, device match, remap, inverted
range, remap off — but no physical knob has yet reached a function. Two attempts caught
an idle rig. `OSCulator.MIDIMonitor 1` prints every message with its device; that is the
check to run.

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
| Strictly one map per device | Built the permissive version instead: several maps live at once, each with an optional `Default Device` its sources inherit. Forcing the split would duplicate a binding — and its remap and note-off settings — for any function driven by two controllers, and would copy the level's whole function inventory into every device's asset |
| Migrating the old channel-first map | Baron did not want the old data. `Channels[]` was replaced outright by `Bindings[]` rather than carrying a deprecated property forever |
| Auto-remapping a renamed function | Validate lists the broken binding and the unclaimed function side by side, which makes it a retype. Guessing which rename is which would be a guess |
| A per-source remap range | On the binding, so a pad and a knob driving one function agree about its range. Easy to move if a case ever needs it |
| Cross-map collision detection in the asset | `Validate` can only see its own bindings. `OSCulator.MIDIValidate` does it across every active map instead |
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

**MIDI ports are only released explicitly.** A PortMidi stream is not a managed
resource — drop the reference and the port stays held for the life of the process, so no
other application can take it. `FOscuMIDIPort` closes in its destructor and is move-only
for exactly this reason.

**PortMidi links statically and MIDIDevice exports none of its symbols.** Adding
`"portmidi"` to another module's Build.cs therefore compiles a **second private copy**
with its own uninitialised globals — it does not reach the engine's. Calling
`Pm_SetFilter` on a stream the engine opened crashed the editor on load:
`pm_descriptors[...]` off a null table. A private copy is only usable if you call
`Pm_Initialize` in it and own every stream you touch, which is what OSCulator now does.
Two PortMidi instances coexist in the process safely — verified by making the engine
terminate and reinitialise its copy while an OSCulator stream was open and reading.

**UE's MIDI input plugin never gets the buffer size it asks for.**
`UMIDIDeviceInputController::StartupDevice` passes its own zeroed `MIDIBufferSize` member
to `Pm_OpenInput` instead of the argument it was handed, and only assigns the member
afterwards. Identical in 5.3 through 5.8. Combined with a filter that drops only active
sensing, a sequencer at tempo overflows the queue within seconds of pressing Play —
measured at 68% clock.

**`UMIDIDeviceManager::FindAllMIDIDeviceInfo` is not a read.** It calls
`ReinitializeDeviceManager`, which terminates PortMidi and restarts every controller it
can still reach *through `TObjectIterator`* — including ones you shut down and dropped
but which GC has not collected, which then reopen the port and make the device read as
"already in use by another application".

**TouchDesigner's OSC Out CHOP emits `/_samplerate`** alongside its channels, every
frame. Hence the reserved `/_` namespace. Its MIDI `n` labels are also **1-based**.

**Whether a MIDI input port can be shared is a driver question, not a Windows one.** An
Elektron TM-1 was verified open in TouchDesigner and Unreal at once, both receiving.
PortMidi's `opened` flag is per-process-copy and cannot see other applications, so a
conflict can only be detected by trying.

**Config properties load into the CDO**, so `GetDefault<>()` returns whatever the
project's ini says. There is no compile-time default left to assert against — a test
that checks header defaults is really testing the user's settings file.

**A bare OSC address with no type tag string is a legal zero-argument message.** The
tag string is nominally required but historically optional, and TouchDesigner omits it.
Refusing it makes triggers unreachable from a mainstream tool.

**Anything reachable from the network must never log per-message at Warning.**
Unroutable addresses are reported once per distinct address, capped at 64. Malformed
packets are throttled to one line per five seconds.
