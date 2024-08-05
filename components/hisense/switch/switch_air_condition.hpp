#pragma once

#include "../air_conditioner.h"
#include "esphome/components/switch/switch.h"
#include "esphome/core/component.h"

#include <vector>

namespace esphome {
namespace hisense {
namespace ac {
static const char *const TAG = "climate.air_conditioner";

class AirConditionSwitch : public Component, public switch_::Switch {
public:
  AirConditionSwitch(){};
  void write_state(bool state) {
    parent_->set_display_switch(state);
    ESP_LOGD(TAG, "Change state: %d", state);
    this->publish_state(state);
  };
  void set_parent(AirConditioner *parent) { this->parent_ = parent; }

protected:
  AirConditioner *parent_;
};
} // namespace ac
} // namespace hisense
} // namespace esphome
