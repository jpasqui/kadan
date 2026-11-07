#if P4_MAPKIT_TESTS
#include "P4PersistentTextFlags.h"
#include "P4LocalizedTextImport.h"
#include "P4DecodedPropertyWriter.h"
#include "P4ComparisonNames.h"
#include "P4TextValues.h"
#include "Components/TextRenderComponent.h"
#include "Misc/AutomationTest.h"
#include "UObject/TextProperty.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4PersistentTextFlagsTest, "P4MapKit.Levels.PersistentConvertedText",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4PersistentTextFlagsTest::RunTest(const FString&)
{
    using V = P4MapKit::FDecodedValue;
    const auto Envelope = [](uint32 Flags)
    {
        V Value, Text;
        Text.Fields.emplace_back("flags", V::Atom(V::EKind::Number, std::to_string(Flags)));
        Text.Fields.emplace_back("history", V::Atom(V::EKind::Number, "0"));
        Text.Fields.emplace_back("namespace", V::Atom(V::EKind::String, "Native.TextFixture"));
        Text.Fields.emplace_back("key", V::Atom(V::EKind::String, "OriginalKey"));
        Text.Fields.emplace_back("source", V::Atom(V::EKind::String, "Original localized words"));
        Value.Fields.emplace_back("__native_text", Text);
        return Value;
    };
    auto* Component = NewObject<UTextRenderComponent>(GetTransientPackage());
    const auto* Property = FindFProperty<FTextProperty>(Component->GetClass(), TEXT("Text"));
    if (!TestNotNull(TEXT("The fixture uses the real text-render property"), Property))
        return false;
    for (uint32 Flags :
         {0u, uint32(ETextFlag::ConvertedProperty), uint32(ETextFlag::InitializedFromString),
          uint32(ETextFlag::ConvertedProperty | ETextFlag::InitializedFromString), uint32(ETextFlag::Immutable),
          uint32(ETextFlag::Immutable | ETextFlag::ConvertedProperty | ETextFlag::InitializedFromString)})
    {
        const uint32 ExpectedFlags = Flags & uint32(ETextFlag::Immutable);
        const auto Original = Envelope(ExpectedFlags);
        auto Source = Envelope(Flags);
        const auto Retained = P4MapKit::DecodedJson(Source);
        FP4DecodedPropertyWriter Writer(nullptr, true);
        if (!TestTrue(TEXT("Persistent text preserves immutable identity and accepts discarded load bookkeeping"),
                      Writer.Value(Property, Property->ContainerPtrToValuePtr<void>(Component), Source,
                                   TEXT("native text fixture"))))
            return false;
        const FText Actual = Property->GetPropertyValue_InContainer(Component);
        TestEqual(TEXT("Localized source survives"), Actual.ToString(), FString(TEXT("Original localized words")));
        TestEqual(TEXT("Localization namespace survives"), FTextInspector::GetNamespace(Actual).GetValue(),
                  FString(TEXT("Native.TextFixture")));
        TestEqual(TEXT("Localization key survives"), FTextInspector::GetKey(Actual).GetValue(),
                  FString(TEXT("OriginalKey")));
        TArray<uint8> Bytes;
        FMemoryWriter Archive(Bytes, true);
        FText Copy = Actual;
        Archive << Copy;
        uint32 SavedFlags = MAX_uint32;
        FMemoryReader Reader(Bytes, true);
        Reader << SavedFlags;
        TestEqual(TEXT("The actual engine archive preserves immutable flags and discards converted bookkeeping"),
                  SavedFlags, ExpectedFlags);
        TestTrue(TEXT("The caller's source envelope was not changed by import"),
                 Retained == P4MapKit::DecodedJson(Source));
        P4ComparisonNames(Source, Property);
        TestTrue(TEXT("Source and persistent editor text compare the same"),
                 P4MapKit::DecodedJson(Source) == P4MapKit::DecodedJson(Original));
    }
    FText NoKey = FText::AsCultureInvariant(TEXT("Retained text"));
    TestTrue(TEXT("Unkeyed immutable source text persists as a literal without fabricating a localization key"),
             P4MakePersistentLocalizedText(ETextFlag::Immutable, FString(), FString(), TEXT("Text"), NoKey));
    TestEqual(TEXT("The literal preserves its source wording"), NoKey.ToString(), FString(TEXT("Text")));
    TestEqual(TEXT("The literal preserves immutable semantics"), FTextInspector::GetFlags(NoKey),
              uint32(ETextFlag::Immutable | ETextFlag::CultureInvariant));
    TestFalse(TEXT("No localization key was fabricated"), FTextInspector::GetKey(NoKey).IsSet());
    auto Unkeyed = Envelope(ETextFlag::Immutable);
    for (auto& Field : Unkeyed.Fields[0].second.Fields)
    {
        if (Field.first == "namespace" || Field.first == "key")
            Field.second.Text.clear();
        if (Field.first == "source")
            Field.second.Text = "Text";
    }
    FP4DecodedPropertyWriter LiteralWriter(nullptr, true);
    if (!TestTrue(TEXT("The real reflected writer accepts the original unkeyed immutable envelope"),
                  LiteralWriter.Value(Property, Property->ContainerPtrToValuePtr<void>(Component), Unkeyed,
                                      TEXT("legacy literal fixture"))))
        return false;
    FString NativeLiteral;
    TestTrue(TEXT("The actual stored literal serializes independently"),
             P4TextRuntimeJson(Property->GetPropertyValue_InContainer(Component), NativeLiteral));
    P4ComparisonNames(Unkeyed, Property);
    const FString ExpectedLiteral = UTF8_TO_TCHAR(P4MapKit::DecodedJson(Unkeyed).c_str());
    TestEqual(TEXT("Only the unkeyed base history compares with the equivalent native literal"), NativeLiteral,
              ExpectedLiteral);
    Property->SetPropertyValue_InContainer(Component, FText::AsCultureInvariant(TEXT("Changed literal")));
    P4TextRuntimeJson(Property->GetPropertyValue_InContainer(Component), NativeLiteral);
    TestNotEqual(TEXT("An actual authored text edit remains a difference"), NativeLiteral, ExpectedLiteral);
    Property->SetPropertyValue_InContainer(Component, NoKey);
    auto EmptyNamespace = Envelope(ETextFlag::Immutable);
    for (auto& Field : EmptyNamespace.Fields[0].second.Fields)
        if (Field.first == "namespace")
            Field.second.Text.clear();
    if (!TestTrue(TEXT("The actual empty-namespace keyed immutable source shape imports"),
                  LiteralWriter.Value(Property, Property->ContainerPtrToValuePtr<void>(Component), EmptyNamespace,
                                      TEXT("empty namespace fixture"))))
        return false;
    P4TextRuntimeJson(Property->GetPropertyValue_InContainer(Component), NativeLiteral);
    TestEqual(TEXT("Empty namespace, existing key, immutable flag and wording survive native persistence"),
              NativeLiteral, FString(UTF8_TO_TCHAR(P4MapKit::DecodedJson(EmptyNamespace).c_str())));
    Property->SetPropertyValue_InContainer(Component, NoKey);
    auto Unsupported = Envelope(ETextFlag::Transient);
    FP4DecodedPropertyWriter Writer(nullptr, true);
    TestFalse(TEXT("Other flags retain the original refusal"),
              Writer.Value(Property, Property->ContainerPtrToValuePtr<void>(Component), Unsupported,
                           TEXT("unsupported native text fixture")));
    TestEqual(TEXT("A refusal preserves the existing source text"),
              Property->GetPropertyValue_InContainer(Component).ToString(), FString(TEXT("Text")));
    Component->MarkPendingKill();
    return !HasAnyErrors();
}
#endif
