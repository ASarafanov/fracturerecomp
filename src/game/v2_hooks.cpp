// fracture - hand-written replacements for guest functions of default.xex
// version 2 (media ID 43445F92). Every address here is that release's; the
// functions are the ones of src/game/v1_hooks.cpp.

#include <rex/hook.h>
#include <rex/ppc/context.h>

#include "swap_interval.h"

REX_EXTERN(sub_82DFEA44);
REX_EXTERN(__imp__sub_822A39C0);

// 0x82DFEA20, a QueryInterface-style cast reached through a vtable, shares
// its tail at 0x82DFEA38 with sub_82DFEA44 (which jumps there), and guest
// functions cannot overlap. Codegen only gets the part before the tail, so
// this is the whole function:
//   lis r11,0x835C ; addi r10,r11,0x33E0 ; cmplw cr6,r4,r10
//   bne cr6,0x82DFEA44            ; not this interface: try the next one
//   addic. r11,r3,-4 ; addi r3,r3,72
//   bnelr                         ; 0x82DFEA38, shared tail
//   li r3,0 ; blr
REX_HOOK_RAW(sub_82DFEA20) {
  ctx.r11.u64 = 0x835C0000;
  ctx.r10.u64 = 0x835C33E0;
  if (ctx.r4.u32 != ctx.r10.u32) {
    sub_82DFEA44(ctx, base);
    return;
  }
  ctx.r11.u64 = ctx.r3.u64 - 4;
  ctx.r3.u64 = ctx.r11.u32 != 0 ? ctx.r3.u64 + 72 : 0;
}

// D3D's swap step that queues the frame for its vblank (r3 = device).
REX_HOOK_RAW(sub_822A39C0) {
  fracture::game::SwapWithInterval(ctx, base, __imp__sub_822A39C0);
}
