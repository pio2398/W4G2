#include "air_conditioner.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

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

float AirConditioner::get_setup_priority() const {
  // After UART bus
  return setup_priority::BUS - 1.0f;
}

climate::ClimateTraits AirConditioner::traits() {
  auto traits = climate::ClimateTraits();

  traits.set_supported_modes({climate::CLIMATE_MODE_OFF, climate::CLIMATE_MODE_COOL, climate::CLIMATE_MODE_HEAT,
                              climate::CLIMATE_MODE_FAN_ONLY, climate::CLIMATE_MODE_DRY,
                              climate::CLIMATE_MODE_HEAT_COOL});

  traits.set_supported_fan_modes({climate::CLIMATE_FAN_OFF, climate::CLIMATE_FAN_AUTO, climate::CLIMATE_FAN_LOW,
                                  climate::CLIMATE_FAN_FOCUS, climate::CLIMATE_FAN_MEDIUM, climate::CLIMATE_FAN_MIDDLE,
                                  climate::CLIMATE_FAN_HIGH});

  traits.set_supported_swing_modes({climate::CLIMATE_SWING_OFF, climate::CLIMATE_SWING_BOTH,
                                    climate::CLIMATE_SWING_VERTICAL, climate::CLIMATE_SWING_HORIZONTAL});

  traits.add_feature_flags(climate::CLIMATE_SUPPORTS_CURRENT_TEMPERATURE);
  traits.set_visual_target_temperature_step(1.0f);

  return traits;
}
void AirConditioner::send_status() {
  if (this->next_hvac_settings_ == nullopt) {
    ESP_LOGD(TAG, "send_status nullopt");
    return;
  }
  auto next_hvac_settings = next_hvac_settings_.value();

  std::vector<uint8_t> status{
      0xF4,
      0xF5,  // 1
      0x00,  // 2
      0x40,  // 3
      0x29,  // 4
      0x00,  // 5
      0x00,  // 6
      0x01,  // 7
      0x01,  // 8
      0xFE,  // 9
      0x01,  // 10
      0x00,  // 11
      0x00,  // 12
      0x65,  // 13
      0x00,  // 14
      0x00,  // 15
      0x00,  // 16
      0x00,  // 17
      0x00,  // 18
      0x00,  // 19
      0x00,  // 20
      0x00,  // 21
      0x00,  // 22
      0x04,  // 23
      0x00,  // 24
      0x00,  // 25
      0x00,  // 26
      0x00,  // 27
      0x00,  // 28
      0x00,  // 29
      0x00,  // 30
      0x00,  // 31
      0x00,  // 32
      0x00,  // 33
      0x00,  // 34
      0x00,  // 35
      0x00,  // 36
      0x00,  // 37
      0x00,  // 38
      0x00,  // 39
      0x00,  // 40
      0x00,  // 41
      0x00,  // 42
      0x00,  // 43
      0x00,  // 44
      0x00   // 45
  };

  if (next_hvac_settings.mode != nullopt) {
    int mode = encode_climateMode((next_hvac_settings.mode.value()));

    if (next_hvac_settings.mode.value() == climate::CLIMATE_MODE_OFF) {
      status[18] = 0b00000100;

    } else {
      uint8_t m_mode = mode << 1;
      m_mode |= (1ul << 0);
      m_mode = m_mode << 4;
      m_mode |= 0b00001100;  // To on

      status[18] = m_mode;
      next_hvac_settings.target_temperature = this->target_temperature;
    }
  }

  if (next_hvac_settings.target_temperature != nullopt) {
    auto my_tmp = static_cast<uint8_t>(next_hvac_settings.target_temperature.value());
    if (!(my_tmp > 16ul || my_tmp < 30ul)) {
      ESP_LOGD(TAG, "Not valid tmp!");
      return;
    }

    uint8_t c = my_tmp;
    uint8_t tempX = 0;
    tempX = (c << 1);
    tempX |= (1ul << 0);
    status[19] = tempX;
  }

  status[36] = 0b01000000;

  if (display_enable) {
    status[36] = 0b11000000;
  } else {
    status[36] = 0b01000000;
  }

  if (next_hvac_settings.swing_mode != nullopt) {
    // TODO: Combine with decoding
    auto next_swing_setting = next_hvac_settings.swing_mode;
    if (next_swing_setting == climate::ClimateSwingMode::CLIMATE_SWING_BOTH) {
      status[32] = 0b11110000;
    } else if (next_swing_setting == climate::ClimateSwingMode::CLIMATE_SWING_OFF) {
      status[32] = 0b01010000;
    } else if (next_swing_setting == climate::ClimateSwingMode::CLIMATE_SWING_VERTICAL) {
      status[32] = 0b11010000;
    } else if (next_swing_setting == climate::ClimateSwingMode::CLIMATE_SWING_HORIZONTAL) {
      status[32] = 0b01110000;
    }
  }

  if (next_hvac_settings.fan_mode != nullopt) {
    uint8_t fan_raw = static_cast<uint8_t>(encode_fan_mode(next_hvac_settings.fan_mode.value()));
    uint8_t fanX = (fan_raw << 1);
    fanX |= (1ul << 0);
    status[16] = fanX;
  }

  send_raw(status);
  this->next_hvac_settings_ = nullopt;
  ESP_LOGD(TAG, "send_status");
}

void AirConditioner::control(const climate::ClimateCall &call) {
  auto next_hvac_settings = HvacSettings();

  if (call.get_mode().has_value())
    next_hvac_settings.mode = call.get_mode();
  if (call.get_fan_mode().has_value())
    next_hvac_settings.fan_mode = call.get_fan_mode();
  if (call.get_swing_mode().has_value())
    next_hvac_settings.swing_mode = call.get_swing_mode();
  if (call.get_target_temperature().has_value())
    next_hvac_settings.target_temperature = call.get_target_temperature();
  if (call.get_preset().has_value())
    next_hvac_settings.preset = call.get_preset();

  this->next_hvac_settings_ = std::move(next_hvac_settings);
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

  static optional<climate::ClimateFanMode> decode_wind_codes[19]{
      climate::ClimateFanMode::CLIMATE_FAN_OFF,
      climate::ClimateFanMode::CLIMATE_FAN_AUTO,
      climate::ClimateFanMode::CLIMATE_FAN_AUTO,
      nullopt,
      nullopt,
      nullopt,
      nullopt,
      nullopt,
      nullopt,
      nullopt,
      climate::ClimateFanMode::CLIMATE_FAN_LOW,  //"1",
      nullopt,
      climate::ClimateFanMode::CLIMATE_FAN_FOCUS,  //"2",
      nullopt,
      climate::ClimateFanMode::CLIMATE_FAN_MEDIUM,  //"3",
      nullopt,
      climate::ClimateFanMode::CLIMATE_FAN_MIDDLE,  //"4",
      nullopt,
      climate::ClimateFanMode::CLIMATE_FAN_HIGH,  //"5"
  };

  // Is ON
  {
    uint8_t mask = 0b00001000;
    bool is_on = ((payload[16] & mask) != 0);
    if (is_on) {
      auto z = decode_climateMode((payload[16] >> 4));
      this->mode = z;
      ESP_LOGD(TAG, "mode %d", z);
    } else {
      ESP_LOGD(TAG, "Seem to be OFF.");
      this->mode = climate::CLIMATE_MODE_OFF;
    }
  }

  // Target temp
  {
    auto temp = payload[17];
    this->target_temperature = temp;
  }

  // Fan speed
  {
    if (payload[14] < sizeof(decode_wind_codes) / sizeof(decode_wind_codes[0])) {
      auto wind = decode_wind_codes[payload[14]];
      this->fan_mode = wind;
    }
  }

  // Swig detect
  {
    uint8_t updown_mask = 0b01000000;
    bool up_down = (payload[33] & updown_mask) != 0;

    uint8_t lr_mask = 0b10000000;
    bool left_right = (payload[33] & lr_mask) != 0;

    if (left_right && up_down) {
      swing_mode = climate::ClimateSwingMode::CLIMATE_SWING_BOTH;
    } else if (left_right) {
      swing_mode = climate::ClimateSwingMode::CLIMATE_SWING_VERTICAL;
    } else if (up_down) {
      swing_mode = climate::ClimateSwingMode::CLIMATE_SWING_HORIZONTAL;
    } else {
      swing_mode = climate::ClimateSwingMode::CLIMATE_SWING_OFF;
    }
  }
  ESP_LOGD(TAG, "acc status new");
  this->publish_state();

#ifdef USE_SENSOR
  {
    update_sub_sensor_(SubSensorType::INDOOR_TEMPERATURE, static_cast<float>(payload[18]));
    update_sub_sensor_(SubSensorType::INDOOR_COIL_TEMPERATURE, static_cast<float>(payload[19]));

    update_sub_sensor_(SubSensorType::INDOOR_HUMIDITY, static_cast<float>(payload[21]));
    if (payload.size() > 41) {
      update_sub_sensor_(SubSensorType::OUTDOOR_TEMPERATURE, static_cast<float>(payload[40]));
      update_sub_sensor_(SubSensorType::OUTDOOR_COIL_TEMPERATURE, static_cast<float>(payload[41]));
    }

    //  update_sub_sensor_(SubSensorType::OUTDOOR_TEMPERATURE,
    //  static_cast<float>(payload[41])); //OutDoor coil?
  }
#endif
}

#ifdef USE_SENSOR
void AirConditioner::set_sub_sensor(SubSensorType type, sensor::Sensor *sens) {
  std::size_t index = static_cast<std::size_t>(type);
  if (index < sub_sensors_.size()) {
    sub_sensors_[index] = sens;
  } else {
    ESP_LOGE(TAG, "Not valid sensor index: %d", index);
  }
};
void AirConditioner::update_sub_sensor_(SubSensorType type, float value) {
  std::size_t index = static_cast<std::size_t>(type);
  if (index < sub_sensors_.size() && sub_sensors_[index] != nullptr) {
    sub_sensors_[index]->publish_state(value);
  } else {
    ESP_LOGE(TAG, "Not valid sensor index: %d", index);
  }
};
#endif

void AirConditioner::set_display_switch(bool state) {
  auto next_hvac_settings = HvacSettings();
  next_hvac_settings.display = state;
  this->next_hvac_settings_ = std::move(next_hvac_settings);
  display_enable = state;
}

void AirConditioner::change_status(Status new_status) {
  if (new_status != this->status)
    ESP_LOGV(TAG, "Status %d -> %d", (int) this->status, (int) new_status);
  this->status = new_status;
  last_status_change = millis();
}

}  // namespace ac
}  // namespace hisense
}  // namespace esphome
