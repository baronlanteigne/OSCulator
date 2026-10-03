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

## 4b. Performance Mode

**Project Settings → Plugins → OSCulator → Performance**

| Setting | Notes |
| --- | --- |
| Performance Mode | Off by default. Switches off everything that exists only to help you *author* the project |

The authoring features already vanish from a **packaged** build — they live behind
`WITH_EDITOR`. But running a show **from the editor**, which is the normal way to do it,
has every one of them live. This is the switch that says *I have finished editing; stop
paying for the editing.*

What it turns off:

- **MIDI Learn.** It will not arm, and the per-message check that looks for an armed row
  is skipped. Arming is **refused with a log line** and the tickbox is cleared, rather
  than silently doing nothing — a Learn button that quietly stopped working would be a
  miserable thing to debug.
- **Blueprint-recompile tracking.** Nothing recompiles during a show, and a cache rebuild
  triggered mid-performance is a hitch nobody asked for.
- **Per-message parameter-slot resolution.** A binding that names a parameter normally
  re-resolves that name against the live signature on *every message*, so a recompile
  which reorders parameters is followed. With recompiles ruled out, the answer is
  resolved once and remembered.

What it does **not** touch: anything that affects what your show does. No filter, no
channel, no mapping, no value shaping. **Turning it on must never change a value that
reaches a function** — only how much work was done to get it there. There is a test
asserting exactly that, running the same gestures in both modes and comparing value for
value.

`OSCulator.MIDIStatus` says so at the top when it is on, so "why won't Learn arm" has a
visible answer.

**Validate and Auto-Map are unaffected** either way: they only run when their button is
pressed, so they cost nothing during a show regardless.

### What was costing anything in the first place

Worth being concrete, because most of it was already free:

| | Editor, idle | Editor, Learn armed | Packaged |
| --- | --- | --- | --- |
| Learn | one weak-pointer check per message | lifts every port's channel mask to all 16, so more traffic reaches the once-per-frame queue | absent |
| Recompile tracking | nothing until a compile happens | — | absent |
| Named parameter slots | resolved per message | — | **also resolved per message** until now |

That last row was the real one, and it was not editor-only. Resolving a slot walked the
tag's actor list and allocated twice per call, up to twice per matched binding per
message. Performance Mode removes the repetition; the allocations were removed outright,
in both modes, because they were never worth anything.

## 5. MIDI input

**Project Settings → Plugins → OSCulator → MIDI Input**

| Setting | Notes |
| --- | --- |
| Enable MIDI In | Off by default |
| MIDI Input Devices | One entry per controller: its exact name, and what to drop from it. Run `OSCulator.MIDIDevices` to list names |
| └ Listen For | Which message types that device gets to send. Ticked means *send me this*. See below |
| └ Ignored Channels | Channels to **discard** from that device, 1–16. Empty — the default — keeps all sixteen |
| Middle C Octave | `3` gives C3 = 60 (Ableton, Logic). `4` gives C4 = 60 (scientific) |
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

### Listen For, Ignored Channels, and the queue

Both filters are **per device**, alongside the one queue size they share. They exist
because of one fact: **the input queue is drained once per game-thread frame**. Any
hitch — pressing Play, a shader compile — is a window in which nothing drains and the
queue fills. When it fills, PortMidi discards everything in it and reports an overflow,
so a flood of messages nobody wants takes your notes with it.

Measured on a sequencer running at tempo: **MIDI clock was 68% of all traffic** (340 of
503 messages in six seconds), with control change another ~70/second once modulation was
running. Clock alone will overflow a small queue within seconds of pressing Play.

So:

- **Listen For**, per device, drops whole message types at the port, before they are
  ever queued. Ticked means *send me this*. Clock, transport and sysex start unticked
  because nothing in OSCulator can act on them; everything carrying a playable value
  starts ticked, so a controller you add and leave alone behaves.

  Per device because the noise is. Untick Clock on the sequencer flooding the wire with
  it without taking Program Change away from the box next to it that needs it — where a
  single project-wide setting forced the choice between dragging the filter tighter for
  everything or filtering nothing at all. PortMidi applies the filter per stream
  anyway; only the settings ever pretended otherwise.

  Unticking **Notes** is called out per device in the log, since with the filter per
  device the interesting case is one box out of three going deaf.
- **Ignored Channels**, per device, is the sharper tool. A MIDI interface carries a
  whole rig while a map answers to two or three channels — list the rest and they are
  discarded at the port. Unlike unticking Control Change, this costs you nothing you
  wanted: a channel you do not map is a channel you do not use.

  It is **per device** because a channel number means something different on each box —
  channel 10 is drums on one and a lighting desk on the next — so one project-wide list
  could only ever be the intersection of what every device happened to agree on.

  It is an **exclusion** list, not a selection, for two reasons. Every device works
  fully the moment you add it, and narrowing is the deliberate act. And a freshly added
  array row sits at `0`: read as "ignore nothing yet" that is harmless, where read as
  "listen to channel 0" it silently muted the hardware and looked exactly like a broken
  cable. Entries outside 1–16 get a log line and no effect; listing all sixteen is taken
  at face value — the device opens and hears nothing — but is called out in the log,
  since unticking the device is clearer at that point.
- **Queue Size** is the headroom. At ~100 messages/second, 1024 absorbs about ten
  seconds of stalled game thread.

If an overflow does happen you get a log line naming the device, the queue size, and
what to change. `OSCulator.MIDI` shows a per-port overflow count.

MIDI Learn lifts every device's Ignored Channels while a source is armed, so you can
still learn an input from a channel you normally mute. Each port goes back to its own
list when Learn is cancelled.

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
├─ Type                      Note, Control Change or Program Change, for the WHOLE row
├─ Sources[]                 everything that fires it. None is legal and inert
│  ├─ Device                 empty inherits Default Device; empty there means any
│  ├─ Channel                1-16
│  ├─ Note Range             notes only: match a span of pitches, not one note
│  ├─ Note / CC / Program #  "C3", "C#2", a bare "61", a CC number, or a program number
│  │                         with Note Range on, Note is the BOTTOM of the span
│  ├─ Note High              the top of the span, inclusive
│  └─ Learn                  tick to arm, then play, turn or send something
├─ Send Source Number        prepend the note, CC or program number, raw, as a first arg
├─ Value -> Parameter        which parameter the value drives, by name. Empty = the first
├─ Available Parameters      read-only: what this function offers, filled by Validate
│
│                            ---- hidden on a Program Change row ----
├─ Remap                     on by default: 0-127 becomes Out Min..Out Max
├─ Out Min / Out Max         default 0..1. Out Min > Out Max inverts
│
│                            ---- notes only ----
├─ Pitch -> Parameter        which parameter pitch drives, by name. Empty = not sent
├─ Remap Pitch               on by default: span position becomes Pitch Out Min..Max
├─ Pitch Out Min / Max       default 0..1. Untick Remap Pitch for the raw note number
└─ On Note Off               also fire on release, with a raw value of 0
```

**"The value"** means whichever one the row's `Type` carries: velocity for a note, the
controller value for a control change, the **program number** for a program change.

**Several maps are live at once**, so how you split them is your choice: one asset per
device, one per show, or one for everything. A per-device asset sets `Default Device`
once at the top and leaves every source's Device empty.

A binding driven from two controllers of the **same kind** is better as **one binding
with two sources** than as two bindings in two assets — the target and its value settings
stay in one place instead of being kept in sync by hand.

### Program Change

A program change carries **one** data byte where a note and a controller carry two.
There is no velocity, no controller value, nothing continuous at all — so a row driven by
one is a **trigger that happens to know which program arrived**.

```
Type            Program Change
Sources[0]
├─ Channel      1
└─ Program #    5
```

That row fires when program 5 arrives on channel 1. Nothing else does: a different
program, a different channel, or a *note* 5 all miss it, because the lookup is keyed on
the kind as well as the number.

- **The program number is the value.** It reaches the function the same way velocity or a
  controller value would, so `Value -> Parameter` points it wherever you like.
- **It is never remapped.** A program number is which patch, not how much — squashing
  patch 5 into `0.039` is never what anyone meant. `Remap` and its range are hidden
  outright on a Program Change row, and ignored even if an older asset had them set.
  This is the same reasoning `Send Source Number` has always carried.
- **Numbered 0–127, as it arrives on the wire.** Hardware disagrees with itself here —
  plenty of synths print patch *1* for program *0* — so OSCulator shows the number that
  arrived rather than guessing at your box's labelling.
- **Program 0 is a real program.** A row keyed on it fires, so the lowest patch on every
  box is mappable.

**One program can trigger as many functions as you like** — add a binding per function,
all keyed on the same program. They all fire, across as many actors as carry the tags.
That is the same shared-input behaviour notes and controllers have.

Make sure **Program Change** is ticked under that device's *Listen For*, or the message
never reaches the queue.

Two things it does not do. **Auto-Map never chooses Program Change** — it is a deliberate
"patch 7 fires this", not something to guess from a signature, and the auto-map rule
describes a first note and a first CC with nothing to say about programs. A row you set
to Program Change is left for you to number, and auto-map says so in the log rather than
overwriting your choice. And there is **no program change on the output side** yet; `Send
MIDI` covers notes and control change only.

### Note, Control Change or Program Change is per row

`Type` sits on the binding, not on each source, so **every source on a row is the same
kind**. The three behave differently in ways that reach the value settings: a note carries
a pitch and a release, a controller carries neither, and a program change carries no
continuous value at all. With the kind settled on the row, every setting on it is
meaningful for every source — and the ones that are not simply **vanish from the panel**:
pitch and On Note Off on anything but a note, Remap and its range on a program change.

That hiding is the reason the kind lives here. Unreal's `EditCondition` can only read
properties of its **own struct**, so a setting on the binding cannot see a `Type` that
lives inside `Sources[]`. Moving the one property made four of them hideable.

The cost: **one function driven by both a pad and a knob is two bindings**, not one
binding with two sources. Both still fire, and the capability is not gone — it is spelled
differently, and the two rows no longer share a remap range automatically.

Changing `Type` re-points every source on the row, and their numbers are **reinterpreted**
— note 36 becomes CC 36. Learn sets it from whatever you play, and warns if that re-points
other sources on the row.

Assets authored before this are migrated on load: each row adopts its first source's kind,
so a controller row stays a controller row. A row that held a genuine mix — legal before,
not now — resolves to its first source and says so in the log.

**Sharing an input is legal.** Two bindings claiming the same channel and note both
fire, which is how one pad drives two different actors. `OSCulator.MIDIValidate` lists
every input that drives more than one binding, across all active maps, so an accidental
overlap is visible.

### What the function receives

By default, **one value, remapped, in the first parameter**. Everything after it keeps
the zeroes the initialised frame gave it.

A float landing in an `int32` parameter is **truncated toward zero**, so "remap to 0–10
and call a function taking an int" works with no extra setting. With **Send Source
Number** on, the note or CC number arrives first, unremapped, and the value second.

Arguments are always accepted leniently: a zero-argument trigger simply ignores the
value it is handed, and a signature wanting more than MIDI supplies gets zeroes for the
rest.

### Note ranges, and two values at once

A note source normally matches **one** pitch. Tick **Note Range** and it matches a span
instead — `Note` becomes the bottom, `Note High` the top — and every pitch in between
fires the same binding. The span is contiguous; there is no way to punch holes in it. A
row that should fire on some notes of its range and not others is two rows.

A range also makes the pitch *mean* something, so it becomes a second value alongside
velocity:

- **Pitch** is where the note sat in the span, `0..1`, remapped into **Pitch Out
  Min/Max**. It normalises across the span's **own extent**, so one octave and two
  octaves both cover the full output range. Untick **Remap Pitch** to get the raw note
  number instead — that is how you ask for pitch as an identity rather than a position.
- **Velocity** is unchanged: 0–127 through **Remap** into **Out Min/Max**.

The two have separate ranges because they are separate measurements — a pitch sweeping
`0..1` while velocity drives a `0..10` intensity.

A single note still works exactly as before. One pitch carries no information, so its
span position is `0` and **Pitch -> Parameter** is normally left empty.

### Which parameter gets which value

Two values need somewhere to go, so a binding can name the parameter each one drives:

```
Velocity -> Parameter    Level
Pitch    -> Parameter    Pitch
```

Names, not positions. Declaration order used to decide this, which meant a function you
wanted to drive from MIDI had to put its MIDI-relevant parameter **first**; naming the
parameter means the function is written for what it does and the map adapts. Given

```
void Sweep(FVector Origin, float Pitch, float Level)
```

neither scalar is reachable by position — `Origin` eats the first three argument slots —
but both are reachable by name.

Some details worth knowing:

- **Auto-Map fills them in for you.** A newly listed binding gets velocity pointed at
  its function's **first** continuous parameter and pitch at the **second**. Velocity
  takes the first because that keeps the common case identical to the old positional
  default. See below.
- **Leave both empty and nothing changes.** The binding behaves exactly as it did before
  any of this existed: one value, first parameter. **Send Source Number** keeps working
  too. Naming either parameter takes over the whole layout, and Send Source Number is
  then ignored.
- **A name is resolved to an argument slot, not a parameter index.** A `vec3` occupies
  three slots and a Blueprint **output** pin occupies none, so an output pin cannot be
  assigned — it is written by the call.
- **Resolution happens against the live signature, every message.** A Blueprint recompile
  that reorders or renames parameters is followed rather than silently mismatched.
- **A name that matches nothing is reported once**, naming what the function actually
  offers, and that value is not sent. The event still fires — the note did arrive on an
  input the binding claims — and the parameter keeps its zero rather than being quietly
  redirected to whatever happened to be first.
- **On release**, pitch survives but velocity does not: which pad was let go is still the
  pad it was, while a released note carries no meaningful velocity and shapes from zero.

**Available Parameters** on each binding lists the names that function offers, with the
slot each occupies, and is filled by **Validate Against Level**. It is read-only and not
saved — it is a fact about the level that happens to be open. There is no dropdown: the
engine's property-options hook resolves to the owning asset and cannot tell which array
element is being edited, so it could only offer every parameter name in the whole map,
which would read as a list of valid choices without being one.

**Control change is untouched by all of this.** A controller already sends a value, and
a CC number means nothing on a scale, so a span of CC numbers would be a different
feature with no use behind it. On a Control Change row every pitch field is hidden
outright, and Auto-Map leaves `Pitch -> Parameter` empty.

### What counts as a parameter worth driving

**Auto-Map From Level** pre-fills the pair from the function's signature: velocity gets
the first parameter that carries a *magnitude*, pitch gets the second.

Only `float`, `int` and `byte` qualify. The exclusions are the interesting part:

| Excluded | Why |
| --- | --- |
| `bool` | A number in C++ and a choice in meaning. Sweeping a controller across one is a threshold nobody picked |
| `enum` | Same, worse: a sweep picks nonsense on the way past |
| `string`, `name`, `text` | Take a value, but not a magnitude |
| `vec3`, `rotator`, `color`, `transform` | Several numbers. Which of a vec3's three slots a knob should drive has no default answer |
| arrays | Variadic, so "the parameter" is not one slot |
| output pins | Written by the call. Pointing velocity at one would mean the message overwrote a return value |

So `void Sweep(FVector Origin, float Pitch, float Level)` gets velocity → `Pitch` and
pitch → `Level`, skipping `Origin` entirely. A function with one numeric parameter gets
velocity only. A trigger gets neither and stays on the untouched path.

This is **additive only**, and it is the same discipline as the rest of Auto-Map:

- It fills the pair only when **both** are still empty. Name one yourself and Auto-Map
  leaves the row alone entirely — it will not complete your sentence, because naming one
  and leaving the other blank is itself a statement.
- Running Auto-Map twice changes nothing.
- It applies to **existing** rows as well as new ones, so an asset authored before this
  feature picks the assignment up by clicking Auto-Map From Level again.

**Validate Against Level does not fill them in.** Validate never edits and never dirties
the package — that is what makes it safe to click on an asset you are unsure about — so
instead it counts the rows that could be filled and says so in the log. `OSCulator.MIDIValidate`
reports the same count.

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
