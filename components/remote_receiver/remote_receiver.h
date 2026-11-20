#pragma once

#include <cstdlib>
#include "esphome/components/remote_base/remote_base.h"
#include "esphome/core/component.h"

namespace esphome::remote_receiver {

class RemoteReceiverComponent : public remote_base::RemoteReceiverBase, public Component {
 public:
  RemoteReceiverComponent() : remote_base::RemoteReceiverBase(nullptr) {}

  // `timings`: comma-separated microseconds, alternating mark/space and starting with a mark, as
  // the P3's IR module reports them. Hands them to every registered listener (climate_ir on_receive).
  void inject(const char *timings) {
    this->temp_.clear();
    bool mark = true;
    for (const char *p = timings; *p;) {
      char *end;
      long v = strtol(p, &end, 10);
      if (end == p) {
        p++;
        continue;
      }
      this->temp_.push_back(mark ? v : -v);
      mark = !mark;
      p = end;
    }
    this->call_listeners_dumpers_();
  }
};

}  // namespace esphome::remote_receiver
