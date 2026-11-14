# UE OSCulator

OSCulator is an OSC + MIDI plugin for Unreal Engine, designed to simplify
bi-directional interaction between multiple tools.

## Video Tutorials
**Get Started / OSC In: https://youtu.be/gXMZ8Na-vls**

**MIDI In Mapping: https://youtu.be/_J8eaGlQ7CE**

## Road Map
- Improved tools for midi and osc out

## Key features

- **Call any Blueprint event over OSC or MIDI** by tagging its actor.
- In depth **MIDI input mapping** interface with MIDI Learn
  - a section per actor tag, with a **Scan** that lists just that tag's functions
  - **Single Note**, **Note Range**, Control Change and Program Change per row
  - midi in filtering by data type or range
  - rescaling for pitch, velocity and cc
- **Send OSC and MIDI from Blueprint**
  - Avoid stuck notes: `Send MIDI Note` releases the note automatically after a tweakable duration.
  
- **Optimized for real time use.**
## Quick start

This repository *is* a working demo project — clone it, open `OSCulator.uproject`,
build when prompted and you're ready to test osc and midi map with your own tools.

1. Send OSC to **port 8000**, or open the patch in `TouchDesigner/` and use that.
2. In the console, `OSCulator.List Custom` shows everything the demo map exposes.
3. For MIDI, tick **Enable MIDI In** in *Project Settings → Plugins → OSCulator* and add
   your controller by the name `OSCulator.MIDIDevices` prints. The demo's map asset is
   already set up.

### Auto camera and TouchOSC template

The demo map plays as `BP_AutoCamera` (via `GM_AutoCamera`): a CineCamera on a spring
arm, tagged `OSC_autocam`. A `BP_EnviroManager` in the level, tagged `OSC_enviro`, finds
the sun, sky, fog, clouds and post process volume on BeginPlay.
`TouchOSC/AutoCamera.tosc` drives both — open it in TouchOSC and point its OSC
connection at the Unreal machine, on the port above.

**[TouchOSC controls reference →](https://claude.ai/artifact/R9vCVs2HUZrysvxFVgZnZU)** — both pages
drawn to scale, with every control's address, range and behaviour.

| Address | Argument | Does |
| --- | --- | --- |
| `/autocam/OrbitY`, `/autocam/OrbitZ`, `/autocam/SpringArmLength` | `-1..1` | joystick-style rates, `0` stops |
| `/autocam/PivotXY` | `x y`, `-1..1` | moves the pivot right / forward relative to the view, flat on the ground |
| `/autocam/PivotZ` | `-1..1` | moves the pivot up / down in world Z, kept above the ground |
| `/autocam/FocalLength`, `/autocam/Aperture` | `0..1` | 10–120 mm and f/1.2–22, exponential so the wide / shallow end gets the precision |
| `/autocam/FocusOffset` | `-1..1` | focus follows the pivot; this shifts it ±120 cm |
| `/enviro/TimeOfDay` · `/enviro/DayCycleSpeed` | `0..24` · hours/s | sun position, or a running day/night cycle |
| `/enviro/WhiteTemp`, `WhiteTint`, `BloomIntensity`, `BloomThreshold`, `LensFlareIntensity`, `LensFlareBokehSize`, `Saturation`, `Contrast`, `Gamma`, `Gain` | one float each | post process, one fader per setting |

Speeds, limits and the aspect ratio are variables on `BP_AutoCamera` (*Tuning* and
*Lens*). In the template, **auto orbit z** keeps the orbit Z dial where you leave it so
the camera keeps circling.

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
Content/0_OSCulatorDemoProject/   demo map, BP_AutoCamera, BP_EnviroManager, BP_Laser, BP_Cube, a MIDI map asset
TouchDesigner/                    a demo patch to send and receive against
TouchOSC/                         AutoCamera.tosc, a control surface for the auto camera and environment
Source/                           the host project module
```

**Only `Plugins/OSCulator` is needed in your own project.** Copy that folder in and
build, or take the plugin on its own:

```bash
git clone -b plugin https://github.com/baronlanteigne/OSCulator.git
```

The [`plugin`](../../tree/plugin) branch is the same plugin with the demo project
stripped out, so its root drops straight into your `Plugins/` folder.

77 automation tests cover the codec, registry, dispatch, networking, MIDI ingest,
value shaping, note ranges and the port filters. Run them with:

```
UnrealEditor-Cmd.exe <project>.uproject -ExecCmds="Automation RunTests OSCulator;Quit" ^
  -unattended -nopause -nosplash -NullRHI -NoSound -log ^
  -testexit="Automation Test Queue Empty"
```

## Previous version

The UE 5.6 release lives on the [`v0.1_UE5.6`](../../tree/v0.1_UE5.6) branch. This
version is a full rewrite and shares no code with it.
