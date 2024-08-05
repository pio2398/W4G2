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

enum ParseStatus {
  NOT_FULL,
  PARSE_ERROR,
  PARSE_OK
};

class AirConditioner : public uart::UARTDevice,
                       public Component,
                       public climate::Climate {
public:
  AirConditioner() = default;

  void setup() override;

  void loop() override;

  void dump_config() override;

  climate::ClimateTraits traits() override;

  void send_raw(const std::vector<uint8_t> &payload);
  void set_flow_control_pin(GPIOPin *flow_control_pin) {
    this->flow_control_pin_ = flow_control_pin;
  }
  uint8_t waiting_for_response{0};
  void set_display_switch(bool state);

protected:
  GPIOPin *flow_control_pin_{nullptr};

  enum Status {
    standby,
    waiting_for_status_response,
    waiting_for_change_confirm,
    after_fail,
    waiting_for_more,
    reset
  };
  Status status = Status::standby;

  /// Override control to change settings of the climate device.
  void control(const climate::ClimateCall &call) override;

  ParseStatus parse_ac_message_byte_();

  void decode_message(std::vector<uint8_t>);

  void send_status();

  std::vector<uint8_t> rx_buffer_;
  uint32_t last_recived_{0};
  uint32_t last_send_{0};
  uint32_t last_status_change{0};
  uint32_t last_extra_log_print{0};
  bool print_extra_log_in_this_loop{true};

  bool display_enable {true}; // Status of display screen of AC unit.


  float get_setup_priority() const override;

  short int checksum(const std::vector<uint8_t> &payload);

  void change_status(Status status);

  
#ifdef USE_SENSOR
 public:
  enum class SubSensorType {
    // Used data based sensors
    OUTDOOR_TEMPERATURE = 0,
    HUMIDITY,
    // Big data based sensors
    INDOOR_COIL_TEMPERATURE,
    OUTDOOR_COIL_TEMPERATURE,
    INDOOR_TEMPERATURE,
    INDOOR_HUMIDITY,
    //Not my
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

 protected:
  static constexpr std::size_t SubSensorTypeCount = static_cast<std::size_t>(SubSensorType::SUB_SENSOR_TYPE_COUNT);
  void update_sub_sensor_(SubSensorType type, float value);
  std::vector<sensor::Sensor*> sub_sensors_{SubSensorTypeCount, nullptr};

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


};



} // namespace ac
} // namespace hisense
} // namespace esphome