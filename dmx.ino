// © Kay Sievers <kay@vrfy.org>, 2020-2021
// SPDX-License-Identifier: Apache-2.0

#include <V2Buttons.h>
#include <V2Color.h>
#include <V2DMX.h>
#include <V2Device.h>
#include <V2LED.h>
#include <V2MIDI.h>
#include <V2Music.h>

V2DEVICE_METADATA("com.versioduo.dmx", 35, "versioduo:samd:dmx");

static V2LED LED(2, PIN_LED_WS2812, &sercom2, SPI_PAD_0_SCK_1, PIO_SERCOM);
static V2DMX DMX(PIN_DMX_TX, &sercom3, SPI_PAD_0_SCK_1, PIO_SERCOM);

static class Device : public V2Device {
public:
  Device() : V2Device() {
    metadata.vendor      = "Versio Duo";
    metadata.product     = "dmx";
    metadata.description = "DMX Controller";
    metadata.home        = "https://versioduo.com/#dmx";

    system.download       = "https://versioduo.com/download";
    system.configure      = "https://versioduo.com/configure";
    system.ports.announce = 0;

    // https://github.com/versioduo/arduino-board-package/blob/main/boards.txt
    usb.pid = 0xe940;

    configuration = {.magic{0x9e030000 | usb.pid}, .size{sizeof(config)}, .data{&config}};
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
    {{.name = "Albert"}, .address{0 * 5}, .count{5}},
    {{.name = "Berta"}, .address{1 * 5}, .count{5}},
    {{.name = "Cäsar"}, .address{2 * 5}, .count{5}},
    {{.name = "David"}, .address{3 * 5}, .count{5}},
    {{.name = "Emil"}, .address{4 * 5}, .count{5}},
    {{.name = "Friedrich"}, .address{5 * 5}},
    {{.name = "Gustav"}, .address{6 * 5}},
    {{.name = "Heinrich"}, .address{7 * 5}},
    {{.name = "Ida"}, .address{8 * 32}},
    {{.name = "Jakob"}, .address{9 * 5}},
    {{.name = "Katharina"}, .address{10 * 5}},
    {{.name = "Ludwig"}, .address{11 * 5}},
    {{.name = "Marie"}, .address{12 * 5}},
    {{.name = "Nathan"}, .address{13 * 5}},
    {{.name = "Otto"}, .address{14 * 5}},
    {{.name = "Paula"}, .address{15 * 5}},
  }};

  void readConfig() {
    bool values{};

    for (uint8_t ch = 0; ch < 16; ch++) {
      // Set default values from EEPROM.
      if (config.devices[ch].count >= 3 && config.devices[ch].v > 0) {
        _devices[ch].h = (float)config.devices[ch].h / 127.f * 360.f;
        _devices[ch].s = (float)config.devices[ch].s / 127.f;
        _devices[ch].v.setFraction((uint8_t)CC::Brightness, (float)config.devices[ch].v / 127.f);
        setDMXHSV(ch, _devices[ch].h, _devices[ch].s, _devices[ch].v.getFraction());
        _devices[ch].mode = Mode::HSV;

        for (uint8_t i = 3; i < config.devices[ch].count; i++) {
          _devices[ch].channels[i] = config.devices[ch].channels[i];
          setDMX(ch, i, (float)config.devices[ch].channels[i] / 127.f);
        }

        values = true;

      } else {
        for (uint8_t i = 0; i < config.devices[ch].count; i++) {
          if (config.devices[ch].channels[i] == 0)
            continue;

          _devices[ch].channels[i] = config.devices[ch].channels[i];
          setDMX(ch, i, (float)config.devices[ch].channels[i] / 127.f);
          values = true;
        }

        if (config.devices[ch].count >= 3)
          _devices[ch].mode = Mode::RGB;
      }
    }

    if (values)
      setState(State::Config);
  }

  void reset() {
    digitalWrite(PIN_LED_ONBOARD, LOW);
    LED.reset();
    DMX.reset();
    _force.reset();
    for (uint8_t i = 0; i < 16; i++)
      _devices[i] = {};
    setState(State::Off);
  }

  void deviceNotesOff(uint8_t channel) {
    _devices[channel].note = {};
    _devices[channel].playing.reset();

    if (_devices[channel].mode == Mode::HSV) {
      setDMXHSV(channel, _devices[channel].h, _devices[channel].s, _devices[channel].v.getFraction());
      for (uint8_t i = 3; i < config.devices[channel].count; i++)
        setDMX(channel, i, (float)_devices[channel].channels[i] / 127.f);

    } else
      for (uint8_t i = 0; i < config.devices[channel].count; i++)
        setDMX(channel, i, (float)_devices[channel].channels[i] / 127.f);
  }

  void allNotesOff() {
    if (_force.trigger()) {
      reset();
      return;
    }

    for (uint8_t ch = 0; ch < 16; ch++)
      deviceNotesOff(ch);
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
        readConfig();
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
  const char *_program_names[(uint8_t)Program::_count]{"Channels", "Brightness"};

  // The current mode of the first three channels;
  enum class Mode { Init, HSV, RGB };

  struct {
    Program program;

    // Controllers
    Mode mode;
    float h;
    float s;
    V2MIDI::CC::HighResolution<(uint8_t)CC::Brightness> v;
    uint8_t channels[32];

    // Notes
    V2Music::Playing<88> playing;
    struct {
      float aftertouch;
      float pitchbend;
      float h;
      float s;
      float v;
    } note;
  } _devices[16]{};

  void switchMode(uint8_t channel, Mode mode) {
    if (config.devices[channel].count < 3)
      return;

    _devices[channel].mode = mode;

    switch (mode) {
      case Mode::HSV:
        _devices[channel].channels[0] = 0;
        _devices[channel].channels[1] = 0;
        _devices[channel].channels[2] = 0;
        break;

      case Mode::RGB:
        _devices[channel].h = 0;
        _devices[channel].s = 0;
        _devices[channel].v.set((uint8_t)CC::Brightness, 0);
        break;
    }
  }

  // Update a single DMX channel.
  void setDMX(uint8_t channel, uint8_t address, float fraction) {
    if (address >= config.devices[channel].count)
      return;

    DMX.setChannel(config.devices[channel].address + address, roundf(fraction * 255.f));
  }

  // Update the DMX RGB channels with the given HSV values.
  void setDMXHSV(uint8_t channel, float h, float s, float v) {
    if (config.devices[channel].count < 3)
      return;

    uint8_t r, g, b;
    V2Color::HSVtoRGB(h, s, v, r, g, b);
    DMX.setChannel(config.devices[channel].address + 0, r);
    DMX.setChannel(config.devices[channel].address + 1, g);
    DMX.setChannel(config.devices[channel].address + 2, b);
  }

  // Update the DMX channels for the duration of the notes. The brightness and color
  // are also modulated by aftertouch and pitch bend.
  void setDMXHSVNote(uint8_t channel) {
    if (config.devices[channel].count < 3)
      return;

    bool on = false;

    float h;
    if (_devices[channel].note.h > 0) {
      h  = _devices[channel].note.h;
      on = true;

    } else
      h = _devices[channel].h;

    float s;
    if (_devices[channel].note.s > 0) {
      s  = _devices[channel].note.s;
      on = true;

    } else
      s = _devices[channel].s;

    float v;
    if (_devices[channel].note.v > 0) {
      v  = _devices[channel].note.v;
      on = true;

    } else
      v = _devices[channel].v.getFraction();

    if (on) {
      h += (_devices[channel].note.pitchbend * 180.f);
      if (h > 360)
        h -= 360;
      else if (h < 0)
        h += 360;

      if (_devices[channel].note.aftertouch > 0)
        v = _devices[channel].note.aftertouch;

      setDMXHSV(channel, h, s, v);

    } else
      deviceNotesOff(channel);
  }

  // Notes temporarily overwrite the values set by the controllers. The Note-Off will
  // restore the value.
  void handleNote(uint8_t channel, uint8_t note, uint8_t velocity) override {
    switch (_devices[channel].program) {
      case Program::Channels: {
        switch (note) {
          case V2MIDI::C(3):
            if (config.devices[channel].count >= 3) {
              if (velocity == 0)
                _devices[channel].note.aftertouch = 0;
              _devices[channel].note.v = (float)velocity / 127.f;
              setDMXHSVNote(channel);
            }
            break;

          case V2MIDI::Cs(3):
            if (config.devices[channel].count >= 3) {
              if (velocity == 0)
                _devices[channel].note.pitchbend = 0;
              _devices[channel].note.h = (float)velocity / 127.f * 360.f;
              setDMXHSVNote(channel);
            }
            break;

          case V2MIDI::D(3):
            if (config.devices[channel].count >= 3) {
              _devices[channel].note.s = (float)velocity / 127.f;
              setDMXHSVNote(channel);
            }
            break;

          case V2MIDI::Ds(3)... V2MIDI::Ds(3) + 31: {
            const uint8_t address = note - V2MIDI::Ds(3);
            if (address < config.devices[channel].count) {
              const uint8_t v = velocity > 0 ? v : _devices[channel].channels[address];
              setDMX(channel, address, (float)v / 127.f);
            }
          } break;
        }
      } break;

      case Program::Brightness:
        if (note < V2MIDI::A(-1))
          return;

        if (note >= V2MIDI::A(-1) + 88)
          return;

        _devices[channel].playing.update(note, velocity);
        // Restore previous note.
        if (velocity == 0) {
          uint8_t n;
          uint8_t v;
          if (_devices[channel].playing.getLast(n, v))
            velocity = v;
        }

        if (velocity == 0) {
          deviceNotesOff(channel);
          break;
        }

        if (_devices[channel].mode == Mode::HSV) {
          // Change the HSV brightness.
          setDMXHSV(channel,
                    _devices[channel].h,
                    _devices[channel].s,
                    velocity > 0 ? (float)velocity / 127.f : _devices[channel].v.getFraction());

        } else {
          // Change first DMX channel only.
          setDMX(channel, 0, velocity > 0 ? (float)velocity / 127.f : (float)_devices[channel].channels[0] / 127.f);
        }
        break;
    }

    setState(State::MIDI);
  }

  void handleNoteOff(uint8_t channel, uint8_t note, uint8_t velocity) override {
    handleNote(channel, note, 0);
  }

  void handleProgramChange(uint8_t channel, uint8_t program) override {
    if (program >= (uint8_t)Program::_count)
      return;

    _devices[channel].program = (Program)program;
    _devices[channel].playing.reset();
  }

  void handleControlChange(uint8_t channel, uint8_t controller, uint8_t value) override {
    switch (controller) {
      case V2MIDI::CC::AllSoundOff:
      case V2MIDI::CC::AllNotesOff:
        allNotesOff();
        break;

      case (uint8_t)CC::Brightness:
      case V2MIDI::CC::ControllerLSB + (uint8_t)CC::Brightness:
        if (!_devices[channel].v.setByte(controller, value))
          break;
        switchMode(channel, Mode::HSV);
        setDMXHSV(channel, _devices[channel].h, _devices[channel].s, _devices[channel].v.getFraction());
        break;

      case (uint8_t)CC::Color:
        _devices[channel].h = (float)value / 127.f * 360.f;
        switchMode(channel, Mode::HSV);
        setDMXHSV(channel, _devices[channel].h, _devices[channel].s, _devices[channel].v.getFraction());
        break;

      case (uint8_t)CC::Saturation:
        _devices[channel].s = (float)value / 127.f;
        switchMode(channel, Mode::HSV);
        setDMXHSV(channel, _devices[channel].h, _devices[channel].s, _devices[channel].v.getFraction());
        break;

      case V2MIDI::CC::GeneralPurpose1... V2MIDI::CC::GeneralPurpose1 + 15: {
        const uint8_t address               = controller - V2MIDI::CC::GeneralPurpose1;
        _devices[channel].channels[address] = value;
        if (address <= 2)
          switchMode(channel, Mode::RGB);
        setDMX(channel, address, (float)value / 127.f);
      } break;

      case V2MIDI::CC::Controller102... V2MIDI::CC::Controller102 + 15: {
        const uint8_t address               = 16 + (controller - V2MIDI::CC::Controller102);
        _devices[channel].channels[address] = value;
        setDMX(channel, address, (float)value / 127.f);
      } break;
    }

    setState(State::MIDI);
  }

  void handleAftertouchChannel(uint8_t channel, uint8_t pressure) override {
    switch (_devices[channel].program) {
      case Program::Channels:
        _devices[channel].note.aftertouch = (float)pressure / 127.f;
        setDMXHSVNote(channel);
        break;
    }
  }

  void handlePitchBend(uint8_t channel, int16_t value) override {
    switch (_devices[channel].program) {
      case Program::Channels:
        _devices[channel].note.pitchbend = (float)value / (value < 0 ? 8192.f : 8191.f);
        setDMXHSVNote(channel);
        break;
    }
  }

  void handleSystemReset() override {
    reset();
    readConfig();
  }

  void exportSettings(JsonArray json) override {
    for (uint8_t ch = 0; ch < 16; ch++) {
      if (config.devices[ch].count == 0)
        continue;

      {
        JsonObject json_text = json.createNestedObject();
        json_text["type"]    = "text";

        json_text["label"] = "Name";
        char name[16];
        sprintf(name, "Channel %d", ch + 1);
        json_text["title"] = name;

        // The object in the configuration record.
        JsonObject json_configuration = json_text.createNestedObject("configuration");
        json_configuration["path"]    = "devices";
        json_configuration["index"]   = ch;
        json_configuration["field"]   = "name";
      }

      if (config.devices[ch].count >= 3) {
        JsonObject json_color = json.createNestedObject();
        json_color["type"]    = "color";

        // The object in the configuration record.
        JsonObject json_configuration = json_color.createNestedObject("configuration");
        json_configuration["path"]    = "devices";
        json_configuration["index"]   = ch;
        json_configuration["field"]   = "color";
      }
    }
  }

  void importConfiguration(JsonObject json) override {
    JsonArray json_devices = json["devices"];
    if (json_devices) {
      for (uint8_t ch = 0; ch < 16; ch++) {
        if (json_devices[ch].isNull())
          break;

        const char *name = json_devices[ch]["name"];
        if (name)
          strlcpy(config.devices[ch].name, name, sizeof(config.devices[ch].name));

        if (!json_devices[ch]["address"].isNull()) {
          uint16_t address = json_devices[ch]["address"];
          if (address < 1)
            address = 1;
          else if (address > 512)
            address = 512;

          config.devices[ch].address = address - 1;
        }

        if (!json_devices[ch]["count"].isNull()) {
          uint16_t count = json_devices[ch]["count"];
          if (count > 32)
            count = 32;

          config.devices[ch].count = count;
        }

        JsonArray json_color = json_devices[ch]["color"];
        if (json_color) {
          uint8_t color = json_color[0];
          if (color > 127)
            color = 127;
          config.devices[ch].h = color;

          uint8_t saturation = json_color[1];
          if (saturation > 127)
            saturation = 127;
          config.devices[ch].s = saturation;

          uint8_t brightness = json_color[2];
          if (brightness > 127)
            brightness = 127;
          config.devices[ch].v = brightness;
        }

        JsonArray json_channels = json_devices[ch]["channels"];
        if (json_channels) {
          for (uint8_t i = 0; i < config.devices[ch].count; i++) {
            uint8_t value = json_channels[i];
            if (value > 127)
              value = 127;
            config.devices[ch].channels[i] = value;
          }
        }
      }
    }
  }

  void exportConfiguration(JsonObject json) override {
    json["#devices"]       = "The DMX device address, number of channels, default values.";
    JsonArray json_devices = json.createNestedArray("devices");
    for (uint8_t ch = 0; ch < 16; ch++) {
      JsonObject json_device = json_devices.createNestedObject();
      json_device["name"]    = config.devices[ch].name;
      json_device["address"] = config.devices[ch].address + 1;
      json_device["count"]   = config.devices[ch].count;

      if (config.devices[ch].count == 0)
        continue;

      if (config.devices[ch].count >= 3) {
        JsonArray json_color = json_device.createNestedArray("color");
        json_color.add(config.devices[ch].h);
        json_color.add(config.devices[ch].s);
        json_color.add(config.devices[ch].v);
      }

      JsonArray json_channels = json_device.createNestedArray("channels");
      for (uint8_t i = 0; i < config.devices[ch].count; i++)
        json_channels.add(config.devices[ch].channels[i]);
    }
  }

  void exportInput(JsonObject json) override {
    JsonArray json_channels = json.createNestedArray("channels");
    for (uint8_t ch = 0; ch < 16; ch++) {
      if (config.devices[ch].count == 0)
        continue;

      JsonObject json_channel = json_channels.createNestedObject();
      json_channel["name"]    = config.devices[ch].name;
      json_channel["number"]  = ch;

      JsonArray json_controller = json_channel.createNestedArray("controllers");
      JsonArray json_notes      = json_channel.createNestedArray("notes");

      if (config.devices[ch].count >= 3) {
        JsonObject json_brightness   = json_controller.createNestedObject();
        json_brightness["name"]      = "Brightness";
        json_brightness["number"]    = (uint8_t)CC::Brightness;
        json_brightness["value"]     = _devices[ch].v.getMSB();
        json_brightness["valueFine"] = _devices[ch].v.getLSB();

        JsonObject json_color = json_controller.createNestedObject();
        json_color["name"]    = "Color";
        json_color["number"]  = (uint8_t)CC::Color;
        json_color["value"]   = (uint8_t)(_devices[ch].h / 360.f * 127.f);

        JsonObject json_saturation = json_controller.createNestedObject();
        json_saturation["name"]    = "Saturation";
        json_saturation["number"]  = (uint8_t)CC::Saturation;
        json_saturation["value"]   = (uint8_t)(_devices[ch].s * 127.f);
      }

      for (uint8_t i = 0; i < config.devices[ch].count; i++) {
        JsonObject json_control = json_controller.createNestedObject();
        char name[16];
        sprintf(name, "DMX #%d", i + 1);
        json_control["name"] = name;

        if (i < 16)
          json_control["number"] = V2MIDI::CC::GeneralPurpose1 + i;
        else
          json_control["number"] = V2MIDI::CC::Controller102 + i - 16;
        json_control["value"] = _devices[ch].channels[i];
      }

      // MIDI Program Change
      {
        JsonArray json_programs = json_channel.createNestedArray("programs");
        for (uint8_t i = 0; i < (uint8_t)Program::_count; i++) {
          JsonObject json_program = json_programs.createNestedObject();
          json_program["name"]    = _program_names[i];
          json_program["number"]  = i;
          if (i == (uint8_t)_devices[ch].program)
            json_program["selected"] = true;
        }
      }

      // Notes
      switch (_devices[ch].program) {
        case Program::Channels:
          if (config.devices[ch].count >= 3) {
            JsonObject json_aftertouch = json_channel.createNestedObject("aftertouch");
            json_aftertouch["value"]   = (uint8_t)(_devices[ch].note.aftertouch * 127.f);

            JsonObject json_pitchbend = json_channel.createNestedObject("pitchbend");
            json_pitchbend["value"] =
              (int16_t)(_devices[ch].note.pitchbend * (_devices[ch].note.pitchbend < 0 ? 8192.f : 8191.f));

            JsonObject json_brightness = json_notes.createNestedObject();
            json_brightness["name"]    = "Brightness";
            json_brightness["number"]  = (uint8_t)V2MIDI::C(3) + 0;

            JsonObject json_color = json_notes.createNestedObject();
            json_color["name"]    = "Color";
            json_color["number"]  = (uint8_t)V2MIDI::C(3) + 1;

            JsonObject json_saturation = json_notes.createNestedObject();
            json_saturation["name"]    = "Saturation";
            json_saturation["number"]  = (uint8_t)V2MIDI::C(3) + 2;
          }

          for (uint8_t i = 0; i < config.devices[ch].count; i++) {
            JsonObject json_note = json_notes.createNestedObject();
            char name[16];
            sprintf(name, "DMX #%d", i + 1);
            json_note["name"]   = name;
            json_note["number"] = V2MIDI::C(3) + 3 + i;
          }
          break;

        case Program::Brightness:
          JsonObject json_chromatic = json_channel.createNestedObject("chromatic");
          json_chromatic["start"]   = V2MIDI::A(-1);
          json_chromatic["count"]   = 88;
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
  Device.readConfig();
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
