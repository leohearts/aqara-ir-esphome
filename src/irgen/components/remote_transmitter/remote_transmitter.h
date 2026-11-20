#pragma once

#include <cstdio>
#include "esphome/components/remote_base/remote_base.h"
#include "esphome/core/component.h"

namespace esphome::remote_transmitter {

// Prints the frame as alternating mark/space microseconds, one comma-separated line,
// starting with a mark and ending with a space (what mha_ir's IR UART expects).
// `send_times` (total copies, like ESPHome's `repeat: times:`) prints the whole frame that many
// times back to back: the AC misses 30-50% of the frames the P3 sends.
class RemoteTransmitterComponent : public remote_base::RemoteTransmitterBase, public Component {
 public:
  RemoteTransmitterComponent() : remote_base::RemoteTransmitterBase(nullptr) {}
  void set_send_times(uint32_t times) { this->times_ = times; }

 protected:
  static constexpr int32_t TAIL_SPACE = 20000;

  void send_internal(uint32_t send_times, uint32_t send_wait) override {
    std::vector<int32_t> out;  // positive = mark, negative = space; adjacent same-sign items merged
    for (uint32_t n = 0; n < send_times; n++) {
      for (int32_t v : this->temp_.get_data()) {
        if (v == 0 || (out.empty() && v < 0))
          continue;
        if (!out.empty() && (out.back() > 0) == (v > 0))
          out.back() += v;
        else
          out.push_back(v);
      }
      if (n + 1 < send_times && send_wait)
        out.push_back(-int32_t(send_wait * 1000));
    }
    if (!out.empty() && out.back() > 0)
      out.push_back(-TAIL_SPACE);
    for (uint32_t r = 0; r < this->times_; r++)
      for (size_t i = 0; i < out.size(); i++)
        printf(r || i ? ",%d" : "%d", out[i] < 0 ? -out[i] : out[i]);
    printf("\n");
    fflush(stdout);
  }

  uint32_t times_{1};
};

}  // namespace esphome::remote_transmitter
