#pragma once
#include "nvasm_ir.h"
#include "nvasm_program.h"

namespace nvasm::lower {
// Lowers a parsed program to IR and fills the reflection parts of `result`.
ir::Function lower(detail::Program& program, const Options& options, Result& result);
}
