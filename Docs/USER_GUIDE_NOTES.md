# OSCulator — Notes for the User Guide

Raw material for a future user guide: behaviour a user will trip over unless the guide
explains it. Each entry says what to explain and where the reference lives. The reference
docs (SETUP.md, HELPERS.md) stay the source of truth; this file only collects topics.

---

## Sending an enum (example: LTween's Ease)

**Explain:** an enum parameter takes a number **or** a name.

- Number: the entry's position, from 0. Decimals are cut off (`2.9` → `2`). Out-of-range
  numbers are not caught.
- Name: a string, `"OutQuad"` or `"ELTweenEase::OutQuad"`. An unknown name becomes `0`.

LTween's `ELTweenEase`, the enum most tween events here take:

| # | Ease | # | Ease | # | Ease |
| --- | --- | --- | --- | --- | --- |
| 0 | Linear | 10 | InSine | 20 | OutElastic |
| 1 | InQuad | 11 | OutSine | 21 | InOutElastic |
| 2 | OutQuad | 12 | InOutSine | 22 | InBack |
| 3 | InOutQuad | 13 | InExpo | 23 | OutBack |
| 4 | InCubic | 14 | OutExpo | 24 | InOutBack |
| 5 | OutCubic | 15 | InOutExpo | 25 | InBounce |
| 6 | InOutCubic | 16 | InCirc | 26 | OutBounce |
| 7 | InQuart | 17 | OutCirc | 27 | InOutBounce |
| 8 | OutQuart | 18 | InOutCirc | 28 | CurveFloat |
| 9 | InOutQuart | 19 | InElastic | | |

Reference: [Enum parameters](SETUP.md#enum-parameters).

## Missing values and defaults

**Explain:** values a message does not carry are zero, never the Blueprint's defaults.
Unreal only applies defaults on a call node in a graph; OSCulator calls the function
directly, and custom events have no input defaults at all.

- OSC refuses a message with too few arguments (Strict), so this only bites MIDI, which
  is always lenient: one knob into a `(Value, Duration, Ease)` event gives Duration `0`
  (instant) and Ease `0` (Linear).
- The workaround is a default decided inside the event — a `Select` on "is Duration 0".
- Open idea: let each MIDI map row set fixed values for the parameters it does not drive.

Reference: [Missing values are zero, not your defaults](SETUP.md#missing-values-are-zero-not-your-defaults).

## Nested addresses

**Explain:** `/tag/Group/Leaf` calls `Group_Leaf`, so a TouchDesigner parameter page with
a prefix maps onto one Blueprint event per parameter with no extra wiring.

Reference: [Address format](SETUP.md#address-format).

## The end of an address as an argument

**Explain:** one event can serve a whole page of material parameters. With no
`mat_Emissive` anywhere, `/tag/mat/Emissive` calls `mat("Emissive", …)` — as long as
`mat`'s first parameter is a Name or String. Pair it with
`Resolve Material Parameter Value` and a trailing array when the page mixes scalars and
colours; a float-only page needs neither, just `(Name, Value, Duration, Ease)`.

Reference: [The end of an address as an argument](SETUP.md#the-end-of-an-address-as-an-argument),
[Resolve Material Parameter Value](HELPERS.md).

## Components answer for their actor

**Explain:** an event on a component is reachable through the owning actor's tag, and a
same-named function on the actor (or a child Blueprint) overrides it — no pass-through
functions needed.

Reference: [Components answer for their actor](SETUP.md#components-answer-for-their-actor).

## Sending OSC

**Explain:** OSC output is off until enabled, and each destination is a named target.

- Enable **OSC Output** and add one **OSC Target** per destination (name, host, port).
  The Send node's **Target** picks one by name; `(all)` sends to every enabled target, so
  a message meant for one app needs its target named.
- **Send OSC (Floats)** for plain numbers; **Send OSC** for mixed types. Floats go out as
  `f`, ints as `i`, bools as `T`/`F`, strings as `s`, a vector as three floats.
- There is no rotator conversion: break the rotator and wire its three floats in the
  order the receiver expects.
- Wire **Make Array**'s output into Send OSC *before* wiring values into it, or it becomes
  a float array and the other types will not connect.
- Output opens when play begins and closes after EndPlay, so a "running = false" sent
  from EndPlay still goes out.
- **Host must be a real destination.** `0.0.0.0` means "any address" for a listener and
  is not somewhere to send to: every send fails (the log says `send failed`). Use
  `127.0.0.1` for the same machine.
- **Match how the receiver is bound.** An app bound to a specific address — TouchDesigner's
  OSC In with a Local Address set, for example — only hears packets sent to that address,
  so `127.0.0.1` will not reach something bound to `192.168.0.12`. `netstat -ano -p udp`
  lists what is listening where.
- "Ready" in the log means the socket opened, not that anyone is listening: UDP does not
  know. Test with any OSC monitor on the target port.

Reference: [OSC output](SETUP.md#4-osc-output).

## A note you set up once and fire later

**Explain:** for "send this note when X happens", add a **MIDI Note** struct variable
(OSCu MIDI Note), make it instance editable, and call **Send MIDI Note (Struct)** where
X happens. Device is a dropdown of the configured outputs. The note off is automatic
after Length; too short a Length and a frame-sampling receiver like TouchDesigner
misses the note entirely, so the default is 0.1 s.

Reference: [The MIDI Note struct](SETUP.md#the-midi-note-struct).

## The clock runs alongside the DAW, not from it

**Explain:** OSCulator's clock counts its own beats at its own BPM. From MIDI it takes
Start/Stop/Continue/Song Position and, if set up, tempo from one CC — never MIDI clock.
Users will expect "sync" to mean sample-locked; it means "started together, same
tempo", re-aligned on every Start or Continue. Two gotchas: the device needs
**Transport** ticked under Listen For, and the DAW has to be told to send transport to
that port (in Ableton that is the port's Sync output, which also sends clock —
harmless, OSCulator drops it). Beats count from 0, so the DAW's 1.1 is Bar 0, Beat 0.

Reference: [Clock](SETUP.md#6b-clock).

## One clock for everything that repeats

**Explain:** anything that repeats at a fraction of the tempo should read the OSCulator
clock, not keep its own timer or Quartz clock. There are three ways to do it, one per
job:
- **Visuals, lights, OSC/DMX, logic:** an **OSCu Pulse** component (Division, Offset,
  Enabled → On Pulse(Index)), or Get Pulse Phase to drive something smoothly. Up to a
  frame late, which you can't see.
- **Sound:** **Play On Pulse** (Audio Component, Division). Sample-accurate, on the
  clock's Quartz clock `Main`.
- **Your own Quartz logic:** **Get Quartz Clock**. Never make a second Quartz clock.

Divisions use the same dropdown as Quartz (1/32 … Whole, dotted, triplets, Beat, Bar).
Beat is a quarter note. Pulses count from beat 0 straight across bar lines, so a dotted
1/8 doesn't restart every bar. Offset 0.5 puts one effect halfway between another's
pulses, which is how the hide roll and the camera clicks interlace.

What users will notice:
- After a **Continue** or **Set Position** mid-song, quantized sounds can sit up to one
  beat off the grid until the next beat. Dotted and triplet sounds can sit off until the
  next 12-beat point in 4/4. A fresh **Start** lines everything up at once.
- The Quartz clock keeps ticking while transport is stopped, so a queued sound still
  plays, but nothing re-aligns until the next Start or Continue.
- Quartz runs on the audio device's clock and OSCulator on the system clock, so over a
  long set they can drift apart slowly. Every Start realigns them.

Reference: [Clock](SETUP.md#6b-clock).

## Getting the tempo in without MIDI clock

**Explain:** most DAWs can't send their tempo as a CC, so the BPM CC often isn't an
option. The other way in is OSC: anything that already knows the tempo sends one number
to `/clock/SetBPM`. In this project that's TouchDesigner, whose MIDI In shows a BPM
channel worked out from MIDI clock. Tell users to round it and send it only when it
changes, plus once a second so a fresh Play picks it up. 0 is ignored, because that's
what TouchDesigner shows before clock arrives. The plugin also keeps a Quartz clock
(`Main`) at this tempo, so quantized sounds follow it without any Blueprint.

Reference: [Clock](SETUP.md#6b-clock).

## Event names that clash with variables

**Explain:** Blueprint refuses an event named like an existing variable, so the event
cannot be called `Cooldown` when a `Cooldown` variable exists. The convention used in
this project is `SetCooldown`, and the sender's address follows: `/tag/SetCooldown`.
