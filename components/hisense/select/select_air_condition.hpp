#pragma once

#include "../air_conditioner.h"
#include "esphome/components/select/select.h"
#include "esphome/core/component.h"
#include "esphome/core/preferences.h"

namespace esphome {
namespace hisense {
namespace ac {

// Sleep profile used when the sleep switch is turned on. While sleep is on,
// the unit's own profile is read back and shown here. The last choice is
// restored at boot.
class AirConditionSleepProfileSelect : public Component, public select::Select {
 public:
  void setup() override {
    this->pref_ = global_preferences->make_preference<uint8_t>(this->get_object_id_hash());
    uint8_t index = 0;
    if (!this->pref_.load(&index) || index >= AirConditioner::SLEEP_PROFILE_COUNT)
      index = 0;
    this->parent_->set_sleep_profile(index + 1, false);
    auto option = this->at(index);
    if (option.has_value())
      this->publish_state(*option);
  }

  void set_parent(AirConditioner *parent) { this->parent_ = parent; }

 protected:
  void control(const std::string &value) override {
    auto index = this->index_of(value);
    if (!index.has_value())
      return;
    uint8_t i = *index;
    this->pref_.save(&i);
    this->parent_->set_sleep_profile(i + 1, true);
    this->publish_state(value);
  }

  AirConditioner *parent_;
  ESPPreferenceObject pref_;
};

}  // namespace ac
}  // namespace hisense
}  // namespace esphome
