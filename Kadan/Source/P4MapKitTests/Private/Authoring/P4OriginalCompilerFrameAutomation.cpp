#if P4_MAPKIT_TESTS
#include "P4MapKitCore/blueprint_compiler_frame.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformMisc.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4OriginalCompilerFrame, "P4MapKit.Kadan.OriginalCompilerFrame",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4OriginalCompilerFrame::RunTest(const FString&)
{
    const FString Header = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_COMPILER_FRAME_HEADER"));
    if (Header.IsEmpty())
    {
        AddWarning(
            TEXT("P4MapKit test not run. Set P4_NATIVE_COMPILER_FRAME_HEADER to the actual retained GAvatar source."));
        return true;
    }
    TArray<uint8> H, B;
    if (!TestTrue(TEXT("Actual source pair is readable"),
            FFileHelper::LoadFileToArray(H, *Header) &&
                FFileHelper::LoadFileToArray(B, *FPaths::ChangeExtension(Header, TEXT("uexp")))))
        return false;
    std::vector<std::uint8_t> Head(H.GetData(), H.GetData() + H.Num()), Body(B.GetData(), B.GetData() + B.Num());
    using namespace P4MapKit;
    using namespace P4MapKit::Blueprint;
    try
    {
        const auto P = ReadPackage(Head, Body);
        const auto Doc = ReadClassStructure(P, Body);
        std::int32_t DefaultIndex = 0;
        for (std::size_t I = 0; I < P.Exports.size(); ++I)
        {
            std::string Path;
            if (P.Reference(std::int32_t(I + 1), Path) && Path == Doc["class"]["class_default_object"].S())
                DefaultIndex = std::int32_t(I + 1);
        }
        if (!TestTrue(TEXT("Actual original source has a fully qualified empty compiler frame"),
                IsProvenEmptyCompilerFrame(Doc, P, Body, DefaultIndex)))
            return false;
        std::size_t Slot = 0;
        while (Slot < Doc["class"]["properties"].Items.size() &&
            Doc["class"]["properties"].Items[Slot]["name"].S() != "UberGraphFrame")
            ++Slot;
        const auto Mutate = [&](const char* Key, const J& Value)
        {
            auto Changed = Doc;
            Changed["class"]["properties"].Items[Slot][Key] = Value;
            TestFalse(FString(TEXT("Changed compiler frame schema refuses: ")) + UTF8_TO_TCHAR(Key),
                IsProvenEmptyCompilerFrame(Changed, P, Body, DefaultIndex));
        };
        Mutate("name", "AuthoredFrame");
        Mutate("type", "ObjectProperty");
        Mutate("array_dim", 2);
        Mutate("element_size", 8);
        Mutate("flags", "0x200000");
        Mutate("struct", "/Script/Engine.Vector");
        auto Changed = Doc;
        Changed["class"]["class_default_object"] = "OtherDefault";
        TestFalse(
            TEXT("Wrong class default linkage refuses"), IsProvenEmptyCompilerFrame(Changed, P, Body, DefaultIndex));
        const auto ClassName = Doc["class"]["name"].S();
        const auto Graph = "ExecuteUbergraph_" + ClassName.substr(0, ClassName.size() - 2);
        Changed = Doc;
        Changed["class"]["function_map"][Graph] = "OtherClass.OtherFunction";
        TestFalse(
            TEXT("Wrong native function map refuses"), IsProvenEmptyCompilerFrame(Changed, P, Body, DefaultIndex));
        Changed = Doc;
        Changed["functions"][Graph]["outer"] = "OtherClass";
        TestFalse(TEXT("Wrong function owner refuses"), IsProvenEmptyCompilerFrame(Changed, P, Body, DefaultIndex));
        auto BadPackage = P;
        BadPackage.Exports[std::size_t(DefaultIndex - 1)].Flags &= ~std::uint64_t(0x10);
        TestFalse(TEXT("An authored object is not a compiler CDO"),
            IsProvenEmptyCompilerFrame(Doc, BadPackage, Body, DefaultIndex));
        const auto& Default = P.Exports[std::size_t(DefaultIndex - 1)];
        const auto Tags = ReadPropertyEnvelopes(Body, P.Names, Default.Offset, Default.Offset + Default.Size);
        for (const auto& Tag : Tags.Properties)
            if (Tag.Name == "UberGraphFrame")
            {
                auto Bytes = Body;
                Bytes[Tag.PayloadOffset + 4] = 1;
                TestFalse(TEXT("A noncanonical empty-struct terminator refuses"),
                    IsProvenEmptyCompilerFrame(Doc, P, Bytes, DefaultIndex));
                Bytes = Body;
                Bytes[Tag.TagOffset + 20] = 1;
                TestFalse(
                    TEXT("A fixed-array frame tag refuses"), IsProvenEmptyCompilerFrame(Doc, P, Bytes, DefaultIndex));
            }
    }
    catch (const std::exception& E)
    {
        AddError(UTF8_TO_TCHAR(E.what()));
        return false;
    }
    return !HasAnyErrors();
}
#endif
