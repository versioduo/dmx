// V2 - DMX
//
// © Kay Sievers 2020

#include <V2DMX.h>
#include <V2Device.h>
#include <V2LED.h>
#include <V2MIDI.h>

V2DEVICE_METADATA("com.versioduo.dmx", 18, "versioduo:samd:connect");

static V2LED LED(1, PIN_LED_WS2812, &sercom2, SERCOM2, SERCOM2_DMAC_ID_TX, SPI_PAD_0_SCK_1, PIO_SERCOM);
static V2DMX DMX(PIN_SERIAL_SOCKET_TX, &sercom3, SERCOM3, SERCOM3_DMAC_ID_TX, SPI_PAD_0_SCK_1, PIO_SERCOM);

static class Device : public V2Device {
public:
  Device() : V2Device() {
    metadata.vendor      = "Versio Duo";
    metadata.product     = "dmx";
    metadata.description = "DMX controller";
    metadata.home        = "https://versioduo.com/#dmx";

    system.ports.announce = 0;
    system.download       = "https://versioduo.com/download";

    // https://github.com/versioduo/arduino-board-package/blob/main/boards.txt
    usb.pid = 0xe940;

    configuration = {.magic{0x9e010000 | usb.pid}, .size{sizeof(config)}, .data{&config}};
  }

  // Config, written to EEPROM
  struct {
    struct {
      uint16_t address;
      uint16_t count;
      struct {
        uint8_t brightness;
        uint8_t color;
        uint8_t saturation;
      } init;
    } devices[16];
  } config{.devices{{.address{0 * 32}, .count{32}},
                    {.address{1 * 32}, .count{32}},
                    {.address{2 * 32}, .count{32}},
                    {.address{3 * 32}, .count{32}},
                    {.address{4 * 32}, .count{32}},
                    {.address{5 * 32}, .count{32}},
                    {.address{6 * 32}, .count{32}},
                    {.address{7 * 32}, .count{32}},
                    {.address{8 * 32}, .count{32}},
                    {.address{9 * 32}, .count{32}},
                    {.address{10 * 32}, .count{32}},
                    {.address{11 * 32}, .count{32}},
                    {.address{12 * 32}, .count{32}},
                    {.address{13 * 32}, .count{32}},
                    {.address{14 * 32}, .count{32}},
                    {.address{15 * 32}, .count{32}}}};

  void reset() {
    allNotesOff();
  }

  void allNotesOff() {
    digitalWrite(PIN_LED_ONBOARD, LOW);
    LED.setHSV(0, 60, 1, 0.5);
    memset(_hsv, 0, sizeof(_hsv));
    DMX.reset();

    for (uint8_t i = 0; i < 16; i++) {
      float fraction = (float)config.devices[i].init.color / 127;
      _hsv[i].hue    = fraction * 360;

      _hsv[i].saturation = (float)config.devices[i].init.saturation / 127;
      _hsv[i].value      = (float)config.devices[i].init.brightness / 127;
      setRGB(i);
    }
  }

  void loop() {}

private:
  struct {
    float hue;
    float saturation;
    float value;
  } _hsv[16];

  void setRGB(uint8_t channel) {
    // If we have fewer than three channels available, set only the brightness.
    if (config.devices[channel].count < 3) {
      DMX.setChannel(config.devices[channel].address, _hsv[channel].value * 255);
      return;
    }

    float h = _hsv[channel].hue;
    float s = _hsv[channel].saturation;
    float v = _hsv[channel].value;
    float r, g, b;

    if (fabs(h - 360) < 0.005)
      h = 0;

    h /= 60;

    int8_t i = h;
    float f  = h - i;
    float p  = v * (1 - s);
    float q  = v * (1 - f * s);
    float t  = v * (1 - (1 - f) * s);

    switch (i) {
      case 0:
        r = v;
        g = t;
        b = p;
        break;

      case 1:
        r = q;
        g = v;
        b = p;
        break;

      case 2:
        r = p;
        g = v;
        b = t;
        break;

      case 3:
        r = p;
        g = q;
        b = v;
        break;

      case 4:
        r = t;
        g = p;
        b = v;
        break;

      case 5:
        r = v;
        g = p;
        b = q;
        break;
    }

    DMX.setChannel(config.devices[channel].address + 0, r * 255);
    DMX.setChannel(config.devices[channel].address + 1, g * 255);
    DMX.setChannel(config.devices[channel].address + 2, b * 255);
  }

  void handleNote(uint8_t channel, uint8_t note, uint8_t velocity) override {
    if (note < V2MIDI::C(3))
      return;

    note -= V2MIDI::C(3);
    if (note >= 3 + 32)
      return;

    switch (note) {
      case 0: {
        float fraction      = (float)velocity / 127;
        _hsv[channel].value = fraction;
        setRGB(channel);
      } break;

      case 1: {
        float fraction    = (float)velocity / 127;
        _hsv[channel].hue = fraction * 360;
        setRGB(channel);
      } break;
        ;

      case 2: {
        float fraction           = (float)velocity / 127;
        _hsv[channel].saturation = fraction;
        setRGB(channel);
      } break;
        break;

      case 3 ... 3 + 31: {
        float fraction      = (float)velocity / 127;
        uint8_t dmx_address = note - 3;
        if (dmx_address < config.devices[channel].count)
          DMX.setChannel(config.devices[channel].address + dmx_address, fraction * 255);
      } break;
    }
  }

  void handleControlChange(uint8_t channel, uint8_t controller, uint8_t value) override {
    switch (controller) {
      case V2MIDI::CC::AllNotesOff:
        allNotesOff();
        break;

      case V2MIDI::CC::ChannelVolume: {
        float fraction      = (float)value / 127;
        _hsv[channel].value = fraction;
        setRGB(channel);
      } break;

      case V2MIDI::CC::Balance: {
        float fraction    = (float)value / 127;
        _hsv[channel].hue = fraction * 360;
        setRGB(channel);
      } break;

      case V2MIDI::CC::Expression: {
        float fraction           = (float)value / 127;
        _hsv[channel].saturation = fraction;
        setRGB(channel);
      } break;

      case V2MIDI::CC::GeneralPurpose1... V2MIDI::CC::GeneralPurpose1 + 15: {
        float fraction      = (float)value / 127;
        uint8_t dmx_address = controller - V2MIDI::CC::GeneralPurpose1;
        if (dmx_address < config.devices[channel].count)
          DMX.setChannel(config.devices[channel].address + dmx_address, fraction * 255);
      } break;

      case V2MIDI::CC::GeneralPurpose1LSB... V2MIDI::CC::GeneralPurpose1LSB + 15: {
        float fraction      = (float)value / 127;
        uint8_t dmx_address = 16 + (controller - V2MIDI::CC::GeneralPurpose1LSB);
        if (dmx_address < config.devices[channel].count)
          DMX.setChannel(config.devices[channel].address + dmx_address, fraction * 255);
      } break;
    }
  }

  void handleSystemReset() override {
    reset();
  }

  void exportInput(JsonObject json) override {
    JsonObject json_channels = json.createNestedObject("channels");
    JsonArray json_names     = json_channels.createNestedArray("names");
    for (uint8_t i = 0; i < 16; i++) {
      char name[16];
      sprintf(name, "%d – DMX #%d", i + 1, config.devices[i].address + 1);
      json_names.add(name);
    }

    {
      JsonArray json_controller  = json.createNestedArray("controllers");
      JsonObject json_brightness = json_controller.createNestedObject();
      json_brightness["name"]    = "Brightness";
      json_brightness["number"]  = (uint8_t)V2MIDI::CC::ChannelVolume;

      JsonObject json_hue = json_controller.createNestedObject();
      json_hue["name"]    = "Color";
      json_hue["number"]  = (uint8_t)V2MIDI::CC::Balance;

      JsonObject json_saturation = json_controller.createNestedObject();
      json_saturation["name"]    = "Saturation";
      json_saturation["number"]  = (uint8_t)V2MIDI::CC::Expression;

      for (uint8_t i = 0; i < 16; i++) {
        JsonObject json_address = json_controller.createNestedObject();
        char name[16];
        sprintf(name, "DMX #%d", i + 1);

        json_address["name"]   = name;
        json_address["number"] = (uint8_t)V2MIDI::CC::GeneralPurpose1 + i;
      }

      for (uint8_t i = 0; i < 16; i++) {
        JsonObject json_address = json_controller.createNestedObject();
        char name[16];
        sprintf(name, "DMX #%d", 16 + i + 1);

        json_address["name"]   = name;
        json_address["number"] = (uint8_t)V2MIDI::CC::GeneralPurpose1LSB + i;
      }
    }

    {
      JsonArray json_notes       = json.createNestedArray("notes");
      JsonObject json_brightness = json_notes.createNestedObject();
      json_brightness["name"]    = "Brightness";
      json_brightness["number"]  = (uint8_t)V2MIDI::C(3);

      JsonObject json_hue = json_notes.createNestedObject();
      json_hue["name"]    = "Color";
      json_hue["number"]  = (uint8_t)V2MIDI::C(3) + 1;

      JsonObject json_saturation = json_notes.createNestedObject();
      json_saturation["name"]    = "Saturation";
      json_saturation["number"]  = (uint8_t)V2MIDI::C(3) + 2;

      for (uint8_t i = 0; i < 32; i++) {
        char name[11];
        sprintf(name, "DMX #%d", i + 1);

        JsonObject json_note = json_notes.createNestedObject();
        json_note["name"]    = name;
        json_note["number"]  = V2MIDI::C(3) + 3 + i;
      }
    }
  }

  void importConfiguration(JsonObject json) override {
    JsonArray json_devices = json["devices"];
    if (json_devices) {
      for (uint8_t i = 0; i < 16; i++) {
        if (json_devices[i].isNull())
          break;

        uint16_t address = json_devices[i]["address"];
        if (address < 1)
          address = 1;
        else if (address > 512)
          address = 512;

        uint16_t count = json_devices[i]["count"];
        if (count > 512)
          count = 512;

        config.devices[i].address = address - 1;
        config.devices[i].count   = count;

        JsonObject json_init = json_devices[i]["init"];
        if (json_init) {
          uint8_t brightness = json_init["brightness"];
          if (brightness > 127)
            brightness = 127;
          config.devices[i].init.brightness = brightness;

          uint8_t color = json_init["color"];
          if (color > 127)
            color = 127;
          config.devices[i].init.color = color;

          uint8_t saturation = json_init["saturation"];
          if (saturation > 127)
            saturation = 127;
          config.devices[i].init.saturation = saturation;
        }
      }
    }
  }

  void exportConfiguration(JsonObject json) override {
    json["#devices"]       = "The DMX device address for every MIDI channel";
    JsonArray json_devices = json.createNestedArray("devices");
    for (uint8_t i = 0; i < 16; i++) {
      JsonObject json_device = json_devices.createNestedObject();
      json_device["address"] = config.devices[i].address + 1;
      json_device["count"]   = config.devices[i].count;

      JsonObject json_init    = json_device.createNestedObject("init");
      json_init["brightness"] = config.devices[i].init.brightness;
      json_init["color"]      = config.devices[i].init.color;
      json_init["saturation"] = config.devices[i].init.saturation;
    }
  }
} Device;

// Dispatch MIDI packets
static class MIDI {
public:
  MIDI() {
    _midi.setSystemExclusiveBuffer(_sysex_buffer, sizeof(_sysex_buffer));
  }

  void loop() {
    if (!Device.usb.midi.receive(&_midi))
      return;

    if (_midi.getPort() == 0) {
      // Simple messages are dispatched immediately, if it is a sysex message,
      // we store the chunk of the message in our packet and receive() again
      // until it is complete.
      if (!_midi.storeSystemExclusive())
        return;

      Device.dispatchMIDI(&Device.usb.midi, &_midi);
    }
  }

private:
  V2MIDI::Packet _midi;
  uint8_t _sysex_buffer[12 * 1024];
} MIDI;

void setup() {
  Serial.begin(9600);

  static Adafruit_USBD_WebUSB WebUSB;
  static WEBUSB_URL_DEF(WEBUSBLandingPage, 1 /*https*/, "versioduo.com/configure");
  WebUSB.begin();
  WebUSB.setLandingPage(&WEBUSBLandingPage);

  LED.begin();
  LED.setMaxBrightness(0.5);
  DMX.begin();
  Device.begin();
  Device.reset();
}

void loop() {
  LED.loop();
  MIDI.loop();
  DMX.loop();
  Device.loop();

  if (Device.idle())
    Device.sleep();
}
