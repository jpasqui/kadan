#pragma once
// Internal to the NV assembly translator: the parsed program model shared by
// the parser (nvasm_parse.cpp), the lowering to IR and the backends.
#include "nvasm.h"

#include <algorithm>
#include <array>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace nvasm::detail {

// ---------------------------------------------------------------- program model

// A resolved program binding such as fragment.attrib[3] or result.color[1].
struct Binding {
    std::string path;  // "fragment.attrib", "vertex[].position", "result.depth", ...
    int index = -1;    // [N] after the member, -1 when absent
    int vertex = -1;   // geometry inputs: vertex[N]
};

struct Operand {
    enum Kind { Var, Const, Bind, Handle, Offset, Target, CCTest } kind = Var;
    std::string name;        // Var: declared name; Target/CCTest: keyword
    Binding bind;            // Bind
    bool indexed = false;    // Var arrays: name[...]
    int index = 0;           // constant part of the index
    std::string index_reg;   // relative index register ("" when absolute)
    int temp = -1;           // Var naming a TEMP: its Program::temp_list index
    int index_temp = -1;     // index_reg's temp_list index
    int index_comp = 0;
    std::vector<std::string> consts; // Const: literal texts (signs folded in)
    std::string suffix;      // swizzle / write mask text after '.'
    bool neg = false, abs = false;
    std::string pred;        // destination condition-code mask test, e.g. "NE"
    std::string pred_suffix;
    std::shared_ptr<Operand> inner; // Handle: the register holding it
    int handle_comp = -1;    // TXG component selection on the handle
};

struct Instr {
    int line = 0;
    std::string op;
    std::vector<std::string> mods;
    std::vector<Operand> ops;
    bool has(const char* m) const { return std::find(mods.begin(), mods.end(), m) != mods.end(); }
};

struct Node {
    enum Kind { Ins, If, Loop } kind = Ins;
    Instr ins;               // Ins; If/Loop keep the opening instruction here
    std::vector<Node> body, other;
};

enum class TempKind { Float, Short, Long };
struct Temp {
    TempKind kind;
    int array = 0; // > 0 for TEMP name[N]
    int id = 0;    // declaration order: index into Program::temp_list
};

struct AttribRange {
    std::string path;
    int first = 0, count = 0;
    std::string interpolation;
};

struct Program {
    Stage stage{};
    std::set<std::string> options;
    std::map<std::string, int> cbuffers, storage; // name -> program.buffer / program.storage index
    std::string shared_name;
    uint32_t shared_bytes = 0;
    std::map<std::string, Temp> temps;
    std::vector<std::string> temp_list;          // names by Temp::id
    std::vector<Temp> temp_info;                 // declarations by Temp::id
    std::map<std::string, AttribRange> aliases;  // ATTRIB / OUTPUT names
    std::map<int, std::string> interpolation;    // fragment.attrib index -> qualifier
    std::array<uint32_t, 3> group{1, 1, 1};
    std::string prim_in, prim_out;
    int vertices_out = 0, invocations = 0;
    std::vector<Instr> code;
};

bool ident_start(char c);
bool ident_char(char c);
bool is_swizzle(std::string_view s);
int comp_index(char c);
bool is_cc_test(std::string_view s);
extern const std::set<std::string, std::less<>> kTargets;
Program parse(std::string_view src);
// Consumes the instructions: the tree owns them afterwards.
std::vector<Node> build_tree(std::vector<Instr>&& code);

} // namespace nvasm::detail
