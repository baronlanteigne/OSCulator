# UE OSCulator

OSCulator is an OSC + MIDI plugin for Unreal Engine, designed to simplify
bi-directional interaction between multiple tools.

## What it does

Tag an actor `OSC_laser`. Send `/laser/Fire 0 0 1 burst 0.5` from TouchDesigner, Max
or Ableton. OSCulator finds every actor carrying that tag, reads `Fire`'s parameter
list by reflection, and fills it from the message in order.

No per-actor wiring. No parser. No manager to build.

**The function signature is the schema.** The sender declares no types — it sends the
right count of values in the right order, and the receiving signature decides what
they mean. Given `Fire(FVector Dir, FName Mode, float Power)`, five incoming values
become a vector from the first three, a name from the fourth, and a float from the
fifth.

## Key features

- **Call any Blueprint event over OSC or MIDI** by tagging its actor. Several actors
  can share a tag and all fire.
- **MIDI note and CC mapping** through a Data Asset, with **MIDI Learn** — arm a row,
  hit a pad. Auto-populate maps a whole level's events in one click, additively, so
  it never disturbs mappings you have already made.
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

1. Copy `Plugins/OSCulator` into your project and build.
2. Tag an actor `OSC_laser` in **Actor → Tags**.
3. Play, then in the console: `OSCulator.List Custom` to see what is callable.
4. Test without a sender: `OSCulator.Send /laser/Fire 0 0 1 burst 0.5`
5. Send real OSC to **port 8000**. `OSCulator.Status` shows what arrived.

## Documentation

| | |
| --- | --- |
| [Setup and configuration](Plugins/OSCulator/Docs/SETUP.md) | Install, tagging, every setting, and the interactions worth knowing before you are debugging them |
| [Console reference](Plugins/OSCulator/Docs/CONSOLE.md) | Every command, with real output and how to read the counters |
| [Build status](Plugins/OSCulator/Docs/STATUS.md) | What is built and tested, what is not yet verified, and the behaviours that cost debugging time |

## This repository

The whole test project, so the plugin has somewhere to be exercised:

```
Plugins/OSCulator/                the plugin — this is the portable part
Content/0_OSCulatorDemoProject/   demo map, BP_Laser, a MIDI map asset
Source/                           the host project module
```

Only `Plugins/OSCulator` is needed in your own project.

31 automation tests cover the codec, registry, marshalling, dispatch, networking,
coalescing, MIDI note names, ingest and auto-populate. Run them with:

```
UnrealEditor-Cmd.exe <project>.uproject -ExecCmds="Automation RunTests OSCulator;Quit" ^
  -unattended -nopause -nosplash -NullRHI -NoSound -log ^
  -testexit="Automation Test Queue Empty"
```

## Previous version

The UE 5.6 release lives on the [`v0.1_UE5.6`](../../tree/v0.1_UE5.6) branch. This
version is a full rewrite and shares no code with it.
