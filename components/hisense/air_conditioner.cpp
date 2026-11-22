#include "air_conditioner.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

#include <cmath>

#ifdef USE_LIBRETINY_VARIANT_RTL8710B
#include "esphome/components/uart/uart_component_libretiny.h"
#include <ArduinoPrivate.h>
#endif

namespace esphome {
namespace hisense {
namespace ac {
static const char *const TAG = "climate.air_conditioner";

#ifdef USE_LIBRETINY_VARIANT_RTL8710B
// Workarounds for LibreTiny on RTL8710B (AmebaZ), the AEH-W4G2's chip:
//  * LibreTiny >= 1.12 calls UART_Init() after enabling RX, which clears the RX
//    interrupt, so the port never receives (libretiny-eu/libretiny#366).
//  * The Arduino RingBuffer updates its element count from both the RX interrupt
//    (++) and read() (--) without locking. An interrupt landing inside read()
//    loses an increment, after which the last bytes of every frame stay
//    unreadable until more data arrives (libretiny-eu/libretiny#419). Masking
//    the UART RX interrupt around reads avoids it; the 16-byte hardware FIFO
//    holds incoming bytes meanwhile.
static UART_TypeDef *ambz_uart(uart::UARTComponent *parent) {
  auto *lt = static_cast<uart::LibreTinyUARTComponent *>(parent);
  const int8_t idx = lt->get_hw_serial_number();
  return (idx >= 0 && idx <= 2) ? UART_DEV_TABLE[idx].UARTx : nullptr;
}
#endif

static const uint8_t FRAME_ESC = 0xF4;
static const uint8_t FRAME_START = 0xF5;
static const uint8_t FRAME_END = 0xFB;
static const size_t MAX_FRAME_LEN = 512;

void AirConditioner::setup() {
  if (this->flow_control_pin_ != nullptr) {
    this->flow_control_pin_->setup();
    this->flow_control_pin_->digital_write(false);
  }
  this->last_status_change = millis();
  this->status = Status::standby;
  this->update_custom_fan_modes_();
#ifdef USE_LIBRETINY_VARIANT_RTL8710B
  if (auto *u = ambz_uart(this->parent_)) {
    UART_RxCmd(u, ENABLE);
    UART_INTConfig(u, RUART_IER_ERBI, ENABLE);
  }
#endif
}

bool AirConditioner::read_rx_byte_(uint8_t *byte) {
#ifdef USE_LIBRETINY_VARIANT_RTL8710B
  UART_TypeDef *u = ambz_uart(this->parent_);
  if (u != nullptr)
    UART_INTConfig(u, RUART_IER_ERBI, DISABLE);
  const bool ok = this->available() && this->read_byte(byte);
  if (u != nullptr)
    UART_INTConfig(u, RUART_IER_ERBI, ENABLE);
  return ok;
#else
  return this->available() && this->read_byte(byte);
#endif
}

void AirConditioner::loop() {
  if (millis() - this->last_recived_ > 10000 && millis() - this->last_status_change > 10000) {
    ESP_LOGD(TAG, "Stuck reset error");
    this->rx_buffer_.clear();
    this->rx_in_frame_ = false;
    this->rx_pending_f4_ = false;
    change_status(Status::after_fail);
    return;
  }

  if (status == Status::after_fail) {
    this->rx_buffer_.clear();
    this->rx_in_frame_ = false;
    this->rx_pending_f4_ = false;
    uint8_t byte;
    while (this->read_rx_byte_(&byte)) {
    }
    if (millis() - last_status_change > 3000) {
      ESP_LOGD(TAG, "Reset status.");
      change_status(Status::standby);
    }
    return;
  }

  // The bus is half duplex: only send once the previous exchange is finished
  // (reply received or timed out), otherwise the command collides with the
  // unit's reply and is lost. Queued changes keep merging meanwhile.
  if (this->next_hvac_settings_ && status == Status::standby) {
    send_status();
    change_status(Status::waiting_for_change_confirm);
    return;
  }

  // A frame that stopped mid-way, or a request that got no reply: recover quickly
  // instead of waiting for the 10 s stuck timer.
  if (status == Status::waiting_for_more && millis() - this->last_recived_ > 1000) {
    ESP_LOGD(TAG, "Incomplete frame (%u bytes), dropping", (unsigned) this->rx_buffer_.size());
    change_status(Status::after_fail);
    return;
  }
  if ((status == Status::waiting_for_status_response || status == Status::waiting_for_change_confirm) &&
      millis() - this->last_send_ > 2000) {
    ESP_LOGD(TAG, "No reply to request");
    change_status(Status::standby);
    return;
  }

  if (millis() - this->last_recived_ > 2000 && status == Status::standby) {
    ESP_LOGV(TAG, "Requesting status.");
    // F4 F5 00 40 0C 00 00 01 01 FE 01 00 00 66 00 00 00 01 B3 F4 FB
    std::vector<uint8_t> requst_status{0xF4, 0xF5, 0x00, 0x40, 0x0C, 0x00, 0x00, 0x01, 0x01,
                                       0xFE, 0x01, 0x00, 0x00, 0x66, 0x00, 0x00, 0x00};
    this->send_raw(requst_status);
    change_status(Status::waiting_for_status_response);
    return;
  }

  uint8_t byte;
  while (this->read_rx_byte_(&byte)) {
    this->last_recived_ = millis();
    switch (this->feed_byte_(byte)) {
      case ParseStatus::PARSE_ERROR:
        ESP_LOGD(TAG, "Parse error");
        change_status(Status::after_fail);
        return;
      case ParseStatus::PARSE_OK:
        change_status(Status::standby);
        return;
      case ParseStatus::NOT_FULL:
        if (this->rx_in_frame_)
          change_status(Status::waiting_for_more);
        break;
    }
  }
}

ParseStatus AirConditioner::feed_byte_(uint8_t byte) {
  if (this->rx_pending_f4_) {
    this->rx_pending_f4_ = false;
    if (byte == FRAME_START) {
      // (Re)start of frame. Anything collected so far is dropped.
      if (this->rx_in_frame_ && this->rx_buffer_.size() > 2)
        ESP_LOGD(TAG, "Frame restarted, dropping %u bytes", (unsigned) this->rx_buffer_.size());
      this->rx_buffer_.assign({FRAME_ESC, FRAME_START});
      this->rx_in_frame_ = true;
      return ParseStatus::NOT_FULL;
    }
    if (!this->rx_in_frame_)
      return ParseStatus::NOT_FULL;
    if (byte == FRAME_ESC) {
      // Stuffed literal 0xF4.
      this->rx_buffer_.push_back(FRAME_ESC);
    } else if (byte == FRAME_END) {
      this->rx_buffer_.push_back(FRAME_ESC);
      this->rx_buffer_.push_back(FRAME_END);
      this->rx_in_frame_ = false;
      auto result = this->check_frame_();
      this->rx_buffer_.clear();
      return result;
    } else {
      // Unescaped F4 inside a frame: not valid, keep it to be lenient.
      this->rx_buffer_.push_back(FRAME_ESC);
      this->rx_buffer_.push_back(byte);
    }
  } else if (byte == FRAME_ESC) {
    this->rx_pending_f4_ = true;
    return ParseStatus::NOT_FULL;
  } else if (this->rx_in_frame_) {
    this->rx_buffer_.push_back(byte);
  }

  if (this->rx_buffer_.size() > MAX_FRAME_LEN) {
    ESP_LOGD(TAG, "Frame too long");
    this->rx_buffer_.clear();
    this->rx_in_frame_ = false;
    return ParseStatus::PARSE_ERROR;
  }
  return ParseStatus::NOT_FULL;
}

ParseStatus AirConditioner::check_frame_() {
  const auto &buf = this->rx_buffer_;
  if (buf.size() < 10) {
    ESP_LOGD(TAG, "Frame too short: %u", (unsigned) buf.size());
    return ParseStatus::PARSE_ERROR;
  }
  std::vector<uint8_t> payload_for_crc(buf.begin(), buf.end() - 4);
  const uint16_t csum = this->checksum(payload_for_crc);
  const uint8_t cr1 = (csum >> 8) & 0xFF;
  const uint8_t cr2 = csum & 0xFF;
  const uint8_t provided_cr1 = buf[buf.size() - 4];
  const uint8_t provided_cr2 = buf[buf.size() - 3];
  if (cr1 != provided_cr1 || cr2 != provided_cr2) {
    ESP_LOGD(TAG, "Not valid checksum, expected: 0x%02X 0x%02X, got 0x%02X 0x%02X.", cr1, cr2, provided_cr1,
             provided_cr2);
    return ParseStatus::PARSE_ERROR;
  }

  std::vector<uint8_t> payload(buf.begin() + 2, buf.end() - 4);
  ESP_LOGV(TAG, "Frame OK (%u bytes): %s", (unsigned) payload.size(), format_hex_pretty(payload).c_str());

  if (payload.size() > 12 && (payload[11] == 0x66 || payload[11] == 0x65) && payload[12] == 0x00) {
    // 0x66 = status reply; 0x65 = reply to a control command, which carries the
    // full status in the same layout (verified live).
    this->decode_message(payload);
  } else {
    ESP_LOGD(TAG, "Non-status frame (cmd 0x%02X, %u bytes)", payload.size() > 11 ? payload[11] : 0,
             (unsigned) payload.size());
  }

  if (waiting_for_response) {
    waiting_for_response = 0;
  } else {
    ESP_LOGD(TAG, "Not expected");
  }
  return ParseStatus::PARSE_OK;
}

void AirConditioner::update_custom_fan_modes_() { this->set_supported_custom_fan_modes({FAN_LOWER, FAN_HIGHER}); }

optional<uint8_t> AirConditioner::fan_code_from_call_(const climate::ClimateCall &call) {
  if (call.has_custom_fan_mode()) {
    auto name = call.get_custom_fan_mode();
    if (name == FAN_LOWER)
      return 5;
    if (name == FAN_HIGHER)
      return 9;
    return {};
  }
  if (!call.get_fan_mode().has_value())
    return {};
  switch (*call.get_fan_mode()) {
    case climate::CLIMATE_FAN_AUTO:
      return 1;
    case climate::CLIMATE_FAN_LOW:
      return 6;
    case climate::CLIMATE_FAN_MEDIUM:
      return 7;
    case climate::CLIMATE_FAN_HIGH:
      return 8;
    default:
      return {};
  }
}

void AirConditioner::publish_fan_code_(uint8_t code) {
  switch (code) {
    case 1:
      this->set_fan_mode_(climate::CLIMATE_FAN_AUTO);
      break;
    case 2:
    case 6:
      this->set_fan_mode_(climate::CLIMATE_FAN_LOW);
      break;
    case 3:
    case 7:
      this->set_fan_mode_(climate::CLIMATE_FAN_MEDIUM);
      break;
    case 4:
    case 8:
      this->set_fan_mode_(climate::CLIMATE_FAN_HIGH);
      break;
    case 5:
      this->set_custom_fan_mode_(FAN_LOWER);
      break;
    case 9:
      this->set_custom_fan_mode_(FAN_HIGHER);
      break;
    default:
      ESP_LOGD(TAG, "Unknown fan code %u", code);
      break;
  }
}

float AirConditioner::get_setup_priority() const {
  // After UART bus
  return setup_priority::BUS - 1.0f;
}

climate::ClimateTraits AirConditioner::traits() {
  auto traits = climate::ClimateTraits();

  traits.set_supported_modes({climate::CLIMATE_MODE_OFF, climate::CLIMATE_MODE_COOL, climate::CLIMATE_MODE_HEAT,
                              climate::CLIMATE_MODE_FAN_ONLY, climate::CLIMATE_MODE_DRY,
                              climate::CLIMATE_MODE_AUTO});

  // 5-speed units add the custom "Lower" and "Higher" modes (update_custom_fan_modes_).
  traits.set_supported_fan_modes(
      {climate::CLIMATE_FAN_AUTO, climate::CLIMATE_FAN_LOW, climate::CLIMATE_FAN_MEDIUM, climate::CLIMATE_FAN_HIGH});

  traits.set_supported_swing_modes({climate::CLIMATE_SWING_OFF, climate::CLIMATE_SWING_BOTH,
                                    climate::CLIMATE_SWING_VERTICAL, climate::CLIMATE_SWING_HORIZONTAL});

  traits.add_feature_flags(climate::CLIMATE_SUPPORTS_CURRENT_TEMPERATURE);
  traits.set_visual_min_temperature(16.0f);
  traits.set_visual_max_temperature(32.0f);
  traits.set_visual_target_temperature_step(1.0f);
  traits.set_visual_current_temperature_step(1.0f);

  return traits;
}

void AirConditioner::send_status() {
  if (this->next_hvac_settings_ == nullopt) {
    ESP_LOGD(TAG, "send_status nullopt");
    return;
  }
  auto next_hvac_settings = next_hvac_settings_.value();
  this->next_hvac_settings_ = nullopt;

  std::vector<uint8_t> status(46, 0x00);
  const uint8_t header[] = {0xF4, 0xF5, 0x00, 0x40, 0x29, 0x00, 0x00, 0x01, 0x01, 0xFE, 0x01, 0x00, 0x00, 0x65};
  std::copy(std::begin(header), std::end(header), status.begin());
  // Byte 23 bit 2: the unit beeps when it accepts the frame (stock module always sets it).
  status[23] = this->beep_enable_ ? 0x04 : 0x00;

  if (next_hvac_settings.mode != nullopt) {
    if (next_hvac_settings.mode.value() == climate::CLIMATE_MODE_OFF) {
      status[18] = 0b00000100;  // power write-enable, power = 0
    } else {
      int mode = encode_climateMode(next_hvac_settings.mode.value());
      if (mode < 0 || mode > 7) {
        ESP_LOGW(TAG, "Unsupported mode %d", (int) next_hvac_settings.mode.value());
        return;
      }
      // mode in bits 5-7 (+ write enable bit 4), power on bit 3 (+ write enable bit 2)
      status[18] = (uint8_t) ((mode << 5) | 0x10 | 0x0C);
      if (next_hvac_settings.target_temperature == nullopt && !std::isnan(this->target_temperature))
        next_hvac_settings.target_temperature = this->target_temperature;
    }
  }

  if (next_hvac_settings.target_temperature != nullopt) {
    int my_tmp = (int) lroundf(next_hvac_settings.target_temperature.value());
    if (my_tmp < 16 || my_tmp > 32) {
      ESP_LOGW(TAG, "Target temperature %d out of range 16..32, not sent", my_tmp);
    } else {
      status[19] = (uint8_t) ((my_tmp << 1) | 1);
    }
  }

  // Display: bit 7 value, bit 6 write enable. Only sent when changing it; the
  // unit reports the display off while powered off, so re-sending a cached
  // value with every command could switch it off unintentionally.
  if (next_hvac_settings.display.has_value()) {
    this->display_enable = *next_hvac_settings.display;
    status[36] = this->display_enable ? 0b11000000 : 0b01000000;
  }

  if (next_hvac_settings.swing_mode != nullopt) {
    // bit7 up/down (+bit6 write enable), bit5 left/right (+bit4 write enable)
    switch (next_hvac_settings.swing_mode.value()) {
      case climate::CLIMATE_SWING_BOTH:
        status[32] = 0b11110000;
        break;
      case climate::CLIMATE_SWING_OFF:
        status[32] = 0b01010000;
        break;
      case climate::CLIMATE_SWING_VERTICAL:
        status[32] = 0b11010000;
        break;
      case climate::CLIMATE_SWING_HORIZONTAL:
        status[32] = 0b01110000;
        break;
      default:
        break;
    }
  }

  if (next_hvac_settings.fan_code.has_value())
    status[16] = (uint8_t) ((*next_hvac_settings.fan_code << 1) | 1);

  auto settling = [this](Feature f) { this->settling_features_ |= 1u << static_cast<uint8_t>(f); };
  if (next_hvac_settings.display.has_value())
    settling(Feature::FEATURE_DISPLAY);
  if (next_hvac_settings.sleep.has_value())
    settling(Feature::FEATURE_SLEEP);
  if (next_hvac_settings.turbo.has_value())
    settling(Feature::FEATURE_TURBO);
  if (next_hvac_settings.eco.has_value())
    settling(Feature::FEATURE_ECO);
  if (next_hvac_settings.quiet.has_value())
    settling(Feature::FEATURE_QUIET);

  if (next_hvac_settings.sleep.has_value())
    status[17] = (uint8_t) ((*next_hvac_settings.sleep << 1) | 1);
  if (next_hvac_settings.turbo.has_value())
    status[33] |= (uint8_t) (((*next_hvac_settings.turbo ? 1 : 0) << 3) | 0x04);
  if (next_hvac_settings.eco.has_value())
    status[33] |= (uint8_t) (((*next_hvac_settings.eco ? 1 : 0) << 5) | 0x10);
  if (next_hvac_settings.quiet.has_value())
    status[35] |= (uint8_t) (((*next_hvac_settings.quiet ? 1 : 0) << 5) | 0x10);

  // On timer in bytes 27/28, off timer in 29/30 (straga xm_KTKT / xm_KTGT):
  //   first byte:  hours << 3 | 0x04 (hours valid) | on << 1 | 0x01 (on valid)
  //   second byte: minutes << 2 | 0x01 (minutes valid)
  // The stock app sends a clock time together with the unit clock (bytes 25/26).
  // The unit reports the time remaining, so the clock is sent as 00:00 and the
  // timer as the delay: that works whether the unit takes it as a delay or a time.
  bool timer_sent = false;
  for (int i = 0; i < 2; i++) {
    if (!next_hvac_settings.timer[i].has_value())
      continue;
    const uint16_t minutes = *next_hvac_settings.timer[i];
    const size_t b = i == 0 ? 27 : 29;
    if (minutes == 0) {
      status[b] = 0x01;  // timer off
    } else {
      status[b] = (uint8_t) (((minutes / 60) << 3) | 0x04 | 0x02 | 0x01);
      status[b + 1] = (uint8_t) (((minutes % 60) << 2) | 0x01);
      timer_sent = true;
    }
  }
  if (timer_sent) {
    status[25] = 0x01;  // clock hours = 0, valid
    status[26] = 0x01;  // clock minutes = 0, valid
  }

  send_raw(status);
  ESP_LOGD(TAG, "send_status");
}

void AirConditioner::control(const climate::ClimateCall &call) {
  auto &next = this->pending_settings_();
  if (call.get_mode().has_value())
    next.mode = call.get_mode();
  if (auto code = this->fan_code_from_call_(call))
    next.fan_code = code;
  if (call.get_swing_mode().has_value())
    next.swing_mode = call.get_swing_mode();
  if (call.get_target_temperature().has_value())
    next.target_temperature = call.get_target_temperature();
  if (call.get_preset().has_value())
    next.preset = call.get_preset();
}

void AirConditioner::set_feature(Feature feature, bool state) {
  if (feature == Feature::FEATURE_BEEP) {
    // Not a unit setting: only changes how our own commands are sent.
    this->beep_enable_ = state;
    return;
  }
  auto &next = this->pending_settings_();
  switch (feature) {
    case Feature::FEATURE_DISPLAY:
      next.display = state;
      this->display_enable = state;
      break;
    case Feature::FEATURE_ECO:
      next.eco = state;
      break;
    case Feature::FEATURE_TURBO:
      next.turbo = state;
      break;
    case Feature::FEATURE_QUIET:
      next.quiet = state;
      break;
    case Feature::FEATURE_SLEEP:
      next.sleep = state ? this->sleep_profile_ : 0;
      break;
    default:
      break;
  }
}

void AirConditioner::set_timer(bool off, uint16_t minutes) {
  if (minutes > TIMER_MAX_MINUTES)
    minutes = TIMER_MAX_MINUTES;
  this->pending_settings_().timer[off ? 1 : 0] = minutes;
}

void AirConditioner::set_sleep_profile(uint8_t profile, bool send) {
  if (profile < 1 || profile > SLEEP_PROFILE_COUNT)
    return;
  this->sleep_profile_ = profile;
  if (!send)
    return;
  // Apply at once if sleep is on, or about to be switched on by a queued command.
  bool queued_on = this->next_hvac_settings_.has_value() && this->next_hvac_settings_->sleep.has_value() &&
                   *this->next_hvac_settings_->sleep != 0;
  bool queued_off = this->next_hvac_settings_.has_value() && this->next_hvac_settings_->sleep.has_value() &&
                    *this->next_hvac_settings_->sleep == 0;
  if (queued_on || (this->sleep_reported_ != 0 && !queued_off))
    this->pending_settings_().sleep = profile;
}

void AirConditioner::dump_config() {
  ESP_LOGCONFIG(TAG, "Hisense:");
  LOG_PIN("  Flow Control Pin: ", this->flow_control_pin_);
}

void AirConditioner::send_raw(const std::vector<uint8_t> &payload) {
  if (payload.size() < 2)
    return;
  const uint16_t csum = this->checksum(payload);

  // Everything between the F4 F5 header and the F4 FB trailer is byte-stuffed.
  std::vector<uint8_t> frame(payload.begin(), payload.begin() + 2);
  frame.reserve(payload.size() + 8);
  auto put = [&frame](uint8_t b) {
    frame.push_back(b);
    if (b == FRAME_ESC)
      frame.push_back(FRAME_ESC);
  };
  for (size_t i = 2; i < payload.size(); i++)
    put(payload[i]);
  put((csum >> 8) & 0xFF);
  put(csum & 0xFF);
  frame.push_back(FRAME_ESC);
  frame.push_back(FRAME_END);

  if (this->flow_control_pin_ != nullptr)
    this->flow_control_pin_->digital_write(true);

  this->write_array(frame);
  this->flush();

  if (this->flow_control_pin_ != nullptr)
    this->flow_control_pin_->digital_write(false);
  waiting_for_response = 1;

  last_send_ = millis();
}

uint16_t AirConditioner::checksum(const std::vector<uint8_t> &payload) {
  uint16_t csum = 0;
  for (size_t i = 2; i < payload.size(); i++)
    csum += payload[i];
  return csum;
}

void AirConditioner::decode_message(const std::vector<uint8_t> &payload) {
  // Need at least up to the swing byte.
  if (payload.size() < 34) {
    ESP_LOGD(TAG, "Status frame too short: %u", (unsigned) payload.size());
    return;
  }

  if ((payload[16] & 0b00001000) != 0) {
    this->mode = decode_climateMode(payload[16] >> 4);
  } else {
    this->mode = climate::CLIMATE_MODE_OFF;
  }

  this->target_temperature = payload[17];
  this->current_temperature = payload[18] <= 128 ? (float) payload[18] : NAN;

  // payload[14] = fan code << 1. Some units report auto as 0x01. 0 while off: keep last.
  if (payload[14] == 0x01) {
    this->publish_fan_code_(1);
  } else if (payload[14] != 0) {
    this->publish_fan_code_(payload[14] >> 1);
  }

  {
    // bit7 = up/down louver (shown as vertical swing), bit6 = left/right.
    bool up_down = (payload[33] & 0b10000000) != 0;
    bool left_right = (payload[33] & 0b01000000) != 0;
    if (up_down && left_right) {
      this->swing_mode = climate::CLIMATE_SWING_BOTH;
    } else if (up_down) {
      this->swing_mode = climate::CLIMATE_SWING_VERTICAL;
    } else if (left_right) {
      this->swing_mode = climate::CLIMATE_SWING_HORIZONTAL;
    } else {
      this->swing_mode = climate::CLIMATE_SWING_OFF;
    }
  }
  this->publish_state();

  this->decode_features_(payload);
  this->decode_faults_(payload);
#ifdef USE_BINARY_SENSOR
  // Byte 36 bit 3 (straga's field table: "IR remote or key control before this
  // command"); bit 2 is the same for Wi-Fi control. Never seen set on product type 104.
  if (this->remote_used_binary_sensor_ != nullptr && payload.size() > 36)
    this->remote_used_binary_sensor_->publish_state((payload[36] & 0x08) != 0);
#endif
#ifdef USE_NUMBER
  // On/off timers, as time remaining (also when set with the remote): bytes 28/29
  // (on) and 30/31 (off). First byte bit 1 = set, bits 7-3 = hours; second byte
  // bits 7-2 = minutes. The unit counts them down once a minute and acts when they
  // reach zero. Published in minutes, 0 = not set. The reply to a timer command
  // can still show the old timers, so only polls are used.
  if (payload[11] == 0x66 && !(this->next_hvac_settings_.has_value() &&
                               (this->next_hvac_settings_->timer[0].has_value() ||
                                this->next_hvac_settings_->timer[1].has_value()))) {
    for (int i = 0; i < 2; i++) {
      const size_t b = i == 0 ? 28 : 30;
      const float v = (payload[b] & 0x02) ? (float) ((payload[b] >> 3) * 60 + (payload[b + 1] >> 2)) : 0.0f;
      auto *n = this->timer_numbers_[i];
      if (n != nullptr && (!n->has_state() || n->state != v))
        n->publish_state(v);
    }
  }
#endif

#ifdef USE_SENSOR
  auto byte_or_nan = [&payload](size_t idx) -> float {
    if (idx >= payload.size())
      return NAN;
    return payload[idx] == 0x80 ? NAN : (float) payload[idx];  // 0x80 = not supported
  };
  update_sub_sensor_(SubSensorType::INDOOR_TEMPERATURE, byte_or_nan(18));
  update_sub_sensor_(SubSensorType::INDOOR_COIL_TEMPERATURE, byte_or_nan(19));
  update_sub_sensor_(SubSensorType::INDOOR_HUMIDITY, byte_or_nan(21));
  // Outdoor unit (verified live; the bytes 40/41 used previously are compressor
  // frequencies). Outdoor temperatures are signed.
  auto signed_or_nan = [&payload](size_t idx) -> float {
    if (idx >= payload.size() || payload[idx] == 0x80)
      return NAN;
    return (float) (int8_t) payload[idx];
  };
  update_sub_sensor_(SubSensorType::OUTDOOR_TEMPERATURE, signed_or_nan(42));
  update_sub_sensor_(SubSensorType::OUTDOOR_COIL_TEMPERATURE, signed_or_nan(43));
  // Compressor discharge temperature (stock firmware field table; rises with the compressor).
  update_sub_sensor_(SubSensorType::DISCHARGE_TEMPERATURE, byte_or_nan(44));
  // Setpoint as reported by the unit (byte 17), as a sensor so its history can be
  // graphed, e.g. to see whether a sleep curve shows up in the reported setpoint.
  update_sub_sensor_(SubSensorType::SETPOINT, (float) payload[17]);
  // Byte 24 bits 7-4: auto/dry temperature compensation (stock firmware
  // t_temp_compensate; the remote offers -7..+7). Encoding not confirmed: raw value.
  update_sub_sensor_(SubSensorType::TEMPERATURE_COMPENSATION, (float) (payload[24] >> 4));
  // Compressor output frequency in Hz (ramps; byte 39 is the target frequency).
  if (payload.size() > 41)
    update_sub_sensor_(SubSensorType::COMPRESSOR_FREQUENCY, (float) payload[41]);
  if (payload.size() > 48)
    update_sub_sensor_(SubSensorType::SUPPLY_VOLTAGE, (float) ((payload[47] << 8) | payload[48]));
  // f_electricity in the stock firmware (units not confirmed).
  if (payload.size() > 143) {
    // Little-endian: byte 142 is the low byte. It is a copy of bytes 53:54
    // (big-endian) on the units seen so far, e.g. 53:54 = 06 40, 142:143 = 40 06.
    uint16_t power = (payload[143] << 8) | payload[142];
    ESP_LOGV(TAG, "Power raw 0x%04X", power);
    update_sub_sensor_(SubSensorType::POWER, (float) power);
  }
#endif
}

void AirConditioner::decode_features_(const std::vector<uint8_t> &payload) {
  // The unit's reply to a control command (0x65) can still show the old value of
  // a feature it was just told to change (seen live for the display), so those
  // features are only read back from the next status poll (0x66).
  const bool command_reply = payload[11] == 0x65;
  auto settled = [this, command_reply](Feature f) {
    return !command_reply || !(this->settling_features_ & (1u << static_cast<uint8_t>(f)));
  };
  if (settled(Feature::FEATURE_SLEEP)) {
    this->sleep_reported_ = payload[15] >> 1;
    // While sleep is on, the unit's profile is the one to show (and to reuse).
    if (this->sleep_reported_ >= 1 && this->sleep_reported_ <= SLEEP_PROFILE_COUNT &&
        !this->next_hvac_settings_.has_value()) {
      this->sleep_profile_ = this->sleep_reported_;
#ifdef USE_SELECT
      auto *sel = this->sleep_profile_select_;
      if (sel != nullptr) {
        auto index = sel->active_index();
        auto option = sel->at(this->sleep_profile_ - 1);
        if (option.has_value() && (!sel->has_state() || !index.has_value() || *index != this->sleep_profile_ - 1u))
          sel->publish_state(*option);
      }
#endif
    }
  }
#ifdef USE_SWITCH
  if (settled(Feature::FEATURE_SLEEP))
    this->publish_feature_(Feature::FEATURE_SLEEP, (payload[15] >> 1) != 0);
  if (settled(Feature::FEATURE_TURBO))
    this->publish_feature_(Feature::FEATURE_TURBO, (payload[33] & 0b00000010) != 0);
  if (settled(Feature::FEATURE_ECO))
    this->publish_feature_(Feature::FEATURE_ECO, (payload[33] & 0b00000100) != 0);
  if (payload.size() > 34 && settled(Feature::FEATURE_QUIET))
    this->publish_feature_(Feature::FEATURE_QUIET, (payload[34] & 0b00000100) != 0);
#endif
  if (payload.size() > 35 && !this->next_hvac_settings_.has_value() && settled(Feature::FEATURE_DISPLAY)) {
    // Display on/off is reported in bit 7 of byte 35 (verified live).
    this->display_enable = (payload[35] & 0x80) != 0;
#ifdef USE_SWITCH
    this->publish_feature_(Feature::FEATURE_DISPLAY, this->display_enable);
#endif
  }
  if (!command_reply)
    this->settling_features_ = 0;
}

void AirConditioner::decode_faults_(const std::vector<uint8_t> &payload) {
#ifdef USE_TEXT_SENSOR
  if (this->faults_text_sensor_ == nullptr)
    return;
  struct FaultBit {
    uint8_t index;
    uint8_t mask;
    const char *name;
  };
  static const FaultBit FAULTS[] = {
      {37, 0x80, "indoor temp sensor"},
      {37, 0x40, "indoor coil sensor"},
      {37, 0x20, "humidity sensor"},
      {37, 0x10, "water full"},
      {37, 0x08, "indoor fan motor"},
      {37, 0x04, "grille protection"},
      {37, 0x02, "zero-cross detection"},
      {37, 0x01, "indoor/outdoor comms"},
      {38, 0x80, "display board"},
      {38, 0x40, "keypad"},
      {38, 0x20, "wifi module"},
      {38, 0x10, "electrical"},
      {38, 0x08, "indoor EEPROM"},
      {62, 0x40, "outdoor EEPROM"},
      {62, 0x20, "outdoor coil sensor"},
      {62, 0x10, "discharge temp sensor"},
      {62, 0x08, "outdoor temp sensor"},
      {64, 0x10, "over-heat/over-cold protection"},
  };
  std::string faults;
  for (const auto &f : FAULTS) {
    if (f.index < payload.size() && (payload[f.index] & f.mask)) {
      if (!faults.empty())
        faults += ", ";
      faults += f.name;
    }
  }
  if (faults.empty())
    faults = "OK";
  if (!this->faults_text_sensor_->has_state() || this->faults_text_sensor_->get_state() != faults)
    this->faults_text_sensor_->publish_state(faults);
#endif
}

#ifdef USE_SWITCH
void AirConditioner::publish_feature_(Feature feature, bool state) {
  auto *sw = this->feature_switches_[static_cast<size_t>(feature)];
  if (sw == nullptr)
    return;
  // Don't overwrite a change that is queued but not yet sent.
  if (this->next_hvac_settings_.has_value())
    return;
  if (!sw->has_state() || sw->state != state)
    sw->publish_state(state);
}
#endif

#ifdef USE_SENSOR
void AirConditioner::set_sub_sensor(SubSensorType type, sensor::Sensor *sens) {
  std::size_t index = static_cast<std::size_t>(type);
  if (index < sub_sensors_.size()) {
    sub_sensors_[index] = sens;
  } else {
    ESP_LOGE(TAG, "Not valid sensor index: %d", index);
  }
}

void AirConditioner::update_sub_sensor_(SubSensorType type, float value) {
  std::size_t index = static_cast<std::size_t>(type);
  if (index < sub_sensors_.size() && sub_sensors_[index] != nullptr) {
    sub_sensors_[index]->publish_state(value);
  }
}
#endif

void AirConditioner::change_status(Status new_status) {
  if (new_status != this->status)
    ESP_LOGV(TAG, "Status %d -> %d", (int) this->status, (int) new_status);
  this->status = new_status;
  last_status_change = millis();
}

}  // namespace ac
}  // namespace hisense
}  // namespace esphome
