# UE OSCulator 5.6
UE OSCulator is a OSC + MIDI plugin for Unreal Engine designed to simplify the integration of external controlling software within Unreal Engine.
It parses and routes the incoming messages, straight to the actor you want to interact with.

This version was built in 5.6 using Blueprints. The system was fully tested to work in 5.7 and 5.8.

This version is archived here as reference but will not be updated.

### [setup video](https://youtu.be/J3UqybmT_OA)

### [demo](https://youtube.com/shorts/3rbzkUMAiUI)

This repository includes a demo **Unreal 5.6 project** and a simple TouchDesigner patch controlling it but works with any other software using MIDI and OSC.
Your ideas, suggestions and any kind of contributions are welcome.
### [discord server](https://discord.gg/cjUawK65PY)

### Requirements
- Basic Unreal Engine knowledge: install a plugin, create blueprint networks, build basic logic using blueprint.
- **Unreal Engine 5.6**
- [**LTween** interpolation plugin](https://www.fab.com/listings/1dbb1791-152d-4581-8c0e-32faccabfbf2)

### Installation & Usage

- Enable the following plugins:
  - LTween
  - OSC (official plugin comes with Unreal Engine)
  - MIDI (official plugin, comes with UE)
    
- Add and configure OSC_Init and MIDI_Init Components to your own Game Mode.
    - OSC_Init needs your local IP and ports for OSC communication.
    - MIDI_Init needs the name of the midi devices you want to use. (I use LoopMIDI for virtual ports)
      
- In the actor you want to control:
  - Add the BPI_MIDI_OSC as an BPI Interface in the Class Settings
  - Add at least one tag
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

### Other resources / Similar projects
- [https://eusebijucgla.com/](https://www.eusebijucgla.com/)
- https://mistaudio.com/tutorials/ue-live/
- https://www.youtube.com/@semandtrisavclub4962
- Ableton M4L Devices for OSC: https://www.fragmentflow.com/tools-and-other-downloads/
- [Yu Fujishiro](https://www.youtube.com/yfjsr)
- [LoopMIDI](https://www.tobias-erichsen.de/software/loopmidi.html)
- ... (send me your own!)
