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

## Event names that clash with variables

**Explain:** Blueprint refuses an event named like an existing variable, so the event
cannot be called `Cooldown` when a `Cooldown` variable exists. The convention used in
this project is `SetCooldown`, and the sender's address follows: `/tag/SetCooldown`.
