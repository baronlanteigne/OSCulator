# OSCulator — Console Reference

Open the console with `` ` `` (backtick).

Most commands need a **playing world** (PIE or packaged), because the transports live
there. The ones marked **editor too** work without entering play.

---

## Discovering what is callable

### `OSCulator.List` — editor too

Everything exposed, grouped by tag. Outside play it **rescans the open level first**, so
it always answers about the level as it is right now rather than as it was when
something last looked. With `Expose All Functions` on this includes a
couple of hundred inherited engine functions per actor.

```
[OSCulator] 1 tag(s), 214 exposed function(s)

  test  [2 actor(s): BP_Laser_C x2]
    /test/Fire            5 args     (vec3 Dir, name Mode, float Power)
    /test/K2_DestroyActor 0 args     ()
    ...
```

### `OSCulator.List Custom`

**Blueprint-authored functions only.** The view you will actually live in — your own
events, without the inherited noise.

### `OSCulator.List Actors`

Tags and the actors under them, no functions. Answers "is my tag registered at all?"

### `OSCulator.List <tag>`

One tag in full. `OSCulator.List laser`.

### `OSCulator.Rescan` — editor too

Forgets the registry and walks the open level again, then says what it saw — including
what it declined.

```
[OSCulator] Rescanned: 812 actor(s) walked, 4 carrying a prefixed tag, 3 tag(s) registered.
[OSCulator] Run 'OSCulator.List Custom' to see the callable surface.
```

The editor world never runs `BeginPlay`, so nothing there populates the registry on its
own schedule. Dragging in a tagged actor and editing an actor's **Tags** both update it
live now, but this is the explicit "look again" for everything else — a Blueprint you
just compiled, a level you just streamed in.

Two lines only appear when something is wrong:

| Line | Means |
| --- | --- |
| `N actor(s) are tagged with something that is not prefixed 'OSC_'` | Nothing matched, but the level *is* tagged. Almost always **Registry → Tag Prefix** against what is typed on the actors |
| `not inherited by the instance: <actor> -- 'OSC_x' is on the class defaults only` | The tag is on the Blueprint, not on the actor in the level. See [Tagging](SETUP.md#tag-it) |

### `OSCulator.Export [path]`

Writes the whole surface to JSON. Defaults to `Saved/OSCulator.json`. Useful for
generating sender configurations outside Unreal.

---

## Testing dispatch without a sender

### `OSCulator.Send [-lenient] /tag/Function [args...]`

Synthesises a message and dispatches it exactly as the network would.

```
OSCulator.Send /test/Fire 0 0 1 burst 0.5
OSCulator.Send /test/Stop
OSCulator.Send -lenient /test/Fire 0.5
```

Numeric tokens become floats, everything else becomes a string. Reports how many
actors were called, or the rejection reason:

```
[OSCulator] /test/Fire expects 5 args (vec3, name, float) -- got 4. Ignored.
```

`-lenient` uses MIDI's argument policy — fill what is supplied, zero the rest —
so you can test how an event behaves when triggered from a pad.

---

## OSC status

### `OSCulator.Status`

Whether the socket is listening, and the counters.

```
[OSCulator] Listening on 0.0.0.0:8000
  packets   received 4821, malformed 0, from blocked senders 0
  messages  drained 4821, coalesced away 4104, dispatched 717
  arrived but matched nothing:
    /laser/Fyre
```

**"arrived but matched nothing"** is the answer to *"I'm sending it and nothing
happens"* — if the address is listed, it reached Unreal and could not be routed.
Usually a typo or a tag that is not on any actor.

**received far exceeding dispatched** is coalescing working, not packet loss.

If it says *enabled but NOT listening*, the bind failed — the log says why, usually
another application on the port.

### `OSCulator.OSCMonitor 0|1|2`

Prints incoming OSC as it is dispatched. The OSC counterpart of `OSCulator.MIDIMonitor`.

| | |
| --- | --- |
| `0` | off, and the default |
| `1` | the **first** message on each distinct address, and nothing after it |
| `2` | every message |

```
OSC in (new address): /laser/Fire 0 0 1 "burst" 0.5   -> 2 actor(s)   from 192.168.1.40
OSC in (new address): /cube/Spin 0.5                  -> 1 actor(s)   from 192.168.1.40
OSC in (new address): /laser/Fyre 1                   -> 0 actor(s)   from 192.168.1.40
```

**Use `1`.** It answers the question you nearly always have — *did the address I am
sending ever arrive, and did it reach anything* — and it costs one set lookup per
message, so it is safe to leave on while you work. `2` is for a single gesture with the
stream otherwise idle: at 10–20 messages a frame it is a wall of identical lines that
buries the one address you were looking for.

`-> 0 actor(s)` means it arrived and routed to nothing. The monitor does not say why —
the router already does, **once per distinct address**, and repeating it per message is
exactly what the throttle exists to prevent. The reason is in the log above, and the
address is also listed under *arrived but matched nothing* in `OSCulator.Status`.

Three outcomes other than a count:

| | |
| --- | --- |
| `-> answered, not routed` | the reserved `/_` namespace, e.g. `/_describe` |
| `-> clock tempo` | the address configured as the clock's tempo input |
| `N message(s) coalesced away this frame` | level `2` only, once per frame |

That last line matters: a coalesced message never reaches a function at all, so the
monitor printing one value where the sender sent sixty is coalescing working, not a lost
message. See [Coalescing](SETUP.md#coalescing).

Toggling it off and back on starts the level `1` address list over. The list is capped at
128 distinct addresses, after which it says so once and stops — addresses arrive from the
network, and a sender sweeping a generated address would otherwise grow it for the life
of the process.

Logged in the **drain**, on the game thread, never on the receive thread: a log call
between two reads of a UDP socket is how a queue overflows.

Off by default. Leave it off during a show.

---

## MIDI

### `OSCulator.MIDIDevices` — editor too

Every MIDI device this machine can see, with exact names. **Copy these verbatim** into
Project Settings; there is no picker.

```
[OSCulator] MIDI devices. Copy a name verbatim into Project Settings.
  Inputs (3):
    "UE2TD"
    "TD2UE"
    "Elektron TM-1"   [open by OSCulator]
  Outputs (2):
    "LPD8"
    "loopMIDI Port"   [already in use]
```

Inputs are enumerated through OSCulator's own PortMidi instance — the one that will
actually open them — so `[open by OSCulator]` means this session holds it. Outputs come
from the engine's MIDIDevice plugin, and its "already in use" flag only ever sees the
current process.

`Inputs (0)` means nothing is reaching Unreal at all — a driver or cabling problem, not a
settings one.

### `OSCulator.MIDIStatus` — editor too

Which ports are open and what they are filtering, every active map with every binding,
and the counters.

```
[OSCulator] MIDI devices open: 2
  'Elektron TM-1': queue 1024, dropping clock, timecode, transport, sysex, listening on all channels except 10
     unused messages 0, queue overflows 0
  'Midi Fighter Twister': queue 1024, dropping NOTES, clock, timecode, transport, sysex, listening on all channels
     unused messages 0, queue overflows 0
  Map: DA_Pads [Elektron TM-1] (4 binding(s), 3 assigned, 1 unassigned)
    laser/Aim   <- Note  ch 1  C1 (36)
    laser/Fire  <- Note  ch 1  C#1 (37)
    laser/Level <- CC    ch 1  #7 (+1 more)
    laser/Stop   -- unassigned
  messages  received 12, dispatched 9, unmapped 3
```

Reading it:

| Symptom | Means |
| --- | --- |
| received 0 | The device is not delivering. Check `OSCulator.MIDIDevices` |
| received > 0, unmapped > 0 | Delivering, but on an input no binding claims |
| dispatched 0 with bindings present | The tag has no actors, or the function is not exposed |
| `-- unassigned` | The binding exists but nothing fires it yet. Normal after Auto-Map |
| queue overflows > 0 | Messages were lost. Raise the queue size, or narrow **that device's** Listen For or Ignored Channels |
| unused messages climbing | Something is getting through that device's Listen For that no binding uses |
| `PERFORMANCE MODE is on` | Learn will not arm and recompiles are not tracked. Untick Performance Mode in Project Settings to go back to editing |

### `OSCulator.MIDIValidate` — editor too

Checks every active map against the open level, then reports inputs claimed by more than
one binding **across all maps at once** — which is the one thing the asset's own
**Validate Against Level** button cannot see, since it only knows its own bindings.

```
[OSCulator] DA_Pads: 3 ok, 1 broken, 0 for another level, 1 unassigned. See the log for detail.
[OSCulator] DA_Knobs: 6 ok, 0 broken, 0 for another level, 0 unassigned. See the log for detail.
[OSCulator] Inputs driving more than one binding (legal -- one pad, several actors -- but worth a look):
  ch 1 note 36:
      laser/Stop   [DA_Pads, Elektron TM-1]
      cube/Flash   [DA_Knobs, any device]
```

Sharing an input is a feature, so this reports rather than warns. Two per-device maps
reusing the same note number are **not** listed — their devices cannot both match one
message. A source with no device is listed against everything on that number, because an
empty device accepts any.

The per-map detail — which functions are missing, and which the level exposes that
nothing claims — goes to the log, where it is too long for a console line.

### `OSCulator.MIDIRestart`

Closes and reopens the MIDI devices, re-reading settings. Settings edits already do this
automatically; this is the manual retry.

Also the way to **release a port back to another application** without restarting —
disable MIDI input, restart, and the port is free.

### `OSCulator.MIDIMonitor 0|1`

Logs **every** incoming message with its device, raw channel, number and resolved note
name.

```
MIDI in [Elektron TM-1]: channel=1 note=37 (C#1) velocity=100 on
MIDI in [Midi Fighter Twister]: channel=1 CC=7 value=64
MIDI in [rtpMIDI]: channel=1 program=5
```

This is the tool for *"is the note I think I am sending the note that arrives?"* — which
no amount of reading the map can settle, because senders disagree about whether their
note labels are 0-based or 1-based. TouchDesigner's are 1-based, so its `n38` arrives
here as 37.

The device name is printed because a binding can require one, and a mapping that never
fires is often a mapping listening to the wrong box.

Off by default. Leave it off during a show.

---

## Performance

### `stat OSCulator`

The plugin's own frame cost, separated from whatever your Blueprint events do.

```
Cycle counters                        InclusiveAvg  InclusiveMax
  Drain (total per frame)                   0.04 ms       0.08 ms
  Dispatch (incl. called event)             0.03 ms       0.06 ms
  Coalesce                                  0.00 ms       0.01 ms

Counters                              Average
  Messages drained                       1.55
  Messages coalesced away                0.90
  Messages dispatched                    0.65
```

The nesting is the useful part. **Dispatch contains `ProcessEvent`**, so your event
body — Print String included — is inside that number. The **gap between Drain and
Dispatch** is OSCulator's own routing cost, and it should be tiny.

Under an overloaded stream, **drained** should climb while **dispatched** stays near
your frame rate. If dispatched tracks drained 1:1, coalescing is not engaging.

`stat unitgraph` alongside it shows whether any of this reaches the frame time. Note
that if you run your sender on the same machine, *its* CPU cost shows up in
`stat unit` too and is easily mistaken for OSCulator's.
