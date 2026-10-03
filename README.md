# UE OSCulator

OSCulator is an OSC + MIDI plugin for Unreal Engine, designed to simplify
bi-directional interaction between multiple tools.

## Video Tutorials
**Get Started / OSC In: https://youtu.be/gXMZ8Na-vls**

**MIDI In Mapping: https://youtu.be/_J8eaGlQ7CE**

## Key features

- **Call any Blueprint event over OSC or MIDI** by tagging its actor. Several actors
  can share a tag and all fire.
- **MIDI note, CC and program change mapping** through a Data Asset, written
  target-first: it lists what is controllable, then you assign inputs. **Learn**
  captures the device, channel and input in one gesture. **Auto-Map** lists a whole
  level's events in one click and assigns them additively, so it never disturbs
  mappings you have already made.
- **A span of notes can drive one event**, with the pitch's position in that span
  becoming a value alongside velocity — and each value assigned to a function parameter
  **by name**, not by argument order.
- **Per-device filtering.** Each controller carries its own message-type filter and
  its own list of channels to ignore. Anything filtered is discarded at the port.
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
  TouchDesigner stream. A **Performance Mode** switch turns off the editing-only
  features once you are done authoring.

## Quick start

This repository *is* a working demo project — clone it, open `OSCulator.uproject`,
build when prompted and you're ready to test osc and midi map with your own tools.

1. Send OSC to **port 8000**, or open the patch in `TouchDesigner/` and use that.
2. In the console, `OSCulator.List Custom` shows everything the demo map exposes.
3. For MIDI, tick **Enable MIDI In** in *Project Settings → Plugins → OSCulator* and add
   your controller by the name `OSCulator.MIDIDevices` prints. The demo's map asset is
   already set up.

## Documentation

| | |
| --- | --- |
| [Setup and configuration](Plugins/OSCulator/Docs/SETUP.md) | Install, tagging, every setting, and the interactions worth knowing before you are debugging them |
| [Console reference](Plugins/OSCulator/Docs/CONSOLE.md) | Every command, with real output and how to read the counters |
| [Helper nodes](Plugins/OSCulator/Docs/HELPERS.md) | Blueprint nodes that ship with the plugin but have nothing to do with OSC or MIDI — what to do with values once they arrive |
| [Build status](Plugins/OSCulator/Docs/STATUS.md) | What is built and tested, what is not yet verified, and the behaviours that cost debugging time |

## This repository

```
Plugins/OSCulator/                the plugin — this is the portable part
Content/0_OSCulatorDemoProject/   demo map, BP_Laser, BP_Cube, a MIDI map asset
TouchDesigner/                    a demo patch to send and receive against
Source/                           the host project module
```

**Only `Plugins/OSCulator` is needed in your own project.** Copy that folder in and
build, or take the plugin on its own:

```bash
git clone -b plugin https://github.com/baronlanteigne/OSCulator.git
```

The [`plugin`](../../tree/plugin) branch is the same plugin with the demo project
stripped out, so its root drops straight into your `Plugins/` folder.

49 automation tests cover the codec, registry, dispatch, networking, MIDI ingest,
value shaping, note ranges and the port filters. Run them with:

```
UnrealEditor-Cmd.exe <project>.uproject -ExecCmds="Automation RunTests OSCulator;Quit" ^
  -unattended -nopause -nosplash -NullRHI -NoSound -log ^
  -testexit="Automation Test Queue Empty"
```

## Previous version

The UE 5.6 release lives on the [`v0.1_UE5.6`](../../tree/v0.1_UE5.6) branch. This
version is a full rewrite and shares no code with it.
