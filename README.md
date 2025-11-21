# UE OSCulator
UE OSCulator is a OSC + MIDI plugin for Unreal Engine designed to simplify the use of OSC and MIDI within Unreal Engine.

It simplifies the parsing and routing of incoming messages, straight to the actor you want to interact with.

This repository comes with a demo project and a simple TouchDesigner patch controlling it.

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

### Contributors:
- Dani Kolgan
