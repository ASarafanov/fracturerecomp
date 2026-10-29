// fracture - hand-written replacements for guest functions of default.xex
// version 1 (media ID 20E2A1F5). Every address here is that release's.

#include <rex/hook.h>
#include <rex/ppc/context.h>

#include "swap_interval.h"

REX_EXTERN(sub_82DFDD8C);
REX_EXTERN(__imp__sub_822A4350);

// 0x82DFDD68, a QueryInterface-style cast reached through a vtable, shares
// its tail at 0x82DFDD80 with sub_82DFDD8C (which jumps there), and guest
// functions cannot overlap. Codegen only gets the part before the tail, so
// this is the whole function:
//   lis r11,0x835C ; addi r10,r11,0x31E0 ; cmplw cr6,r4,r10
//   bne cr6,0x82DFDD8C            ; not this interface: try the next one
//   addic. r11,r3,-4 ; addi r3,r3,72
//   bnelr                         ; 0x82DFDD80, shared tail
//   li r3,0 ; blr
REX_HOOK_RAW(sub_82DFDD68) {
  ctx.r11.u64 = 0x835C0000;
  ctx.r10.u64 = 0x835C31E0;
  if (ctx.r4.u32 != ctx.r10.u32) {
    sub_82DFDD8C(ctx, base);
    return;
  }
  ctx.r11.u64 = ctx.r3.u64 - 4;
  ctx.r3.u64 = ctx.r11.u32 != 0 ? ctx.r3.u64 + 72 : 0;
}

// D3D's swap step that queues the frame for its vblank (r3 = device).
REX_HOOK_RAW(sub_822A4350) {
  fracture::game::SwapWithInterval(ctx, base, __imp__sub_822A4350);
}
