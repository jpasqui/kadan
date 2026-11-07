#pragma once
// The module's own precompiled header. Unreal's build tool compiles with /fp:fast, and the readers
// and builders here have to keep IEEE values exactly, so exact floating-point math is switched on
// before the first engine header: the engine's inline functions are then compiled exact in this
// module as well. The engine's shared precompiled headers cannot be used for that reason.
#if defined(_MSC_VER)
#pragma float_control(precise, on)
#pragma fp_contract(off)
#endif
#include "UnrealEdSharedPCH.h"
// Development test seams (P4KadanTestSeams.h). Only a development test stage defines them.
#ifndef P4_KADAN_TEST_SEAMS
#define P4_KADAN_TEST_SEAMS 0
#endif
