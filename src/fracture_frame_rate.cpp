// fracture - frame rate above the original 30 fps.

#include "fracture_frame_rate.h"

#include <rex/cvar.h>

#include <atomic>
#include <cstdint>
#include <string_view>

REXCVAR_DEFINE_STRING(fracture_frame_rate, "original", "Fracture",
                      "Frame rate: original (30 fps), 60, or unlimited. Above 30 is "
                      "experimental; the game was made for 30. unlimited (not in the "
                      "settings screen) lets the game loop run far faster than it presents")
    .allowed({"original", "60", "unlimited"});

namespace fracture::frame_rate {
namespace {

constexpr uint32_t kIntervalOne = 1;
constexpr uint32_t kIntervalImmediate = 0x80000000;

// 0 = leave the game's interval alone.
std::atomic<uint32_t> g_interval{0};

uint32_t IntervalFor(std::string_view mode) {
  if (mode == "60") {
    return kIntervalOne;
  }
  if (mode == "unlimited") {
    return kIntervalImmediate;
  }
  return 0;
}

}  // namespace

void Init() {
  g_interval.store(IntervalFor(REXCVAR_GET(fracture_frame_rate)));
  rex::cvar::RegisterChangeCallback("fracture_frame_rate",
                                    [](std::string_view, std::string_view value) {
                                      g_interval.store(IntervalFor(value));
                                    });
}

uint32_t Interval() { return g_interval.load(std::memory_order_relaxed); }

}  // namespace fracture::frame_rate
