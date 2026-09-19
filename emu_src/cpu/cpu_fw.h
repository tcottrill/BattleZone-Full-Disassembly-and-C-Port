// cpu_fw.h - bz_emu: the header the vendored cpu_6502 core includes in its
// standalone mode (USING_AAE_EMU not defined) for its memory-handler types.
//
// The copy in C:\Source2026\6502_Klaus_tests_c6502\ defines MemoryReadByte /
// MemoryWriteByte / z80PortRead / z80PortWrite itself AND includes deftypes.h,
// which (same tree) defines the same four structs: together they do not
// compile (C2011 'struct' type redefinition - the Klaus tree's older core only
// ever included deftypes.h, so the clash never showed there).  deftypes.h's
// definitions are the ones AAE's own build uses, with the same members
// (lowAddr, highAddr, memoryCall, pUserArea), so this file just forwards to it.
// Not part of the core: cpu_6502.cpp / cpu_6502.h are untouched by this.
#pragma once
#include "deftypes.h"
