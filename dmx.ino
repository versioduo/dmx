// © Kay Sievers <kay@versioduo.com>, 2020-2022
// SPDX-License-Identifier: Apache-2.0

#include <V2Buttons.h>
#include <V2Color.h>
#include <V2DMX.h>
#include <V2Device.h>
#include <V2LED.h>
#include <V2MIDI.h>
#include <V2Music.h>

V2DEVICE_METADATA("com.versioduo.dmx", 51, "versioduo:samd:dmx");

static V2LED::WS2812 LED(2, PIN_LED_WS2812, &sercom2, SPI_PAD_0_SCK_1, PIO_SERCOM);
static V2DMX DMX(PIN_DMX_TX, &sercom3, SPI_PAD_0_SCK_1, PIO_SERCOM);

static class Device : public V2Device {
public:
  Device() : V2Device(24 * 1024) {
    metadata.vendor      = "Versio Duo";
    metadata.product     = "V2 dmx";
    metadata.description = "MIDI to DMX Bridge";
    metadata.home        = "https://versioduo.com/#dmx";

    help.device        = "Up to 16 DMX devices are mapped to 16 MIDI channels. The DMX devices are configured by "
                         "their base DMX address and the number of DMX channels to control.\n"
                         "Control change messages are mapped to DMX channel values. Notes are used to temporarily "
                         "overwrite a value during the duration of the note.\n"
                         "For RGB devices, it is possible to use a separate set of more intuitive CC values to "
                         "control Brightness, Color, Saturation; the controller will calculate the RGB channel "
                         "values accordingly.";
    help.configuration = "Default power-on values can be configured. An AllNotesOff control message will "
                         "clear all power-on settings. A MIDI System-Reset message will reset all DMX devices "
                         "and channels to their configured power-on values.";

    system.download  = "https://versioduo.com/download";
    system.configure = "https://versioduo.com/configure";

    usb.ports.standard = 0;

    configuration = {.size{sizeof(config)}, .data{&config}};
  }

  enum class CC {
    Brightness = V2MIDI::CC::Controller3,
    Color      = V2MIDI::CC::Controller14,
    Saturation = V2MIDI::CC::Controller15,
  };

  enum class State { Off, Config, MIDI, _count };
  enum class Program { Channels, Brightness, _count };

  // Config, written to EEPROM
  struct {
    struct {
      char name[32];
      uint16_t address;
      uint16_t count;
      uint8_t h;
      uint8_t s;
      uint8_t v;
      uint8_t channels[32];
    } devices[16];
  } config{.devices{
    {.name{"Alfa"}, .address{0 * 5}, .count{3}},
    {.name{"Berta"}, .address{1 * 5}},
    {.name{"Charlotte"}, .address{2 * 5}},
    {.name{"David"}, .address{3 * 5}},
    {.name{"Eva"}, .address{4 * 5}},
    {.name{"Friedrich"}, .address{5 * 5}},
    {.name{"Gustav"}, .address{6 * 5}},
    {.name{"Heinrich"}, .address{7 * 5}},
    {.name{"Ida"}, .address{8 * 32}},
    {.name{"Jakob"}, .address{9 * 5}},
    {.name{"Katharina"}, .address{10 * 5}},
    {.name{"Ludwig"}, .address{11 * 5}},
    {.name{"Marie"}, .address{12 * 5}},
    {.name{"Nathan"}, .address{13 * 5}},
    {.name{"Otto"}, .address{14 * 5}},
    {.name{"Paula"}, .address{15 * 5}},
  }};

  void readConfiguration() {
    bool values{};

    for (uint8_t ch = 0; ch < 16; ch++) {
      // Set default values from EEPROM.
      if (config.devices[ch].count >= 3 && config.devices[ch].v > 0) {
        _devices[ch].h = (float)config.devices[ch].h / 127.f * 360.f;
        _devices[ch].s = (float)config.devices[ch].s / 127.f;
        _devices[ch].v.setFraction((uint8_t)CC::Brightness, (float)config.devices[ch].v / 127.f);
        _devices[ch].mode = Mode::HSV;

        for (uint8_t i = 3; i < config.devices[ch].count; i++)
          _devices[ch].channels[i] = config.devices[ch].channels[i];
        values = true;

      } else {
        for (uint8_t i = 0; i < config.devices[ch].count; i++) {
          if (config.devices[ch].channels[i] == 0)
            continue;

          _devices[ch].channels[i] = config.devices[ch].channels[i];
          values                   = true;
        }
      }

      updateChannel(ch);
    }

    if (values)
      setState(State::Config);
  }

  void allNotesOff() {
    if (_force.trigger()) {
      reset();
      return;
    }

    for (uint8_t ch = 0; ch < 16; ch++) {
      _devices[ch].note = {};
      _devices[ch].note.playing.reset();
      updateChannel(ch);
    }
  }

  void setState(State state) {
    _state = state;

    switch (_state) {
      case State::Off:
        LED.setHSV(V2Color::Yellow, 1, 0.1);
        break;

      case State::Config:
        LED.setHSV(V2Color::Green, 1, 0.4);
        break;

      case State::MIDI:
        LED.setHSV(V2Color::Cyan, 1, 0.4);
        break;
    }
  }

  void toggleState() {
    switch (_state) {
      case State::Off:
        readConfiguration();
        break;

      case State::Config:
      case State::MIDI:
        reset();
        break;
    }
  }

private:
  V2Music::ForcedStop _force;
  State _state{};
  const char *_programNames[(uint8_t)Program::_count]{"Channels", "Brightness"};

  // The current mode of the first three channels;
  enum class Mode { Channels, HSV };

  struct {
    Program program;
    uint16_t bank;

    // Controllers
    Mode mode;
    float h;
    float s;
    V2MIDI::CC::HighResolution<(uint8_t)CC::Brightness> v;
    uint8_t channels[32];

    // Notes
    struct {
      V2Music::Playing<88> playing;
      float aftertouch;
      float pitchbend;
      float h;
      float s;
      float v;
      uint8_t channels[32];
    } note;
  } _devices[16]{};

  // The currently selected channel in the user interface. Only a single channel
  // is exported with getAll() to reduce the size of the reply message.
  uint8_t _currentChannel{};

  void handleReset() override {
    LED.reset();
    DMX.reset();
    _force.reset();

    _currentChannel = 0;
    for (uint8_t i = 0; i < 16; i++)
      _devices[i] = {};

    setState(State::Off);
  }

  void setMode(uint8_t channel, Mode mode) {
    _devices[channel].mode = mode;

    switch (mode) {
      case Mode::Channels:
        _devices[channel].h = 0;
        _devices[channel].s = 0;
        _devices[channel].v.set((uint8_t)CC::Brightness, 0);
        break;

      case Mode::HSV:
        _devices[channel].channels[0] = 0;
        _devices[channel].channels[1] = 0;
        _devices[channel].channels[2] = 0;
        break;
    }
  }

  void setDMX(uint8_t channel, uint8_t address, float fraction) {
    DMX.setChannel(config.devices[channel].address + address, roundf(fraction * 255.f));
  }

  // Update the DMX RGB channels with the given HSV values.
  void setDMXHSV(uint8_t channel, float h, float s, float v) {
    uint8_t r, g, b;
    V2Color::HSVtoRGB(h, s, v, r, g, b);
    DMX.setChannel(config.devices[channel].address + 0, r);
    DMX.setChannel(config.devices[channel].address + 1, g);
    DMX.setChannel(config.devices[channel].address + 2, b);
  }

  // Update the DMX channels for the duration of the notes. The brightness and color
  // are also modulated by aftertouch and pitch bend.
  void setDMXHSVNote(uint8_t channel) {
    bool note{};

    float h;
    if (_devices[channel].note.h > 0.f) {
      h    = _devices[channel].note.h;
      note = true;

    } else
      h = _devices[channel].h;

    float s;
    if (_devices[channel].note.s > 0.f) {
      s    = _devices[channel].note.s;
      note = true;

    } else
      s = _devices[channel].s;

    float v;
    if (_devices[channel].note.v > 0.f) {
      v    = _devices[channel].note.v;
      note = true;

    } else
      v = _devices[channel].v.getFraction();

    if (note) {
      h += (_devices[channel].note.pitchbend * 180.f);
      if (h > 360)
        h -= 360;
      else if (h < 0)
        h += 360;

      if (_devices[channel].note.aftertouch > 0)
        v = _devices[channel].note.aftertouch;
    }

    setDMXHSV(channel, h, s, v);
  }

  void updateChannel(uint8_t channel) {
    switch (_devices[channel].mode) {
      case Mode::Channels:
        // A single-channel device, the brightness note sets the channel value.
        if (config.devices[channel].count == 1 && _devices[channel].note.v > 0.f) {
          setDMX(channel, 0, _devices[channel].note.v);
          break;
        }

        for (uint8_t i = 0; i < config.devices[channel].count; i++) {
          if (_devices[channel].note.channels[i] > 0)
            setDMX(channel, i, (float)_devices[channel].note.channels[i] / 127.f);
          else
            setDMX(channel, i, (float)_devices[channel].channels[i] / 127.f);
        }
        break;

      case Mode::HSV:
        setDMXHSVNote(channel);

        for (uint8_t i = 3; i < config.devices[channel].count; i++) {
          if (_devices[channel].note.channels[i] > 0)
            setDMX(channel, i, (float)_devices[channel].note.channels[i] / 127.f);
          else
            setDMX(channel, i, (float)_devices[channel].channels[i] / 127.f);
        }
        break;
    }
  }

  // Notes temporarily overwrite the values set by the controllers. The Note-Off will
  // restore the value.
  void handleNote(uint8_t channel, uint8_t note, uint8_t velocity) override {
    switch (_devices[channel].program) {
      case Program::Channels: {
        switch (note) {
          case V2MIDI::C(3):
            if (config.devices[channel].count >= 3) {
              _devices[channel].note.v = (float)velocity / 127.f;
              setMode(channel, Mode::HSV);
              updateChannel(channel);
            }
            break;

          case V2MIDI::Cs(3):
            if (config.devices[channel].count >= 3) {
              _devices[channel].note.h = (float)velocity / 127.f * 360.f;
              setMode(channel, Mode::HSV);
              updateChannel(channel);
            }
            break;

          case V2MIDI::D(3):
            if (config.devices[channel].count >= 3) {
              _devices[channel].note.s = (float)velocity / 127.f;
              setMode(channel, Mode::HSV);
              updateChannel(channel);
            }
            break;

          case V2MIDI::Ds(3)... V2MIDI::Ds(3) + 31: {
            const uint8_t address = note - V2MIDI::Ds(3);
            if (address < config.devices[channel].count) {
              _devices[channel].note.channels[address] = velocity;
              updateChannel(channel);
            }
          } break;
        }
      } break;

      case Program::Brightness:
        if (note < V2MIDI::A(-1))
          return;

        if (note >= V2MIDI::A(-1) + 88)
          return;

        _devices[channel].note.playing.update(note, velocity);
        // Restore previous note.
        if (velocity == 0) {
          uint8_t n;
          uint8_t v;
          if (_devices[channel].note.playing.getLast(n, v))
            velocity = v;
        }

        _devices[channel].note.v = (float)velocity / 127.f;
        updateChannel(channel);
        break;
    }

    setState(State::MIDI);
  }

  void handleNoteOff(uint8_t channel, uint8_t note, uint8_t velocity) override {
    handleNote(channel, note, 0);
  }

  void handleProgramChange(uint8_t channel, uint8_t program) override {
    if (program != V2MIDI::GM::Program::FX5Brightness)
      return;

    if (_devices[channel].bank >= (uint8_t)Program::_count)
      return;

    _devices[channel].program = (Program)_devices[channel].bank;
    _devices[channel].note.playing.reset();
  }

  void handleControlChange(uint8_t channel, uint8_t controller, uint8_t value) override {
    switch (controller) {
      case V2MIDI::CC::BankSelect:
        _devices[channel].bank = value << 7;
        break;

      case V2MIDI::CC::BankSelectLSB:
        _devices[channel].bank |= value;
        break;

      case V2MIDI::CC::AllSoundOff:
      case V2MIDI::CC::AllNotesOff:
        allNotesOff();
        break;

      case (uint8_t)CC::Brightness:
      case V2MIDI::CC::ControllerLSB + (uint8_t)CC::Brightness:
        if (config.devices[channel].count < 3)
          break;

        if (!_devices[channel].v.setByte(controller, value))
          break;

        setMode(channel, Mode::HSV);
        updateChannel(channel);
        break;

      case (uint8_t)CC::Color:
        if (config.devices[channel].count < 3)
          break;

        _devices[channel].h = (float)value / 127.f * 360.f;
        setMode(channel, Mode::HSV);
        updateChannel(channel);
        break;

      case (uint8_t)CC::Saturation:
        if (config.devices[channel].count < 3)
          break;

        _devices[channel].s = (float)value / 127.f;
        setMode(channel, Mode::HSV);
        updateChannel(channel);
        break;

      case V2MIDI::CC::GeneralPurpose1... V2MIDI::CC::GeneralPurpose1 + 15: {
        const uint8_t address               = controller - V2MIDI::CC::GeneralPurpose1;
        _devices[channel].channels[address] = value;
        if (address <= 2)
          setMode(channel, Mode::Channels);
        updateChannel(channel);
      } break;

      case V2MIDI::CC::Controller102... V2MIDI::CC::Controller102 + 15: {
        const uint8_t address               = 16 + (controller - V2MIDI::CC::Controller102);
        _devices[channel].channels[address] = value;
        updateChannel(channel);
      } break;
    }

    setState(State::MIDI);
  }

  void handleAftertouchChannel(uint8_t channel, uint8_t pressure) override {
    switch (_devices[channel].program) {
      case Program::Channels:
        _devices[channel].note.aftertouch = (float)pressure / 127.f;
        updateChannel(channel);
        break;
    }
  }

  void handlePitchBend(uint8_t channel, int16_t value) override {
    switch (_devices[channel].program) {
      case Program::Channels:
        _devices[channel].note.pitchbend = (float)value / (value < 0 ? 8192.f : 8191.f);
        updateChannel(channel);
        break;
    }
  }

  void handleSystemReset() override {
    reset();
    readConfiguration();
  }

  void handleSwitchChannel(uint8_t channel) override {
    _currentChannel = channel;
  }

  void exportSettings(JsonArray json) override {
    for (uint8_t ch = 0; ch < 16; ch++) {
      {
        JsonObject setting = json.createNestedObject();
        setting["type"]    = "title";

        char name[16];
        sprintf(name, "Device %d", ch + 1);
        setting["title"] = name;
      }

      {
        JsonObject setting = json.createNestedObject();
        setting["type"]    = "number";
        setting["label"]   = "Channels";
        setting["max"]     = 32;
        setting["input"]   = "select";

        char path[64];
        sprintf(path, "devices[%d]/count", ch);
        setting["path"] = path;
      }

      if (config.devices[ch].count == 0)
        continue;

      {
        JsonObject setting = json.createNestedObject();
        setting["type"]    = "text";
        setting["label"]   = "Name";

        char path[64];
        sprintf(path, "devices[%d]/name", ch);
        setting["path"] = path;
      }

      {
        JsonObject setting = json.createNestedObject();
        setting["type"]    = "number";
        setting["label"]   = "Address";
        setting["min"]     = 1;
        setting["max"]     = 512;

        char path[64];
        sprintf(path, "devices[%d]/address", ch);
        setting["path"] = path;
      }

      if (config.devices[ch].count >= 3) {
        JsonObject setting = json.createNestedObject();
        setting["type"]    = "color";
        setting["ruler"]   = true;

        char path[64];
        sprintf(path, "devices[%d]/color", ch);
        setting["path"] = path;
      }

      for (uint8_t i = 0; i < config.devices[ch].count; i++) {
        JsonObject setting = json.createNestedObject();
        setting["type"]    = "number";
        if (i == 0)
          setting["ruler"] = true;

        char name[16];
        sprintf(name, "Channel %d", i + 1);
        setting["label"] = name;

        char path[64];
        sprintf(path, "devices[%d]/channels[%d]", ch, i);
        setting["path"] = path;
      }
    }
  }

  void importConfiguration(JsonObject json) override {
    JsonArray jsonDevices = json["devices"];
    if (jsonDevices) {
      for (uint8_t ch = 0; ch < 16; ch++) {
        if (jsonDevices[ch].isNull())
          break;

        const char *name = jsonDevices[ch]["name"];
        if (name)
          strlcpy(config.devices[ch].name, name, sizeof(config.devices[ch].name));

        if (!jsonDevices[ch]["address"].isNull()) {
          uint16_t address = jsonDevices[ch]["address"];
          if (address < 1)
            address = 1;
          else if (address > 512)
            address = 512;

          config.devices[ch].address = address - 1;
        }

        if (!jsonDevices[ch]["count"].isNull()) {
          uint16_t count = jsonDevices[ch]["count"];
          if (count > 32)
            count = 32;

          config.devices[ch].count = count;
        }

        JsonArray jsonColor = jsonDevices[ch]["color"];
        if (jsonColor) {
          uint8_t color = jsonColor[0];
          if (color > 127)
            color = 127;
          config.devices[ch].h = color;

          uint8_t saturation = jsonColor[1];
          if (saturation > 127)
            saturation = 127;
          config.devices[ch].s = saturation;

          uint8_t brightness = jsonColor[2];
          if (brightness > 127)
            brightness = 127;
          config.devices[ch].v = brightness;
        }

        JsonArray jsonChannels = jsonDevices[ch]["channels"];
        if (jsonChannels) {
          for (uint8_t i = 0; i < config.devices[ch].count; i++) {
            uint8_t value = jsonChannels[i];
            if (value > 127)
              value = 127;
            config.devices[ch].channels[i] = value;
          }
        }

        if (config.devices[ch].h > 0 || config.devices[ch].s > 0 || config.devices[ch].v > 0)
          setMode(ch, Mode::Channels);
      }
    }
  }

  void exportConfiguration(JsonObject json) override {
    json["#devices"]       = "The DMX device address, number of channels, default values.";
    JsonArray jsonDevices = json.createNestedArray("devices");
    for (uint8_t ch = 0; ch < 16; ch++) {
      JsonObject jsonDevice = jsonDevices.createNestedObject();
      jsonDevice["count"]   = config.devices[ch].count;
      jsonDevice["name"]    = config.devices[ch].name;
      jsonDevice["address"] = config.devices[ch].address + 1;

      if (config.devices[ch].count == 0)
        continue;

      if (config.devices[ch].count >= 3) {
        JsonArray jsonColor = jsonDevice.createNestedArray("color");
        jsonColor.add(config.devices[ch].h);
        jsonColor.add(config.devices[ch].s);
        jsonColor.add(config.devices[ch].v);
      }

      JsonArray jsonChannels = jsonDevice.createNestedArray("channels");
      for (uint8_t i = 0; i < config.devices[ch].count; i++)
        jsonChannels.add(config.devices[ch].channels[i]);
    }
  }

  void exportInput(JsonObject json) override {
    JsonArray jsonChannels = json.createNestedArray("channels");
    for (uint8_t ch = 0; ch < 16; ch++) {
      if (config.devices[ch].count == 0)
        continue;

      JsonObject jsonChannel = jsonChannels.createNestedObject();
      jsonChannel["name"]    = config.devices[ch].name;
      jsonChannel["number"]  = ch;

      if (ch != _currentChannel)
        continue;

      JsonArray jsonController = jsonChannel.createNestedArray("controllers");
      JsonArray jsonNotes      = jsonChannel.createNestedArray("notes");

      if (config.devices[ch].count >= 3) {
        JsonObject jsonBrightness   = jsonController.createNestedObject();
        jsonBrightness["name"]      = "Brightness";
        jsonBrightness["number"]    = (uint8_t)CC::Brightness;
        jsonBrightness["value"]     = _devices[ch].v.getMSB();
        jsonBrightness["valueFine"] = _devices[ch].v.getLSB();

        JsonObject jsonColor = jsonController.createNestedObject();
        jsonColor["name"]    = "Color";
        jsonColor["number"]  = (uint8_t)CC::Color;
        jsonColor["value"]   = (uint8_t)(_devices[ch].h / 360.f * 127.f);

        JsonObject jsonSaturation = jsonController.createNestedObject();
        jsonSaturation["name"]    = "Saturation";
        jsonSaturation["number"]  = (uint8_t)CC::Saturation;
        jsonSaturation["value"]   = (uint8_t)(_devices[ch].s * 127.f);
      }

      for (uint8_t i = 0; i < config.devices[ch].count; i++) {
        JsonObject jsonControl = jsonController.createNestedObject();
        char name[16];
        sprintf(name, "DMX #%d", i + 1);
        jsonControl["name"] = name;

        if (i < 16)
          jsonControl["number"] = V2MIDI::CC::GeneralPurpose1 + i;
        else
          jsonControl["number"] = V2MIDI::CC::Controller102 + i - 16;
        jsonControl["value"] = _devices[ch].channels[i];
      }

      // MIDI Program Change
      {
        JsonArray jsonPrograms = jsonChannel.createNestedArray("programs");
        for (uint8_t i = 0; i < (uint8_t)Program::_count; i++) {
          JsonObject jsonProgram = jsonPrograms.createNestedObject();
          jsonProgram["name"]    = _programNames[i];
          jsonProgram["number"]  = V2MIDI::GM::Program::FX5Brightness;
          jsonProgram["bank"]    = i;
          if (i == (uint8_t)_devices[ch].program)
            jsonProgram["selected"] = true;
        }
      }

      // Notes
      switch (_devices[ch].program) {
        case Program::Channels:
          if (config.devices[ch].count >= 3) {
            JsonObject jsonAftertouch = jsonChannel.createNestedObject("aftertouch");
            jsonAftertouch["value"]   = (uint8_t)(_devices[ch].note.aftertouch * 127.f);

            JsonObject jsonPitchbend = jsonChannel.createNestedObject("pitchbend");
            jsonPitchbend["value"] =
              (int16_t)(_devices[ch].note.pitchbend * (_devices[ch].note.pitchbend < 0 ? 8192.f : 8191.f));

            JsonObject jsonBrightness = jsonNotes.createNestedObject();
            jsonBrightness["name"]    = "Brightness";
            jsonBrightness["number"]  = (uint8_t)V2MIDI::C(3) + 0;

            JsonObject jsonColor = jsonNotes.createNestedObject();
            jsonColor["name"]    = "Color";
            jsonColor["number"]  = (uint8_t)V2MIDI::C(3) + 1;

            JsonObject jsonSaturation = jsonNotes.createNestedObject();
            jsonSaturation["name"]    = "Saturation";
            jsonSaturation["number"]  = (uint8_t)V2MIDI::C(3) + 2;
          }

          for (uint8_t i = 0; i < config.devices[ch].count; i++) {
            JsonObject jsonNote = jsonNotes.createNestedObject();
            char name[16];
            sprintf(name, "DMX #%d", i + 1);
            jsonNote["name"]   = name;
            jsonNote["number"] = V2MIDI::C(3) + 3 + i;
          }
          break;

        case Program::Brightness:
          JsonObject jsonChromatic = jsonChannel.createNestedObject("chromatic");
          jsonChromatic["start"]   = V2MIDI::A(-1);
          jsonChromatic["count"]   = 88;
          break;
      }
    }
  }
} Device;

// Dispatch MIDI packets
static class MIDI {
public:
  void loop() {
    if (!Device.usb.midi.receive(&_midi))
      return;

    if (_midi.getPort() == 0)
      Device.dispatch(&Device.usb.midi, &_midi);
  }

private:
  V2MIDI::Packet _midi{};
} MIDI;

static class Button : public V2Buttons::Button {
public:
  Button() : V2Buttons::Button(NULL, PIN_BUTTON) {}

private:
  void handleDown() override {
    Device.toggleState();
  }
} Button;

void setup() {
  Serial.begin(9600);

  LED.begin();
  LED.setMaxBrightness(0.5);
  DMX.begin();
  Button.begin();
  Device.begin();
  Device.reset();
  Device.readConfiguration();
}

void loop() {
  LED.loop();
  MIDI.loop();
  DMX.loop();
  V2Buttons::loop();
  Device.loop();

  if (Device.idle())
    Device.sleep();
}
