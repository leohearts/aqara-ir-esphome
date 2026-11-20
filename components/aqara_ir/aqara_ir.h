#pragma once

#include <cstdio>
#include <cstdlib>
#include "esphome/components/climate/climate.h"
#include "esphome/components/remote_receiver/remote_receiver.h"
#include "esphome/core/component.h"

namespace esphome::aqara_ir {

// One-shot bridge between libirplug.so and a climate_ir entity. AC state uses libha_ir's ac_status
// numbers, "P,M,T,S,D":
//   P power (0 on, 1 off), M mode (0 cool, 1 heat, 2 auto, 3 fan, 4 dry), T target C,
//   S fan (0 auto, 1 low, 2 medium, 3 high), D swing (0 = swing, 1 = fixed).
// Transmit: $AQARA_AC="P,M,T,S,D" -> the climate transmits (timings printed by remote_transmitter).
// Receive:  $AQARA_RX="<timings>" -> the climate decodes them; prints "P,M,T,S,D", exit 1 if not decoded.
class AqaraIR : public Component {
 public:
  explicit AqaraIR(climate::Climate *climate) : climate_(climate) {}
  void set_receiver(remote_receiver::RemoteReceiverComponent *receiver) { this->receiver_ = receiver; }
  float get_setup_priority() const override { return setup_priority::LATE; }

  void setup() override {
    if (const char *rx = getenv("AQARA_RX"))
      this->receive_(rx);
    this->transmit_(getenv("AQARA_AC"));
  }

 protected:
  static constexpr climate::ClimateMode MODES[] = {climate::CLIMATE_MODE_COOL, climate::CLIMATE_MODE_HEAT,
                                                   climate::CLIMATE_MODE_HEAT_COOL, climate::CLIMATE_MODE_FAN_ONLY,
                                                   climate::CLIMATE_MODE_DRY};
  static constexpr climate::ClimateFanMode FANS[] = {climate::CLIMATE_FAN_AUTO, climate::CLIMATE_FAN_LOW,
                                                     climate::CLIMATE_FAN_MEDIUM, climate::CLIMATE_FAN_HIGH};

  void transmit_(const char *s) {
    unsigned p, m, t, f, d;
    if (s == nullptr || sscanf(s, "%u,%u,%u,%u,%u", &p, &m, &t, &f, &d) != 5 || m > 4 || f > 3) {
      fprintf(stderr, "set AQARA_AC=P,M,T,S,D or AQARA_RX=<timings>\n");
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

  void receive_(const char *timings) {
    if (this->receiver_ == nullptr) {
      fprintf(stderr, "aqara_ir has no receiver_id\n");
      exit(2);
    }
    // climate_ir publishes state only when its on_receive decoded a frame
    bool decoded = false;
    this->climate_->add_on_state_callback([&decoded](climate::Climate &) { decoded = true; });
    this->receiver_->inject(timings);
    if (!decoded)
      exit(1);
    auto &c = *this->climate_;
    int m = 5;  // libha_ir's "unknown"
    for (int i = 0; i < 5; i++)
      if (c.mode == MODES[i])
        m = i;
    int f = 0;
    for (int i = 0; i < 4; i++)
      if (c.fan_mode.value_or(climate::CLIMATE_FAN_AUTO) == FANS[i])
        f = i;
    printf("%d,%d,%d,%d,%d\n", c.mode == climate::CLIMATE_MODE_OFF ? 1 : 0, m, (int) (c.target_temperature + 0.5f), f,
           c.swing_mode == climate::CLIMATE_SWING_OFF ? 1 : 0);
    exit(0);
  }

  climate::Climate *climate_;
  remote_receiver::RemoteReceiverComponent *receiver_{nullptr};
};

}  // namespace esphome::aqara_ir
