#pragma once

#include <cstdio>
#include <cstdlib>
#include "esphome/components/climate/climate.h"
#include "esphome/core/component.h"

namespace esphome::aqara_ir {

// $AQARA_AC = "P,M,T,S,D" as logged by libha_ir ha_ir_produce_by_status:
//   P power (1 = off), M mode (0 cool, 1 heat, 2 auto, 3 fan, 4 dry), T target C,
//   S fan (0 auto, 1 low, 2 medium, 3 high), D swing (0 = swing).
class AqaraIR : public Component {
 public:
  explicit AqaraIR(climate::Climate *climate) : climate_(climate) {}
  float get_setup_priority() const override { return setup_priority::LATE; }

  void setup() override {
    static constexpr climate::ClimateMode MODES[] = {climate::CLIMATE_MODE_COOL, climate::CLIMATE_MODE_HEAT,
                                                     climate::CLIMATE_MODE_HEAT_COOL, climate::CLIMATE_MODE_FAN_ONLY,
                                                     climate::CLIMATE_MODE_DRY};
    static constexpr climate::ClimateFanMode FANS[] = {climate::CLIMATE_FAN_AUTO, climate::CLIMATE_FAN_LOW,
                                                       climate::CLIMATE_FAN_MEDIUM, climate::CLIMATE_FAN_HIGH};
    const char *s = getenv("AQARA_AC");
    unsigned p, m, t, f, d;
    if (s == nullptr || sscanf(s, "%u,%u,%u,%u,%u", &p, &m, &t, &f, &d) != 5 || m > 4 || f > 3) {
      fprintf(stderr, "AQARA_AC must be P,M,T,S,D\n");
      exit(2);
    }
    auto call = this->climate_->make_call();
    call.set_mode(p == 1 ? climate::CLIMATE_MODE_OFF : MODES[m]);
    call.set_target_temperature(t);
    call.set_fan_mode(FANS[f]);
    call.set_swing_mode(d == 0 ? climate::CLIMATE_SWING_VERTICAL : climate::CLIMATE_SWING_OFF);
    call.perform();
    exit(0);
  }

 protected:
  climate::Climate *climate_;
};

}  // namespace esphome::aqara_ir
