# dmx
🚥 MIDI to DMX Controller

![Screenshot](images/info.png?raw=true)

## Configuration
Up to 16 DMX devices are mapped to 16 MIDI channels. The DMX
devices are configured by their base DMX address and the number
of DMX channels to control.

Default power-on values can be stored in the configuration. A MIDI
_System-Reset_ message will reset all devices and channels to their
configured power-on values.

For RGB devices, it is possible to use a separate set of more
intuitive CC values to control _Brightness_, _Colour_, _Saturation_;
the controller will calculate the RGB channel values accordingly.

![Screenshot](images/configuration.png?raw=true)

## Controllers
MIDI Control Change (CC) values control the DMX channel values.

![Screenshot](images/controllers.png?raw=true)

## Notes
The velocity value of a note overwrites the corresponding CC value
for the duration of the note; a MIDI _Note-Off_ will reset the DMX channel
value to the current CC value. A MIDI _All-Notes-Off_ message will restore
all devices and channels to the currently configured controller values.

During an active note, channel aftertouch will change the _Brightness_,
pitch bend will change the _Colour_.

There are two programs (MIDI _Program Change_) for notes. _Channels_
maps a range of consecutive notes to DMX channels.

![Screenshot](images/notes-channels.png?raw=true)

_Brightness_ maps all received notes (the pitch is ignored) to control
only the brightness (or the first DMX channel of the device) of the DMX device.
If multiple notes are played at the same time, a MIDI _Note-Off_ will restore
the value of the earlier, still active note.

![Screenshot](images/notes-brightness.png?raw=true)

## Max For Live Device
An optional Ableton Max4Live [device](https://github.com/versioduo/max4live)
allows the the direct mapping from Live Automation to DMX values.

To avoid inconsistencies it will monitor the Live transport state, and update
all configured DMX channels of the current device every time _Play_ or
_Stop_ are triggered.

![Screenshot](images/ableton.png?raw=true)

## Hardware
![Schematics / PCB](hardware)

# Copying
Anyone can use this public domain work without having to seek authorisation, no one can ever own it.

