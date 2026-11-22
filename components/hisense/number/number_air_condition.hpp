#pragma once

#include "../air_conditioner.h"
#include "esphome/components/number/number.h"

namespace esphome {
namespace hisense {
namespace ac {

// On or off timer, in minutes from now. The unit's countdown is read back, so a
// timer set with the remote shows up too. 0 = no timer; setting 0 cancels it.
class AirConditionTimerNumber : public number::Number {
 public:
  void set_parent(AirConditioner *parent, bool off) {
    this->parent_ = parent;
    this->off_ = off;
  }

 protected:
  void control(float value) override {
    this->parent_->set_timer(this->off_, (uint16_t) value);
    this->publish_state(value);
  }

  AirConditioner *parent_;
  bool off_{false};
};

}  // namespace ac
}  // namespace hisense
}  // namespace esphome
