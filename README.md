# UE OSCulator

OSCulator is an OSC + MIDI plugin for Unreal Engine, designed to simplify
bi-directional interaction between multiple tools.

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

Get Started Tutorial: https://youtu.be/gXMZ8Na-vls

1. Copy `Plugins/OSCulator` into your project and build.
2. Tag an actor `OSC_laser` in **Actor → Tags**.
3. Play, then in the console: `OSCulator.List Custom` to see what is callable.
4. Send real OSC to **port 8000**. See the demo TouchDesigner project to start testing.

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
