// fracture - ReXGlue Recompiled Project

#include "generated/default/fracture_init.h"

#include "fracture_app.h"

REXCVAR_DEFINE_STRING(fracture_dump_image, "", "Fracture",
                      "Write the loaded guest image (0x82000000-0x83710000) to "
                      "this file; empty disables");
REXCVAR_DEFINE_STRING(fracture_dump_cvars, "", "Fracture",
                      "Write every cvar (type, lifecycle, default, current "
                      "value, constraints) to this file; empty disables");

REX_DEFINE_APP(fracture, FractureApp::Create)
