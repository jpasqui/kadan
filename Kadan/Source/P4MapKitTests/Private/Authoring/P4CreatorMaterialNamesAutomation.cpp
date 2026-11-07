#if P4_MAPKIT_TESTS
#include "Misc/AutomationTest.h"
#include "P4MapKitCore/custom_material_graph.h"

// A texture parameter keeps its UE name, which may hold spaces, hyphens or dots. Its placeholders in a creator
// material's program must resolve like any other: a tester's texture parameter with such a name left
// "{TEX:...}" in the program and nvasm stopped with "unexpected character ':'" (2026-10-18).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4CreatorMaterialTextureNamesTest, "P4MapKit.Kadan.CreatorMaterialTextureNames",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4CreatorMaterialTextureNamesTest::RunTest(const FString&)
{
    using P4MapKit::CustomMaterial::Resolve;
    const std::string program =
        "LDC.U64 D1.x, buf14[{SAMP:Normal Map}];\nLDC.U64 D0.x, buf14[{TEX:Normal Map}];\n"
        "LDC.U64 D0.x, buf14[{TEX:crystal-base.v2}];\nMOV.F R0, fragment.attrib[{VAR:in_texcoord0}];\n"
        "ATTRIB fragment_attrib[] = { fragment.attrib[0..{LAST:}] };\n";
    const std::string resolved = Resolve(program, {{"Normal Map", {2, 1}}, {"crystal-base.v2", {3, 0}}},
        {{"in_texcoord0", 2}, {"in_texcoord11", 1}});
    TestEqual(TEXT("Names with spaces, hyphens and dots resolve, and interpolants still do"), FString(UTF8_TO_TCHAR(resolved.c_str())),
        FString(TEXT("LDC.U64 D1.x, buf14[1360];\nLDC.U64 D0.x, buf14[344];\nLDC.U64 D0.x, buf14[352];\n"
                     "MOV.F R0, fragment.attrib[2];\nATTRIB fragment_attrib[] = { fragment.attrib[0..2] };\n")));
    bool refused = false;
    try { (void)Resolve("LDC.U64 D0.x, buf14[{TEX:Missing One}];", {}, {}); }
    catch (const std::exception&) { refused = true; }
    TestTrue(TEXT("A spaced name the shader does not bind is still refused by name"), refused);
    return !HasAnyErrors();
}
#endif
