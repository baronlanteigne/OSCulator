# UE OSCulator
UE OSCulator is a OSC + MIDI plugin for Unreal Engine designed to simplify the integration of external controlling software within Unreal Engine.
It parses and routes the incoming messages, straight to the actor you want to interact with.

It is intended for artists who are trying to bridge their existing workflows in another software with an Unreal Engine scene.
I am currently using this system to enable bi directional communications between Ableton Live, TouchDesigner and Unreal.

rough intro covering how to setup the plugin and the TD demo for OSC input: https://youtu.be/J3UqybmT_OA

quick demo of my wip using this thing in multiple ways: https://youtube.com/shorts/3rbzkUMAiUI

This repository includes a demo **Unreal 5.6 project** and a simple TouchDesigner patch controlling it but works with any other software using MIDI and OSC.

#### Your ideas, suggestions and any kind of contributions are welcome.
#### Discuss the plugin here: https://discord.gg/cjUawK65PY

### Requirements
- Basic Unreal Engine knowledge: install a plugin, create blueprint networks, build basic logic using blueprint.
- **Unreal Engine 5.6**
- **LTween** interpolation plugin: https://www.fab.com/listings/1dbb1791-152d-4581-8c0e-32faccabfbf2

### Installation & Usage

- Enable the following plugins:
  - LTween
  - OSC (official plugin comes with Unreal Engine)
  - MIDI (official plugin, comes with UE)
    
- Edit BP_GameMode to access OSC_Init and MIDI_Init, or add those components to your own Game Mode.
  
- Configure a OSC and/or MIDI server:
    - OSC_Init needs your local IP and ports for OSC communication.
    - MIDI_Init needs the name of the midi devices you want to use. (I use LoopMIDI for virtual ports)

- See Test_Cube in the demo project as an example of how to setup your actor.
      
  - In the actor you want to control, add the BPI_MIDI_OSC as an BPI Interface in the Class Settings.  
  - Also add one or more of the components:
    - OSC_Receiver
    - OSC_Sender
    - MIDI_Receiver
    - MIDI_Sender

      These components let you send and receive messages from within the Actor BP they are added to.
      The tag on the actor lets you set the osc address used to control that actor.
      If two actors share a tag, they both receive the osc messages.

### Demo Project
--> Check the demo Level, GameMode and Test_Cube Actor in the Demo folder in the plugin for an example.

--> Check the provided .toe file for an example of how to format the OSC messages to use the interp plugin.

### Contributors
- Dani Kolgan

### Special Thanks
- Eusebi Jucgla

### Other resources / Similar projects
- [https://eusebijucgla.com/](https://www.eusebijucgla.com/)
- https://mistaudio.com/tutorials/ue-live/
- https://www.youtube.com/@semandtrisavclub4962
- Ableton M4L Devices for OSC: https://www.fragmentflow.com/tools-and-other-downloads/
- [Yu Fujishiro](https://www.youtube.com/yfjsr)
- [LoopMIDI](https://www.tobias-erichsen.de/software/loopmidi.html)
- ...
