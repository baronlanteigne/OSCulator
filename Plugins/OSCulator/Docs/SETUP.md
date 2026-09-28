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
| Listen For | Which message types reach the queue at all. See below |
| MIDI Input Channels | Listen only on these, 1–16. Empty means all sixteen |
| MIDI Input Queue Size | Messages buffered between frames. Default 1024 |
| MIDI Maps | The mapping assets. All of them are live at once |

### OSCulator reads MIDI itself

Input does **not** go through Unreal's MIDIDevice plugin. That plugin has three defects
that cannot be reached from outside it: it opens every stream with PortMidi's default
filter (active sensing and nothing else), it passes a zeroed buffer size to
`Pm_OpenInput` so the queue is never the size anyone asked for, and it exposes neither
the message filter nor the channel mask. OSCulator runs its own PortMidi instance and
owns the whole path: open, filter, mask, read, close.

MIDI **output** still uses the engine plugin, which has none of these problems. The two
PortMidi instances share no state — verified by making the engine terminate and restart
its copy while an OSCulator stream was open and reading, which it did not notice.

### Listen For, channels, and the queue

These three settings exist because of one fact: **the input queue is drained once per
game-thread frame**. Any hitch — pressing Play, a shader compile — is a window in which
nothing drains and the queue fills. When it fills, PortMidi discards everything in it
and reports an overflow, so a flood of messages nobody wants takes your notes with it.

Measured on a sequencer running at tempo: **MIDI clock was 68% of all traffic** (340 of
503 messages in six seconds), with control change another ~70/second once modulation was
running. Clock alone will overflow a small queue within seconds of pressing Play.

So:

- **Listen For** drops whole message types at the port, before they are ever queued.
  Ticked means *send me this*. Clock, transport and sysex start unticked because nothing
  in OSCulator can act on them; everything carrying a playable value starts ticked.
- **MIDI Input Channels** is the sharper tool. A MIDI interface carries a whole rig
  while a map answers to two or three channels — everything on the others is discarded
  at the port. Unlike unticking Control Change, this costs you nothing you wanted.
  Entries outside 1–16 are ignored with a log line, and a list containing nothing valid
  falls back to every channel rather than silently muting the device.
- **Queue Size** is the headroom. At ~100 messages/second, 1024 absorbs about ten
  seconds of stalled game thread.

If an overflow does happen you get a log line naming the device, the queue size, and
what to change. `OSCulator.MIDI` shows a per-port overflow count.

MIDI Learn temporarily ignores the channel list while a source is armed, so you can
still learn an input from a channel you normally mute.

### Devices, and sharing them with other applications

Devices are opened **by name**, never by enumeration order — device indices move when
anything is plugged in. A device that is missing or refused is skipped with a log line
rather than taking the others down with it.

Whether another application can hold the same port at the same time **depends on the
driver**. An Elektron TM-1 was verified open in TouchDesigner and Unreal simultaneously,
both receiving, while plenty of Windows MIDI inputs refuse a second client outright.
PortMidi's own "in use" flag only ever sees the current process, so a conflict cannot be
detected before trying — if one occurs you get:

```
MIDI input device 'X' could not be opened (...). Another application may be holding it.
```

Each client gets its own queue, so another application's traffic cannot overflow yours.

### The map asset

Content Browser → right-click → **Miscellaneous → Data Asset** → pick
**OSCulator MIDI Map**.

Maps are written **target-first**: you list what is controllable, then assign inputs to
it. A function that no longer exists is simply absent from the list rather than lurking
as a row pointing nowhere.

```
Default Device               every source here means this device unless it says otherwise

Auto Map Rules[]             per-tag layout, used by Auto-Map. See below
├─ Tag / Device / Channel
└─ First Note / First CC

Bindings[]
├─ Tag                       "laser", from an actor tagged OSC_laser
├─ Function Name             called on every actor carrying the tag
├─ Sources[]                 everything that fires it. None is legal and inert
│  ├─ Device                 empty inherits Default Device; empty there means any
│  ├─ Channel                1-16
│  ├─ Type                   Note or Control Change
│  ├─ Note / CC Number       "C3", "C#2", a bare "61", or a CC number
│  └─ Learn                  tick to arm, then play or turn something
├─ Remap                     on by default: 0-127 becomes Out Min..Out Max
├─ Out Min / Out Max         default 0..1. Out Min > Out Max inverts
├─ Send Source Number        prepend the note or CC number, raw, as a first argument
└─ On Note Off               also fire on release, with a raw value of 0
```

**Several maps are live at once**, so how you split them is your choice: one asset per
device, one per show, or one for everything. A per-device asset sets `Default Device`
once at the top and leaves every source's Device empty.

A binding driven from two controllers is better as **one binding with two sources** than
as two bindings in two assets — the target and its value settings stay in one place
instead of being kept in sync by hand.

**Sharing an input is legal.** Two bindings claiming the same channel and note both
fire, which is how one pad drives two different actors. `OSCulator.MIDIValidate` lists
every input that drives more than one binding, across all active maps, so an accidental
overlap is visible.

### What the function receives

One value, remapped, in the first parameter. Everything after it keeps the zeroes the
initialised frame gave it — which is why a function you intend to drive from MIDI should
take its MIDI-relevant parameter first.

A float landing in an `int32` parameter is **truncated toward zero**, so "remap to 0–10
and call a function taking an int" works with no extra setting. With **Send Source
Number** on, the note or CC number arrives first, unremapped, and the value second.

Arguments are always accepted leniently: a zero-argument trigger simply ignores the
value it is handed.

### Auto-Map

**Auto-Map From Level** runs in two phases:

1. **Lists** every Blueprint-authored function of every tagged actor in the open level
   as a binding, unassigned. This is what makes the asset a complete inventory of what
   could be controlled. Blueprint-authored only — a fully exposed actor drags in a
   couple of hundred inherited engine functions.
2. **Assigns** an input to any unassigned binding whose tag has an **Auto Map Rule**,
   handing out numbers upward from the rule's First Note / First CC and stepping over
   anything already claimed.

Note or CC is chosen from the signature: a function taking nothing is a trigger and gets
a note; one whose first parameter is a `float` or `int` is continuous and gets a
controller; anything else gets a note.

**It is additive only.** An existing binding or source is never moved, renumbered or
rewritten. This matters more than it sounds: a Blueprint class's function map does not
iterate in a stable order across recompiles, so anything that rewrote rows would
reshuffle which pad triggers what every time you compiled — and you would find out
mid-show. Functions are merged alphabetically for the same reason.

### Validate and Prune

**Validate Against Level** marks every binding and logs two lists: bindings whose
function no longer exists, and functions the level exposes that nothing claims. A
renamed function appears in **both**, so fixing it means retyping one name — and the
binding keeps its sources, remap range and note-off setting. It also runs automatically
at the end of Auto-Map.

**Prune Missing Functions** deletes bindings whose tag is present in the level but whose
function is not. It re-validates first, so it can never act on a stale judgement.
Bindings whose *tag* is missing are never touched — a tag absent from this level is
probably present in another one.

A binding mapped by hand to a **native C++** function is judged correctly: validation
checks against everything exposed, not just the Blueprint-authored subset that Auto-Map
offers. Otherwise Prune would delete working mappings.

### Learn

Tick **Learn** on a source, then play a note or turn a knob. It captures the device, the
channel, whether it was a note or a CC, and the number — one gesture fills the whole
row. Only one source across the asset can be armed at a time; ticking a second moves the
arming. The flag is transient, so an armed source is never saved in that state.

Learn is the one thing OSCulator does at edit time, and it works because the devices are
already open — it redirects the next message rather than opening a second listener.

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

**Whether a MIDI input port can be shared depends on the driver.** Plenty of Windows
inputs refuse a second client, but not all: an Elektron TM-1 was verified open in
TouchDesigner and Unreal at once, both receiving. If yours does refuse, use a virtual
port pair such as loopMIDI. OSCulator releases its ports immediately when you disable
MIDI input or remove a device, so you can hand one back without restarting.

**TouchDesigner's `n` labels are 1-based.** TD sends `label - 1` on the wire, so
`n38` is MIDI note 37. To hit a map row showing `(37)`, send `n38`. Confirm with
`OSCulator.MIDIMonitor 1`, which prints the note number that actually arrived.

**Settings take effect immediately.** Editing a device name, a port or a target
reopens the affected transport — no editor restart.

**Everything runs in PIE and packaged builds.** The editor gets a registry only so that
MIDI Auto-Map and Validate can ask what a level exposes; nothing dispatches there. MIDI
Learn is the single edit-time exception.

**Dispatch is game-thread only**, drained before actor ticks, so a message received
this frame affects this frame rather than the next.
