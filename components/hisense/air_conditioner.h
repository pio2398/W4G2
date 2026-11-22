#pragma once

#include "esphome/components/climate/climate.h"
#include "esphome/components/uart/uart.h"
#include "esphome/core/component.h"
#include "capabilities.h"

#include <string>
#include <vector>

#ifdef USE_SENSOR
#include "esphome/components/sensor/sensor.h"
#endif
#ifdef USE_SWITCH
#include "esphome/components/switch/switch.h"
#endif
#ifdef USE_TEXT_SENSOR
#include "esphome/components/text_sensor/text_sensor.h"
#endif
#ifdef USE_BINARY_SENSOR
#include "esphome/components/binary_sensor/binary_sensor.h"
#endif
#ifdef USE_SELECT
#include "esphome/components/select/select.h"
#endif
#ifdef USE_NUMBER
#include "esphome/components/number/number.h"
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

  // Toggleable features exposed as switches.
  enum class Feature : uint8_t { FEATURE_DISPLAY = 0, FEATURE_ECO, FEATURE_TURBO, FEATURE_QUIET, FEATURE_SLEEP,
                              FEATURE_BEEP,  // local setting: beep flag (T23 bit 2) on our commands
                              FEATURE_COUNT };
  void set_feature(Feature feature, bool state);
  void set_display_switch(bool state) { this->set_feature(Feature::FEATURE_DISPLAY, state); }

#ifdef USE_SWITCH
  void set_feature_switch(Feature feature, switch_::Switch *sw) {
    this->feature_switches_[static_cast<size_t>(feature)] = sw;
  }
#endif

#ifdef USE_TEXT_SENSOR
  void set_faults_text_sensor(text_sensor::TextSensor *s) { this->faults_text_sensor_ = s; }
  void set_identity_text_sensor(text_sensor::TextSensor *s) { this->identity_text_sensor_ = s; }
#endif
#ifdef USE_BINARY_SENSOR
  /// Status byte 36 bit 3: the unit was controlled by its IR remote or buttons since the previous command.
  void set_remote_used_binary_sensor(binary_sensor::BinarySensor *s) { this->remote_used_binary_sensor_ = s; }
#endif

  // Sleep profiles reported in status byte 15 (value >> 1) and sent in control
  // byte 17 ((profile << 1) | 1). 0 = sleep off.
  // Profiles 1..4 are General, Elderly, Young, Child (stock firmware names); the
  // select platform lists them in that order.
  static const uint8_t SLEEP_PROFILE_COUNT = 4;
  /// Profile (1..4) used when sleep is switched on. Sent at once if sleep is on and send is true.
  void set_sleep_profile(uint8_t profile, bool send);
#ifdef USE_SELECT
  void set_sleep_profile_select(select::Select *s) { this->sleep_profile_select_ = s; }
#endif
  /// 0 = detect from the unit (default), otherwise force 3 or 5 fan speeds.
  void set_fan_speeds(uint8_t speeds) { this->fan_speeds_config_ = speeds; }
  const HisenseCapabilities &get_capabilities() const { return this->caps_; }

  // On/off timers (control bytes 27/28 and 29/30, status bytes 28/29 and 30/31).
  static const uint16_t TIMER_MAX_MINUTES = 24 * 60 - 1;
  /// Set the on (off = false) or off timer to run out in `minutes`; 0 cancels it.
  void set_timer(bool off, uint16_t minutes);
#ifdef USE_NUMBER
  void set_timer_number(bool off, number::Number *n) { this->timer_numbers_[off ? 1 : 0] = n; }
#endif

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
    // Added from stock firmware analysis
    SUPPLY_VOLTAGE,
    DISCHARGE_TEMPERATURE,
    SETPOINT,
    TEMPERATURE_COMPENSATION,
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
  void decode_features_(const std::vector<uint8_t> &payload);
  climate::ClimateAction decode_action_(const std::vector<uint8_t> &payload);
  void decode_faults_(const std::vector<uint8_t> &payload);

  void send_status();

  // Identification queries sent once at start-up (version, device type, capabilities).
  static const uint8_t IDENTITY_STEPS = 3;
  uint8_t identity_step_{0};
  uint8_t identity_attempts_{0};
  void send_identity_query_();
  bool handle_identity_reply_(const std::vector<uint8_t> &payload);
  void identity_done_();
  HisenseCapabilities caps_;
  uint8_t fan_speeds_config_{0};
  bool three_speed_fan_() const {
    return this->fan_speeds_config_ ? this->fan_speeds_config_ == 3 : this->caps_.three_speed_fan();
  }
  bool legacy_power_wide_{false};  // stock firmware's sticky flag for the old power format

  std::vector<uint8_t> rx_buffer_;  // unstuffed frame being received
  bool rx_in_frame_{false};
  bool rx_pending_f4_{false};

  uint32_t last_recived_{0};
  uint32_t last_send_{0};
  uint32_t last_status_change{0};

  bool display_enable{true};  // Status of display screen of AC unit.
  bool beep_enable_{true};    // Ask the unit to beep when it accepts a command (T23 bit 2)
  uint8_t settling_features_{0};  // Feature bits changed by the last command, not yet confirmed by a poll
  uint8_t sleep_profile_{1};      // profile used when sleep is switched on
  uint8_t sleep_reported_{0};     // profile last reported by the unit, 0 = off

  uint16_t checksum(const std::vector<uint8_t> &payload);
  void change_status(Status status);

#ifdef USE_SWITCH
  switch_::Switch *feature_switches_[static_cast<size_t>(Feature::FEATURE_COUNT)]{};
  void publish_feature_(Feature feature, bool state);
#endif

#ifdef USE_TEXT_SENSOR
  text_sensor::TextSensor *faults_text_sensor_{nullptr};
  text_sensor::TextSensor *identity_text_sensor_{nullptr};
#endif
#ifdef USE_BINARY_SENSOR
  binary_sensor::BinarySensor *remote_used_binary_sensor_{nullptr};
#endif
#ifdef USE_SELECT
  select::Select *sleep_profile_select_{nullptr};
#endif
#ifdef USE_NUMBER
  number::Number *timer_numbers_[2]{};  // on, off
#endif

#ifdef USE_SENSOR
  static constexpr std::size_t SubSensorTypeCount = static_cast<std::size_t>(SubSensorType::SUB_SENSOR_TYPE_COUNT);
  void update_sub_sensor_(SubSensorType type, float value);
  std::vector<sensor::Sensor *> sub_sensors_{SubSensorTypeCount, nullptr};
#endif

  struct HvacSettings {
    esphome::optional<esphome::climate::ClimateMode> mode;
    esphome::optional<uint8_t> fan_code;  // unit's fan speed code, see fan_code_from_call_()
    esphome::optional<esphome::climate::ClimateSwingMode> swing_mode;
    esphome::optional<float> target_temperature;
    esphome::optional<esphome::climate::ClimatePreset> preset;
    esphome::optional<bool> display;
    esphome::optional<bool> eco;
    esphome::optional<bool> turbo;
    esphome::optional<bool> quiet;
    esphome::optional<uint8_t> sleep;  // sleep profile, 0 = off
    esphome::optional<uint16_t> timer[2];  // on, off: minutes from now, 0 = cancel

    HvacSettings(){};
    HvacSettings(const HvacSettings &) = default;
    HvacSettings &operator=(const HvacSettings &) = default;
  };

  optional<HvacSettings> next_hvac_settings_;
  /// Pending settings, created on demand so several changes merge into one frame.
  HvacSettings &pending_settings_() {
    if (!this->next_hvac_settings_.has_value())
      this->next_hvac_settings_ = HvacSettings();
    return *this->next_hvac_settings_;
  }

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
      case climate::CLIMATE_MODE_HEAT_COOL:
        return 4;
      case climate::CLIMATE_MODE_OFF:
        return 12;
      default:
        return -1;
    }
  }

  // Fan speed uses the unit's own code: status byte payload[14] == code << 1,
  // control byte == (code << 1) | 1. Names follow the remote/manual
  // (Auto, Lower, Low, Medium, High, Higher), with ESPHome's standard
  // LOW/MEDIUM/HIGH where they exist and custom modes for the two extremes:
  //   code 1 AUTO | 5-speed: 5 "Lower", 6 LOW, 7 MEDIUM, 8 HIGH, 9 "Higher"
  //               | 3-speed: 2 LOW, 3 MEDIUM, 4 HIGH
  static constexpr const char *FAN_LOWER = "Lower";
  static constexpr const char *FAN_HIGHER = "Higher";
  optional<uint8_t> fan_code_from_call_(const climate::ClimateCall &call);
  void publish_fan_code_(uint8_t code);
  void update_custom_fan_modes_();
};

}  // namespace ac
}  // namespace hisense
}  // namespace esphome
