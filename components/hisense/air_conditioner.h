#pragma once

#include "esphome/components/climate/climate.h"
#include "esphome/components/uart/uart.h"
#include "esphome/core/component.h"

#include <vector>

#ifdef USE_SENSOR
#include "esphome/components/sensor/sensor.h"
#endif
#ifdef USE_SWITCH
#include "esphome/components/switch/switch.h"
#endif

namespace esphome {
namespace hisense {
namespace ac {

// Protocol notes (derived from the stock AEH-W4G2 firmware):
//  * Frame: F4 F5 | dir flags len | ... | cmd sub | data | csum_hi csum_lo | F4 FB
//  * Every 0xF4 between the F4 F5 header and the F4 FB trailer (checksum
//    included) is byte-stuffed as F4 F4.
//  * Indexes below use "payload" = unstuffed frame without the leading F4 F5,
//    i.e. payload[n] == frame[n + 2]. Status reply command 0x66 is payload[11].
//  * Control command 0x65: each field is a value plus a "write enable" bit;
//    fields left at 0 are not changed by the unit.

enum ParseStatus { NOT_FULL, PARSE_ERROR, PARSE_OK };

class AirConditioner : public uart::UARTDevice, public Component, public climate::Climate {
 public:
  AirConditioner() = default;

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override;

  climate::ClimateTraits traits() override;

  void send_raw(const std::vector<uint8_t> &payload);
  void set_flow_control_pin(GPIOPin *flow_control_pin) { this->flow_control_pin_ = flow_control_pin; }
  uint8_t waiting_for_response{0};
  void set_display_switch(bool state);

#ifdef USE_SENSOR
  enum class SubSensorType {
    // Used data based sensors
    OUTDOOR_TEMPERATURE = 0,
    HUMIDITY,
    // Big data based sensors
    INDOOR_COIL_TEMPERATURE,
    OUTDOOR_COIL_TEMPERATURE,
    INDOOR_TEMPERATURE,
    INDOOR_HUMIDITY,
    // Not my
    OUTDOOR_DEFROST_TEMPERATURE,
    OUTDOOR_IN_AIR_TEMPERATURE,
    OUTDOOR_OUT_AIR_TEMPERATURE,
    POWER,
    COMPRESSOR_FREQUENCY,
    COMPRESSOR_CURRENT,
    EXPANSION_VALVE_OPEN_DEGREE,
    SUB_SENSOR_TYPE_COUNT,
    BIG_DATA_FRAME_SUB_SENSORS = INDOOR_COIL_TEMPERATURE,
  };
  void set_sub_sensor(SubSensorType type, sensor::Sensor *sens);
#endif

 protected:
  GPIOPin *flow_control_pin_{nullptr};

  enum Status { standby, waiting_for_status_response, waiting_for_change_confirm, after_fail, waiting_for_more, reset };
  Status status = Status::standby;

  void control(const climate::ClimateCall &call) override;

  /// Read one byte from the UART (race-free on RTL8710B, see .cpp).
  bool read_rx_byte_(uint8_t *byte);
  /// Feed one received (still stuffed) byte into the frame decoder.
  ParseStatus feed_byte_(uint8_t byte);
  ParseStatus check_frame_();
  void decode_message(const std::vector<uint8_t> &payload);

  void send_status();

  std::vector<uint8_t> rx_buffer_;  // unstuffed frame being received
  bool rx_in_frame_{false};
  bool rx_pending_f4_{false};

  uint32_t last_recived_{0};
  uint32_t last_send_{0};
  uint32_t last_status_change{0};

  bool display_enable{true};  // Status of display screen of AC unit.

  uint16_t checksum(const std::vector<uint8_t> &payload);
  void change_status(Status status);

#ifdef USE_SENSOR
  static constexpr std::size_t SubSensorTypeCount = static_cast<std::size_t>(SubSensorType::SUB_SENSOR_TYPE_COUNT);
  void update_sub_sensor_(SubSensorType type, float value);
  std::vector<sensor::Sensor *> sub_sensors_{SubSensorTypeCount, nullptr};
#endif

  struct HvacSettings {
    esphome::optional<esphome::climate::ClimateMode> mode;
    esphome::optional<esphome::climate::ClimateFanMode> fan_mode;
    esphome::optional<esphome::climate::ClimateSwingMode> swing_mode;
    esphome::optional<float> target_temperature;
    esphome::optional<esphome::climate::ClimatePreset> preset;
    esphome::optional<bool> display;

    HvacSettings(){};
    HvacSettings(const HvacSettings &) = default;
    HvacSettings &operator=(const HvacSettings &) = default;
  };

  optional<HvacSettings> next_hvac_settings_;

  climate::ClimateMode decode_climateMode(const int mode) {
    if (mode == 0) {
      return climate::CLIMATE_MODE_FAN_ONLY;
    } else if (mode == 1) {
      return climate::CLIMATE_MODE_HEAT;
    } else if (mode == 2) {
      return climate::CLIMATE_MODE_COOL;
    } else if (mode == 3) {
      return climate::CLIMATE_MODE_DRY;
    } else if (mode == 4 or mode == 5 or mode == 6 or mode == 7) {
      return climate::CLIMATE_MODE_AUTO;
    } else if (mode == 12) {
      return climate::CLIMATE_MODE_OFF;
    }
    return climate::CLIMATE_MODE_OFF;
  }

  int encode_climateMode(const climate::ClimateMode mode) {
    switch (mode) {
      case climate::CLIMATE_MODE_FAN_ONLY:
        return 0;
      case climate::CLIMATE_MODE_HEAT:
        return 1;
      case climate::CLIMATE_MODE_COOL:
        return 2;
      case climate::CLIMATE_MODE_DRY:
        return 3;
      case climate::CLIMATE_MODE_AUTO:
        return 4;
      case climate::CLIMATE_MODE_OFF:
        return 12;
      default:
        return -1;
    }
  }

  int encode_fan_mode(const climate::ClimateFanMode mode) {
    switch (mode) {
      case climate::CLIMATE_FAN_OFF:
        return 0;
      case climate::CLIMATE_FAN_AUTO:
        return 1;
      case climate::CLIMATE_FAN_LOW:
        return 10;
      case climate::CLIMATE_FAN_FOCUS:
        return 12;  // matches your traits() list
      case climate::CLIMATE_FAN_MEDIUM:
        return 14;
      case climate::CLIMATE_FAN_MIDDLE:
        return 16;
      case climate::CLIMATE_FAN_HIGH:
        return 18;
      default:
        return 1;  // fall back to auto
    }
  }
};

}  // namespace ac
}  // namespace hisense
}  // namespace esphome
