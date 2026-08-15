# OSCulator — Installation and Configuration

Call Blueprint actor events over OSC and MIDI, with no per-actor wiring.

Tag an actor `OSC_laser`. Send `/laser/Fire 0 0 1 burst 0.5`. OSCulator finds every
actor with that tag, reads `Fire`'s parameter list by reflection, and fills it from
the message in order.

**The function signature is the schema.** The sender declares no types. It sends the
right count of values in the right order, and the receiving signature decides what
they mean.

---

## 1. Install

1. Copy `Plugins/OSCulator` into your project's `Plugins` folder.
2. Regenerate project files and build.
3. The plugin declares **MIDIDevice** as a dependency, so it is enabled automatically.

Three modules load:

| Module | Contains |
| --- | --- |
| `OSCulatorCore` | The registry, the marshaller, settings, `OSCulator.List` |
| `OSCulatorOSC` | OSC codec, receive thread, senders, Blueprint nodes |
| `OSCulatorMIDI` | Note names, the map asset, device I/O, Blueprint nodes |

All settings live in **Project Settings → Plugins → OSCulator**.

---

## 2. Exposing an actor

### Tag it

Select an actor, find **Actor → Tags** in the Details panel, and add one entry:

```
OSC_laser
```

The prefix is stripped, so this actor answers at `/laser/<FunctionName>`.

- Matching is **case-insensitive**. `OSC_Laser` and `osc_laser` are the same tag.
- An actor may carry **several** prefixed tags and answer under all of them.
- **Several actors may share a tag.** All of them fire. If only some have the
  function, only those fire — that is not an error.
- Tags set in a Blueprint's **Class Defaults** work for runtime-spawned actors.
  A tag added by gameplay code *after* spawning arrives too late to register.

The prefix is configurable (**Registry → Tag Prefix**, default `OSC_`).

### Address format

```
/<tag>/<FunctionName>
```

Exactly two segments. No wildcards, no deeper nesting.

### Which functions are exposed

By default **everything on the class**, including inherited engine functions like
`K2_SetActorLocation` and `K2_DestroyActor`. That is a deliberate trade: free
transform control over tagged actors, in exchange for no per-function opt-in.

To narrow it, set **Function Exposure → Function Prefix**. With `OSC_` set, only
functions named `OSC_Fire` are exposed, and they answer at `/laser/Fire`.

`ExecuteUbergraph_*` is **always excluded** and cannot be opted back in. It is the
Blueprint compiler's entire event graph behind one `int32` bytecode offset, so
calling it with an arbitrary integer jumps into the middle of your graph.

Use `OSCulator.List Custom` to see just your own Blueprint events.

### Parameter types

What each parameter type costs on the wire. Values are consumed in order.

| Type | Values | Order |
| --- | --- | --- |
| float, int, bool, byte | 1 | |
| string, name, text | 1 | |
| enum | 1 | index or name |
| vec2 | 2 | x, y |
| vec3 | 3 | x, y, z |
| rotator | 3 | pitch, yaw, roll |
| color | 4 | r, g, b, a |
| quat | 4 | x, y, z, w |
| transform | 9 | loc3, rot3, scale3 |
| array | all remaining | |

A parameter of any other type — an object reference, an unlisted struct — excludes the
whole function from the registry. `OSCulator.List` prints each signature with these
labels, so it always agrees with what the marshaller will do.

### Variable-length messages

A **trailing array** parameter swallows every argument the fixed parameters did not
take, so one event can serve messages of different lengths:

```
MatParam(FName ParamName, float Interp, TArray<float> Value)
           └─ arg 0        └─ arg 1      └─ args 2..n
```

The array must be the **last** parameter, and there can be only one. A parameter after
an array is rejected at registration, since the split would be ambiguous. The element
type must cost one value, so `TArray<float>` works and `TArray<FVector>` does not.

See [HELPERS.md](HELPERS.md) for nodes that interpret the array once it arrives.

---

## 3. OSC input

**Project Settings → Plugins → OSCulator → OSC Input**

| Setting | Default | Notes |
| --- | --- | --- |
| Enable OSC In | on | Unchecked opens no socket and starts no thread |
| Bind Address | `0.0.0.0` | Every interface. `127.0.0.1` hears only this machine |
| Listen Port | `8000` | |
| Allowed Sender IPs | empty | Empty accepts all. Listing addresses drops everything else |
| Receive Buffer Size | 1 MB | Generous on purpose, so a burst does not drop packets |
| No Coalesce Addresses | empty | Addresses where every hit matters |

**One socket serves every sender.** UDP is connectionless and `0.0.0.0` binds all
interfaces, so any number of machines can send to the same port at once. There is no
need for multiple inputs. Restrict *which* machines with Allowed Sender IPs.

### Argument counts

Incoming OSC uses **Strict** policy, which means **at least** as many arguments as
the signature consumes:

- **Too few** is rejected, and the log prints the expected signature:
  `/laser/Fire expects 5 args (vec3, name, float) -- got 4. Ignored.`
- **Too many** is fine. The surplus is discarded and the call goes ahead.

The asymmetry is deliberate. Too few means running on values you never sent; too
many means running on exactly what was asked for with ignorable data trailing.
Senders append surplus routinely — a TouchDesigner CHOP emits every channel it has
and cannot emit none — so refusing it would make zero-argument triggers unreachable.

### Coalescing

Within one frame, repeated messages to the same address collapse to the **last**
value, at the position the address first appeared. A 60 Hz stream across 20
parameters becomes 20 dispatches per frame rather than 1200.

**Triggers never coalesce.** A function taking zero arguments fires on every hit,
even when the sender attaches a surplus value it ignores. Use **No Coalesce
Addresses** for anything else where every hit matters.

### Reserved namespace

Addresses beginning with `/_` are control traffic, not actor addresses. They are
never routed and never warn. `/_describe` is OSCulator's own; TouchDesigner's OSC Out
CHOP emits `/_samplerate` every frame.

---

## 4. OSC output

**Project Settings → Plugins → OSCulator → OSC Output**

Off by default. **OSC Targets** is a list, because sending is point-to-point —
reaching three devices means three targets:

| Field | Meaning |
| --- | --- |
| Name | Referenced from the Send node's Target dropdown |
| Host | IP or hostname |
| Port | |
| Enabled | Silences one destination without deleting its configuration |

### Blueprint nodes

```
Send OSC          (Target, Address, Args)   -> int32 targets reached
Send OSC (Floats) (Target, Address, Values) -> int32
Is OSC Output Ready                         -> bool
```

Leave **Target** as `(all)` to reach every enabled target, or pick one by name.
Naming a target that is not configured **warns** rather than silently doing nothing.

**Send OSC (Floats)** takes a plain float array and is the easy path for the common
case. **Send OSC** takes `FOscuValue`s and can mix types.

Six autocast conversions exist (float, int, bool, string, name, vector), so a float
can be wired straight into a `Make Array` feeding Send OSC. **Connection order
matters:** attach Make Array's *output to the Send node first* so its wildcard
resolves to `FOscuValue`, then wire your values into the element pins. Wiring a
float in first makes it an array of floats and no autocast is consulted.

An `FVector` flattens to **three floats** on the wire. A float, a float and a vector
is five values arriving.

### Self-describe

Send `/_describe` and OSCulator replies with its callable surface — one message per
Blueprint-authored function — so a patch can build its own senders:

```
/_describe/begin
/_describe/function  <address> <tag> <function> <argCount> <signature> <variadic>
/_describe/end       <count>
```

Include an **integer argument naming your listen port** and the reply comes straight
back to you. Without it, the reply goes to your configured targets — a sender's
source port is ephemeral, not the port it listens on, so there is no way to guess.

---

## 5. MIDI input

**Project Settings → Plugins → OSCulator → MIDI Input**

| Setting | Notes |
| --- | --- |
| Enable MIDI In | Off by default |
| MIDI Input Device Names | Exact names. Run `OSCulator.MIDIDevices` to list them |
| Middle C Octave | `3` gives C3 = 60 (Ableton, Logic). `4` gives C4 = 60 (scientific) |
| MIDI Map | The mapping asset |

### One map, every device

**All listed input devices merge into one stream and share one map.** Two controllers
both sending channel 1 note 60 will both fire the same mapping. There is no per-device
map and no per-device routing.

If you need two controllers behaving differently, **put them on different MIDI
channels** and give each channel its own entry in the map.

Devices are opened **by name**, never by enumeration order — MIDI exclusivity
conflicts between applications are real. A device that is missing or already in use is
skipped with a log line rather than taking the others down with it.

### Creating the map

Content Browser → right-click → **Miscellaneous → Data Asset** → pick
**OSCulator MIDI Map**.

```
Channels[]
├─ Channel   1-16          the MIDI channel, as every DAW displays it
├─ Tag       "laser"       prefix already stripped
└─ Notes[]
   ├─ Note          "C3"   or "C#2", "Db2", or a bare "61"
   ├─ Resolved Note 60     read-only, always visible
   ├─ Function Name "Fire"
   ├─ Mode          Normalized01
   ├─ Fire On Note Off     also fire on release, with velocity 0
   └─ Learn                tick to arm, see below
```

A channel carries the tag, so one channel maps to one actor group and its notes map
to that group's functions. `(channel, note)` becomes `/laser/Fire`.

Anything not in the map is **ignored entirely** and passes through untouched, for
other systems to interpret however they like.

**Resolved Note is always shown** because a name alone is ambiguous — note 60 is C3
in Ableton and C4 in scientific pitch notation, and the MIDI specification defines no
octave naming at all. Names are normalised to sharps when edited, so `Db2` becomes
`C#2`.

### Value modes

| Mode | Sends |
| --- | --- |
| Raw Velocity | velocity, 0–127 |
| **Normalized01** | velocity / 127. The default, since everything else here is 0–1 |
| Note And Velocity | two arguments: the note number, then normalised velocity |

### What the function receives

MIDI dispatch always uses **Lenient** policy: it fills what it has and leaves the rest
**zero**. MIDI supplies one value regardless of what the signature wants.

So a five-argument `Fire(FVector Dir, FName Mode, float Power)` fired from a pad gets
velocity in `Dir.X` and zeroes everywhere else.

**Put the velocity-relevant parameter first**, and read anything else from actor
variables inside the event. Blueprint parameter defaults are *not* applied — they live
in editor-only metadata and are baked into the call node, so `ProcessEvent` never sees
them. Unfilled means zero, full stop.

### Auto-populate

Open the map asset and click **Auto-Populate From Level**. It walks every tagged actor
in the open level and maps each Blueprint-authored function to the next free note from
36 upwards, assigning channels in order from 1.

Two rules make this safe:

- Functions are merged **alphabetically**, not in reflection order. A class's function
  map does not iterate stably across Blueprint recompiles.
- It is **additive only**. An existing row is never moved, renumbered or rewritten.

So running it twice changes nothing, and hand-edited rows survive. It runs only when
clicked; your own mapping decisions always win.

### Learn

Expand **Channels → [n] → Notes → [n]** and tick **Learn** on the row. Play a note and
the row rewrites itself, then disarms. Only one row can be armed at a time.

Learn writes the **note only**. The row's channel must already be correct — moving the
row would drag its siblings to a different channel too. If the learned note arrives on
a different channel you get a warning saying so.

Learn works at edit time, which is the one thing OSCulator does outside play. It needs
the input device already open.

---

## 6. MIDI output

**Project Settings → Plugins → OSCulator → MIDI Output**

Off by default. **MIDI Output Device Names** is a separate list from the input one —
input is what OSCulator listens to, output is what it sends to. The Send node's
dropdown lists **outputs only**.

### Blueprint nodes

```
Send MIDI Note           (Device, Channel, Note, Velocity, Duration Seconds)
Send MIDI Note On        (Device, Channel, Note, Velocity)
Send MIDI Note Off       (Device, Channel, Note)
Send MIDI Control Change (Device, Channel, Control Number, Value)
Release All MIDI Notes
Is MIDI Output Ready                -> bool
Get Pending MIDI Note Off Count     -> int32
MIDI Note From Name  ("C3")         -> 60
MIDI Note To Name    (60)           -> "C3"
```

**Send MIDI Note** sends the note on now and the note off automatically after
**Duration Seconds** — one node, no Delay required. A duration of zero or less sends
the note on and schedules nothing.

The release is scheduled at **engine level, not on a world timer**, so a note started
in play is still released if you stop play before its duration elapses. Everything
outstanding is flushed on End PIE and on shutdown. A note left on is a stuck note on
real hardware, and nothing inside Unreal can silence it afterwards.

**Release All MIDI Notes** is the panic button.

---

## 7. Things worth knowing

**Channels are 1–16 everywhere** in OSCulator — in the map asset, in the Blueprint
nodes, and in the monitor output. Matching every DAW. The wire protocol's 0–15 is
converted internally, in exactly one place.

**Windows MIDI input ports are single-owner.** Unreal and TouchDesigner cannot both
hold the same physical port. Use a virtual port pair such as loopMIDI if you need
both. OSCulator releases its ports immediately when you disable MIDI input or remove a
device, so you can hand one back without restarting.

**TouchDesigner's `n` labels are 1-based.** TD sends `label - 1` on the wire, so
`n38` is MIDI note 37. To hit a map row showing `(37)`, send `n38`. Confirm with
`OSCulator.MIDIMonitor 1`, which prints the note number that actually arrived.

**Settings take effect immediately.** Editing a device name, a port or a target
reopens the affected transport — no editor restart.

**Everything runs in PIE and packaged builds.** The editor gets a registry only so
that MIDI auto-populate can ask what a level exposes; nothing dispatches there.
MIDI Learn is the single edit-time exception.

**Dispatch is game-thread only**, drained before actor ticks, so a message received
this frame affects this frame rather than the next.
