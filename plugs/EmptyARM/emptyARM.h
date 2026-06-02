#pragma once
#include "plugins/plugin_header.h"
#include "plugins/plugin_types.h"

// Empty ARM7 stub - does nothing, prevents BIOS freeze
// when game tries to enable ARM7 CPU

void emptyArmGetInterface(plugin_interface* info);
