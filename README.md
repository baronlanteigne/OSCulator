# UE OSCulator

OSCulator is an OSC + MIDI plugin for Unreal Engine, designed to simplify
bi-directional interaction between multiple tools.

## Key features

- **Call any Blueprint event over OSC or MIDI** by tagging its actor. Several actors
  can share a tag and all fire.
- **MIDI note, CC and program change mapping** through a Data Asset, written
  target-first: it lists what is controllable, then you assign inputs. **Learn** captures
  the device, channel and input in one gesture. **Auto-Map** lists a whole level's events
  in one click and assigns them from a per-tag layout, additively, so it never disturbs
  mappings you have already made. Several maps run at once, so one asset per controller
  is an option.
- **A span of notes can drive one event**, with the pitch's position in that span
  remapped to a value of its own alongside velocity — and each value **assigned to a
  function parameter by name**, so the event is written for what it does rather than
  around MIDI's argument order.
- **Per-device input filtering.** Each MIDI device carries its own message-type filter
  and its own list of channels to ignore, because a channel number means something
  different on every box — channel 10 is drums on one and a lighting desk on the next.
  Anything filtered is discarded at the port and never takes a queue slot.
- **A Performance Mode** that switches off everything existing only to help you author:
  Learn, Blueprint-recompile tracking, and per-message signature resolution. It changes
  how much work is done and never what reaches a function.
- **MIDI input OSCulator reads itself**, not through the engine's MIDI plugin — so clock
  and other traffic you cannot use are dropped at the port rather than filling a queue
  that is drained once a frame. On a running sequencer, clock alone was 68% of the wire.
- **Send OSC and MIDI from Blueprint**, to as many destinations as you configure.
  `Send MIDI Note` releases the note automatically after a tweakable duration, so a
  stuck note is not one Delay node away.
- **Self-describing.** Send `/_describe` and OSCulator replies with its whole callable
  surface, so a patch can build its own senders instead of being told the addresses.
- **Built for live use.** Messages are received on their own thread, coalesced to the
  last value per address per frame, and dispatched *before* actor ticks — so a value
  received this frame affects this frame. Measured at 0.04 ms per frame under a live
  TouchDesigner stream.

## Quick start

**Get Started Tutorial: https://youtu.be/gXMZ8Na-vls**

1. Copy `Plugins/OSCulator` into your project and build.
2. Tag an actor `OSC_cube` in **Actor → Tags**.
3. Play, then in the console: `OSCulator.List Custom` to see what is callable.
4. Send real OSC to **port 8000**. See the demo TouchDesigner project to start testing.

For **MIDI** input, three things have to be true before a note can reach anything, and
all three are off or empty by default:

1. **Enable MIDI In**, in *Project Settings → Plugins → OSCulator*.
2. Add your controller to **MIDI Input Devices** by its exact name — run
   `OSCulator.MIDIDevices` to list what the OS reports.
3. Add a **MIDI Map** asset and give it at least one binding with a source.

Then `OSCulator.MIDIStatus` tells you what is open and what is arriving, and
`OSCulator.MIDIMonitor 1` logs every incoming message. Full walkthrough in
[Setup and configuration](Plugins/OSCulator/Docs/SETUP.md#5-midi-input).

## Documentation

| | |
| --- | --- |
| [Setup and configuration](Plugins/OSCulator/Docs/SETUP.md) | Install, tagging, every setting, and the interactions worth knowing before you are debugging them |
| [Console reference](Plugins/OSCulator/Docs/CONSOLE.md) | Every command, with real output and how to read the counters |
| [Helper nodes](Plugins/OSCulator/Docs/HELPERS.md) | Blueprint nodes that ship with the plugin but have nothing to do with OSC or MIDI — what to do with values once they arrive |
| [Build status](Plugins/OSCulator/Docs/STATUS.md) | What is built and tested, what is not yet verified, and the behaviours that cost debugging time |

## This repository

The whole test project, so the plugin has somewhere to be exercised:

```
Plugins/OSCulator/                the plugin — this is the portable part
Content/0_OSCulatorDemoProject/   demo map, BP_Laser, BP_Cube, a MIDI map asset
Source/                           the host project module
TouchDesigner/                    a demo .toe to send and receive against
```

**Only `Plugins/OSCulator` is needed in your own project** — everything else exists so
the plugin has somewhere to be exercised. See
[Using the plugin without the demo project](#using-the-plugin-without-the-demo-project).

49 automation tests cover the codec, registry, marshalling, dispatch, networking,
coalescing, MIDI note names, ingest, value shaping, note ranges, parameter-name
resolution, auto-map, the binding-type migration, registry staleness and the port
filters.
Run them with:

```
UnrealEditor-Cmd.exe <project>.uproject -ExecCmds="Automation RunTests OSCulator;Quit" ^
  -unattended -nopause -nosplash -NullRHI -NoSound -log ^
  -testexit="Automation Test Queue Empty"
```

## Using the plugin without the demo project

The plugin is self-contained in `Plugins/OSCulator`. Three ways to take just that:

**Sparse checkout** — the whole history, only that folder on disk:

```bash
git clone --filter=blob:none --sparse https://github.com/baronlanteigne/OSCulator.git
cd OSCulator
git sparse-checkout set Plugins/OSCulator
```

**A release asset.** Grab `OSCulator-plugin-<version>.zip` from
[Releases](../../releases) if one is attached, unzip it into your project's `Plugins/`
folder, and build.

**A plugin-only branch.** `git subtree split` rewrites the history of one folder into a
branch whose ROOT is that folder, so consumers get the plugin and nothing else:

```bash
git subtree split --prefix=Plugins/OSCulator -b plugin-only
git push origin plugin-only
```

Re-run both commands after any plugin change and the branch catches up. Consumers then
clone that branch straight into their Plugins folder, or track it:

```bash
git clone -b plugin-only https://github.com/baronlanteigne/OSCulator.git OSCulator
# or, to pull later updates into their own repo
git subtree add --prefix=Plugins/OSCulator https://github.com/baronlanteigne/OSCulator.git plugin-only --squash
```

`git subtree add` grafts the remote's ROOT at the prefix you name, which is why it needs
a branch that is already plugin-only — pointed at `main` it would nest the whole demo
project inside your Plugins folder.

## Previous version

The UE 5.6 release lives on the [`v0.1_UE5.6`](../../tree/v0.1_UE5.6) branch. This
version is a full rewrite and shares no code with it.
