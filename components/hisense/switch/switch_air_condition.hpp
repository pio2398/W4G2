#pragma once

#include "../air_conditioner.h"
#include "esphome/components/switch/switch.h"
#include "esphome/core/component.h"
#include "esphome/core/log.h"

namespace esphome {
namespace hisense {
namespace ac {

class AirConditionSwitch : public Component, public switch_::Switch {
 public:
  AirConditionSwitch(){};

  void setup() override {
    // Force the display on at every boot (upstream behaviour). Other features
    // take their state from the unit's status replies.
    if (this->feature_ == AirConditioner::Feature::FEATURE_DISPLAY)
      this->write_state(true);
    // Beep is a local setting the unit never reports: restore it (default on).
    if (this->feature_ == AirConditioner::Feature::FEATURE_BEEP)
      this->write_state(this->get_initial_state_with_restore_mode().value_or(true));
  }

  void write_state(bool state) override {
    parent_->set_feature(this->feature_, state);
    ESP_LOGD("climate.air_conditioner", "Feature %u -> %d", (unsigned) this->feature_, state);
    this->publish_state(state);
  };

  void set_parent(AirConditioner *parent) { this->parent_ = parent; }
  void set_feature(AirConditioner::Feature feature) { this->feature_ = feature; }

 protected:
  AirConditioner *parent_;
  AirConditioner::Feature feature_{AirConditioner::Feature::FEATURE_DISPLAY};
};

}  // namespace ac
}  // namespace hisense
}  // namespace esphome
