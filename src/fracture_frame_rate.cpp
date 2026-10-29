// fracture - frame rate above the original 30 fps.

#include "fracture_frame_rate.h"

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/ppc/context.h>

#include <atomic>
#include <cstdint>
#include <string_view>

REXCVAR_DEFINE_STRING(fracture_frame_rate, "original", "Fracture",
                      "Frame rate: original (30 fps), 60, or unlimited. Above 30 is "
                      "experimental; the game was made for 30. unlimited (not in the "
                      "settings screen) lets the game loop run far faster than it presents")
    .allowed({"original", "60", "unlimited"});

REX_EXTERN(__imp__sub_822A4350);

namespace fracture::frame_rate {
namespace {

// D3D device field holding D3DPRESENT_PARAMETERS::PresentationInterval;
// sub_822A4350 maps it to a vblank count for the swap (ONE -> 1, TWO -> 2,
// THREE -> 3, IMMEDIATE -> 0) and queues the frame with it.
constexpr uint32_t kPresentIntervalOffset = 13596;
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

uint32_t LoadBE32(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}

void StoreBE32(uint8_t* p, uint32_t value) {
  p[0] = uint8_t(value >> 24);
  p[1] = uint8_t(value >> 16);
  p[2] = uint8_t(value >> 8);
  p[3] = uint8_t(value);
}

}  // namespace

void Init() {
  g_interval.store(IntervalFor(REXCVAR_GET(fracture_frame_rate)));
  rex::cvar::RegisterChangeCallback("fracture_frame_rate",
                                    [](std::string_view, std::string_view value) {
                                      g_interval.store(IntervalFor(value));
                                    });
}

}  // namespace fracture::frame_rate

// D3D's swap step that queues the frame for its vblank (r3 = device). The
// interval is replaced only for this call and restored right after, so the
// rest of the game keeps reading its own value.
REX_HOOK_RAW(sub_822A4350) {
  const uint32_t interval = fracture::frame_rate::g_interval.load(std::memory_order_relaxed);
  if (interval == 0) {
    __imp__sub_822A4350(ctx, base);
    return;
  }
  uint8_t* field = base + ctx.r3.u32 + fracture::frame_rate::kPresentIntervalOffset;
  const uint32_t game_interval = fracture::frame_rate::LoadBE32(field);
  fracture::frame_rate::StoreBE32(field, interval);
  __imp__sub_822A4350(ctx, base);
  fracture::frame_rate::StoreBE32(field, game_interval);
}
