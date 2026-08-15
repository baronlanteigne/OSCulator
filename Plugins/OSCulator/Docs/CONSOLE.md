# OSCulator — Console Reference

Open the console with `` ` `` (backtick).

Most commands need a **playing world** (PIE or packaged), because the registry and the
transports live there. The two exceptions are marked **editor too** — they work
without entering play.

---

## Discovering what is callable

### `OSCulator.List`

Everything exposed, grouped by tag. With `Expose All Functions` on this includes a
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

---

## MIDI

### `OSCulator.MIDIDevices` — editor too

Every MIDI device this machine can see, with exact names. **Copy these verbatim** into
Project Settings; there is no picker.

```
[OSCulator] MIDI devices. Copy a name verbatim into Project Settings.
  Inputs (1):
    "LPD8"
  Outputs (2):
    "LPD8"
    "loopMIDI Port"   [already in use by another application]
```

`Inputs (0)` means nothing is reaching Unreal at all — a driver or cabling problem,
not a settings one.

### `OSCulator.MIDIStatus` — editor too

Which devices are open, which map is active, every mapping in it, and the counters.

```
[OSCulator] MIDI devices open: 1
  Map: DA_LiveMap (1 channel(s), 3 mapping(s))
    channel 1 -> /test
      C1     ( 36) -> Aim
      C#1    ( 37) -> Fire
      D1     ( 38) -> Stop
  notes  received 12, dispatched 9, unmapped 3
```

Reading the counters:

| Symptom | Means |
| --- | --- |
| received 0 | The device is not delivering. Check `OSCulator.MIDIDevices` |
| received > 0, unmapped > 0 | Delivering, but on a channel or note the map does not cover |
| dispatched 0 with mappings present | The tag has no actors, or the function is not exposed |

### `OSCulator.MIDIRestart`

Closes and reopens the MIDI devices, re-reading settings. Settings edits already do
this automatically; this is the manual retry.

Also the way to **release a port back to another application** without restarting —
disable MIDI input, restart, and the port is free.

### `OSCulator.MIDIMonitor 0|1`

Logs **every** incoming note with its raw channel, note number and resolved name.

```
MIDI in: channel=1 note=37 (C#1) velocity=100 on
```

This is the tool for *"is the note I think I am sending the note that arrives?"* —
which no amount of reading the map can settle, because senders disagree about whether
their note labels are 0-based or 1-based. TouchDesigner's are 1-based, so its `n38`
arrives here as 37.

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
