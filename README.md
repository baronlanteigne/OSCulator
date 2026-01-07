# UE OSCulator
UE OSCulator is a OSC + MIDI plugin for Unreal Engine designed to simplify the integration of external controlling software within Unreal Engine.
It parses and routes the incoming messages, straight to the actor you want to interact with.

It is intended for artists with basic knowledge of Unreal Engine who are trying to bridge their existing workflows with an Unreal Engine scene.
I am currently using this system to enable bi directional communications between Ableton Live, TouchDesigner and Unreal.

quick demo of my wip using this thing in multiple ways: https://youtube.com/shorts/3rbzkUMAiUI

This repository includes a demo Unreal 5.6 project and a simple TouchDesigner patch controlling it but works with any other software using MIDI and OSC.

#### Your ideas, suggestions and any kind of contributions are welcome.
#### Discuss the plugin here: https://discord.gg/cjUawK65PY

### Requirements
- LTween interpolation plugin: https://www.fab.com/listings/1dbb1791-152d-4581-8c0e-32faccabfbf2
- OSC official plugin by Epic (already comes with UE)
- MIDI Device Support official plugin by Epic (already comes with UE)

### Installation & Usage
- Enable the required plugins.
- Add the OSC_Init and MIDI_Init components to your Game Mode.
- Configure a OSC and/or MIDI server, the parameters are on the OSC_Init and MIDI_Init components respectively.
- In the actor you want to control, add the BPI_MIDI_OSC as an Interface in the Class Settings.
- Also add one or more of the components:
  - OSC_Receiver
  - OSC_Sender
  - MIDI_Receiver
  - MIDI_Sender

    These components let you send and receive messages from within the Actor BP they are added to.
    The tag on the actor lets you set the osc address used to control that actor.
    If two actors share a tag, they both receive the osc messages.

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
- ...
