// own_switch_program.h: a program's reads of a block the game lays out, at the member the game's layout lists and at one
// it does not, and of a block the game does not lay out. Built by tools/check.py against the staged compiler (its
// staged tests). The SPIR-V is written here, a pixel program adding three reads of View and one of a block of its own.
#include "own_switch_program.h"
#include <iostream>
#include <string>
#include <vector>

namespace Own = P4MapKit::CustomMaterial::OwnSwitch;

static int failures = 0;
static void Expect(bool ok, const std::string& what) {
    if (!ok) { std::cerr << "FAIL " << what << "\n"; ++failures; }
}

// One instruction: its opcode and operands.
static void Put(std::vector<std::uint32_t>& out, std::uint16_t op, const std::vector<std::uint32_t>& operands) {
    out.push_back(std::uint32_t(operands.size() + 1) << 16 | op);
    out.insert(out.end(), operands.begin(), operands.end());
}
// A string operand: its bytes and a terminating zero, in words.
static std::vector<std::uint32_t> Words(const std::string& text) {
    std::vector<std::uint32_t> out((text.size() + 4) / 4, 0);
    for (std::size_t i = 0; i < text.size(); ++i) out[i / 4] |= std::uint32_t(std::uint8_t(text[i])) << (8 * (i % 4));
    return out;
}
static std::vector<std::uint32_t> Join(std::vector<std::uint32_t> head, const std::vector<std::uint32_t>& tail) {
    head.insert(head.end(), tail.begin(), tail.end());
    return head;
}

// View { float4 Listed at 0; float4 Unlisted at 16 } in binding 0 and Own { float4 Value at 0 } in binding 1; the
// program reads View.Listed once, View.Unlisted twice and Own.Value once, and writes their sum to SV_Target0.
static std::vector<std::uint32_t> Program(const std::string& listed, const std::string& unlisted) {
    namespace S = spirv_front::detail;
    enum : std::uint32_t {
        Void = 1, Fn, Float, V4, Int, C0, C1, ViewT, OwnT, PView, POwn, PV4, PO4, View, OwnVar, Out, Main, Label,
        P0, A, P1, B, P2, B2, P3, M, S1, S2, S3, Bound
    };
    std::vector<std::uint32_t> w = {0x07230203u, 0x00010000u, 0u, Bound, 0u};
    Put(w, S::OpCapability, {1});
    Put(w, S::OpMemoryModel, {0, 1});
    Put(w, S::OpEntryPoint, Join(Join({4, Main}, Words("main")), {Out}));
    Put(w, S::OpExecutionMode, {Main, 7});
    Put(w, S::OpName, Join({View}, Words("View")));
    Put(w, S::OpName, Join({OwnVar}, Words("KadanTestOwnBlock")));
    Put(w, S::OpName, Join({Out}, Words("out.var.SV_Target0")));
    Put(w, S::OpMemberName, Join({ViewT, 0}, Words(listed)));
    Put(w, S::OpMemberName, Join({ViewT, 1}, Words(unlisted)));
    Put(w, S::OpMemberName, Join({OwnT, 0}, Words("KadanTestOwnBlock_Value")));
    Put(w, S::OpDecorate, {ViewT, S::DecBlock});
    Put(w, S::OpMemberDecorate, {ViewT, 0, S::DecOffset, 0});
    Put(w, S::OpMemberDecorate, {ViewT, 1, S::DecOffset, 16});
    Put(w, S::OpDecorate, {OwnT, S::DecBlock});
    Put(w, S::OpMemberDecorate, {OwnT, 0, S::DecOffset, 0});
    Put(w, S::OpDecorate, {View, S::DecDescriptorSet, 0});
    Put(w, S::OpDecorate, {View, S::DecBinding, 0});
    Put(w, S::OpDecorate, {OwnVar, S::DecDescriptorSet, 0});
    Put(w, S::OpDecorate, {OwnVar, S::DecBinding, 1});
    Put(w, S::OpDecorate, {Out, S::DecLocation, 0});
    Put(w, S::OpTypeVoid, {Void});
    Put(w, S::OpTypeFunction, {Fn, Void});
    Put(w, S::OpTypeFloat, {Float, 32});
    Put(w, S::OpTypeVector, {V4, Float, 4});
    Put(w, S::OpTypeInt, {Int, 32, 1});
    Put(w, S::OpConstant, {Int, C0, 0});
    Put(w, S::OpConstant, {Int, C1, 1});
    Put(w, S::OpTypeStruct, {ViewT, V4, V4});
    Put(w, S::OpTypeStruct, {OwnT, V4});
    Put(w, S::OpTypePointer, {PView, 2, ViewT});
    Put(w, S::OpTypePointer, {POwn, 2, OwnT});
    Put(w, S::OpTypePointer, {PV4, 2, V4});
    Put(w, S::OpTypePointer, {PO4, 3, V4});
    Put(w, S::OpVariable, {PView, View, 2});
    Put(w, S::OpVariable, {POwn, OwnVar, 2});
    Put(w, S::OpVariable, {PO4, Out, 3});
    Put(w, S::OpFunction, {Void, Main, 0, Fn});
    Put(w, S::OpLabel, {Label});
    Put(w, S::OpAccessChain, {PV4, P0, View, C0});
    Put(w, S::OpLoad, {V4, A, P0});
    Put(w, S::OpAccessChain, {PV4, P1, View, C1});
    Put(w, S::OpLoad, {V4, B, P1});
    Put(w, S::OpAccessChain, {PV4, P2, View, C1});
    Put(w, S::OpLoad, {V4, B2, P2});
    Put(w, S::OpAccessChain, {PV4, P3, OwnVar, C0});
    Put(w, S::OpLoad, {V4, M, P3});
    Put(w, S::OpFAdd, {V4, S1, A, B});
    Put(w, S::OpFAdd, {V4, S2, S1, B2});
    Put(w, S::OpFAdd, {V4, S3, S2, M});
    Put(w, S::OpStore, {Out, S3});
    Put(w, S::OpReturn, {});
    Put(w, S::OpFunctionEnd, {});
    return w;
}

int main() {
    // A View member the game's layout lists, away from both stock offsets the program declares.
    std::string listed;
    std::uint32_t gameOffset = 0;
    for (const auto& entry : Own::GameLayoutOffsets())
        if (entry.first.first == "View" && entry.second >= 32 && entry.second % 16 == 0) {
            listed = entry.first.second;
            gameOffset = entry.second;
            break;
        }
    Expect(!listed.empty(), "the game's layout lists a member of View");
    const std::string unlisted = "View_KadanNotAGameMember";
    Expect(!Own::GameLayoutOffsets().count({"View", unlisted}), "the test's own View member is not the game's");

    Expect(Own::GameLaysOut("View"), "View is a block the game lays out");
    Expect(!Own::GameLaysOut("KadanTestOwnBlock"), "a block of the program's own is not one the game lays out");
    Expect(Own::LayoutMissName({"View", "View_PreExposure"}) == "View.PreExposure", "a miss is named as code names it");
    Expect(Own::LayoutMissName({"View", "Other"}) == "View.Other", "a member without the block's prefix keeps its name");

    // The remap the front end asks: the game's offset for a listed member, none (stock's) otherwise.
    {
        std::set<Own::LayoutMiss> misses;
        const auto options = Own::GameOptions(&misses);
        const auto known = options.remap("View", listed, 0);
        Expect(known && *known == gameOffset, "a listed member is read at the game's offset");
        Expect(misses.empty(), "a listed member is no miss");
        Expect(!options.remap("View", unlisted, 16), "an unlisted member of a known block keeps stock's offset");
        Expect(!options.remap("View", unlisted, 16), "the same member read again keeps stock's offset");
        Expect(misses.size() == 1 && misses.count({"View", unlisted}), "an unlisted member is recorded once");
        Expect(!options.remap("KadanTestOwnBlock", "KadanTestOwnBlock_Value", 0), "an unknown block keeps stock's layout");
        Expect(misses.size() == 1, "a member of an unknown block is no miss");
        Expect(!Own::GameOptions().remap("View", unlisted, 16), "without a record the remap is the same");
    }

    // The whole translation: what the program reads and where.
    try {
        const auto p = Own::Translate(Program(listed, unlisted));
        Expect(p.layout_misses.size() == 1 && p.layout_misses[0] == Own::LayoutMiss("View", unlisted),
               "the program records its one unlisted View member, once");
        const auto& text = p.assembly;
        Expect(text.find("buf0[" + std::to_string(gameOffset) + "]") != std::string::npos,
               "the listed member is read at the game's offset");
        Expect(text.find("buf0[16]") != std::string::npos, "the unlisted member is read at stock's offset");
        Expect(text.find("buf0[0]") == std::string::npos, "nothing is read at the listed member's stock offset");
        Expect(text.find("buf1[0]") != std::string::npos, "the program's own block keeps its layout");
        if (failures) std::cerr << text << "\n";
    } catch (const std::exception& e) {
        Expect(false, std::string("the program translates: ") + e.what());
    }

    if (failures) return 1;
    std::cout << "own switch layout: ok\n";
    return 0;
}
