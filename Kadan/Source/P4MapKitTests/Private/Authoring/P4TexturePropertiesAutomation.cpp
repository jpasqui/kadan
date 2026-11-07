#include "P4TextureProperties.h"
#if WITH_DEV_AUTOMATION_TESTS && P4_MAPKIT_TESTS
#include "Misc/AutomationTest.h"
#include "Dom/JsonObject.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/MemoryReader.h"
#include "UObject/StrongObjectPtr.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureCube.h"
#include "Engine/Texture2DArray.h"
#include "Engine/VolumeTexture.h"
#include "Engine/LightMapTexture2D.h"
#include "Engine/ShadowMapTexture2D.h"
#include "P4ImportReferences.h"
#include "UObject/UnrealType.h"

bool P4TestTextureProperties(FAutomationTestBase& Test)
{
    using namespace P4MapKit;
    bool Good = true;
    const auto Check = [&](const TCHAR* Message, bool Result)
    {
        Good = Test.TestTrue(Message, Result) && Good;
        return Result;
    };
    const FGuid OriginalGuid(UINT32_MAX, 0x80000000u, 0x12345678u, 0u);
    for (UClass* Class :
        {UTexture2D::StaticClass(), ULightMapTexture2D::StaticClass(), UShadowMapTexture2D::StaticClass(),
            UTextureCube::StaticClass(), UTexture2DArray::StaticClass(), UVolumeTexture::StaticClass()})
    {
        FPackageInventory Package;
        Package.Package = "/Game/P4TexturePropertyFixture";
        Package.Imports.push_back({"CoreUObject", "Package", "/Script/Engine", 0});
        Package.Imports.push_back({"CoreUObject", "Class", TCHAR_TO_UTF8(*Class->GetName()), -1});
        Package.Imports.push_back({"CoreUObject", "Package", "/Game/TextureDependency", 0});
        Package.Imports.push_back({"Engine", "Texture2D", "TextureDependency", -3});
        TArray<uint8> Archive;
        FMemoryWriter Writer(Archive, true);
        const auto Name = [&](const FString& Text)
        {
            const std::string UTF8 = TCHAR_TO_UTF8(*Text);
            const auto Found = std::find(Package.Names.begin(), Package.Names.end(), UTF8);
            int32 Index = int32(Found - Package.Names.begin());
            if (Found == Package.Names.end())
            {
                Index = int32(Package.Names.size());
                Package.Names.push_back(UTF8);
            }
            int32 Number = 0;
            Writer << Index << Number;
        };
        const auto Tag =
            [&](const TCHAR* Field, const TCHAR* Type, const TCHAR* Detail, TArray<uint8>& Bytes, bool Boolean = false)
        {
            Name(Field);
            Name(Type);
            int32 Size = Bytes.Num(), Slot = 0;
            Writer << Size << Slot;
            if (Detail)
                Name(Detail);
            if (FString(Type) == TEXT("StructProperty"))
            {
                FGuid StructGuid;
                Writer << StructGuid;
            }
            if (FString(Type) == TEXT("BoolProperty"))
            {
                uint8 Flag = Boolean ? 1 : 0;
                Writer << Flag;
            }
            uint8 HasGuid = 0;
            Writer << HasGuid;
            if (Bytes.Num())
                Writer.Serialize(Bytes.GetData(), Bytes.Num());
        };
        TArray<uint8> Bytes;
        {
            FMemoryWriter Payload(Bytes, true);
            FGuid Value = OriginalGuid;
            Payload << Value;
        }
        Tag(TEXT("LightingGuid"), TEXT("StructProperty"), TEXT("Guid"), Bytes);
        if (Class->IsChildOf(UTexture2D::StaticClass()))
        {
            Bytes.Reset();
            FMemoryWriter Payload(Bytes, true);
            int32 X = 1024, Y = 2048;
            Payload << X << Y;
            Tag(TEXT("ImportedSize"), TEXT("StructProperty"), TEXT("IntPoint"), Bytes);
        }
        Bytes.Reset();
        Tag(TEXT("SRGB"), TEXT("BoolProperty"), nullptr, Bytes, false);
        Tag(TEXT("NeverStream"), TEXT("BoolProperty"), nullptr, Bytes, true);
        const auto EnumBytes = [&](const TCHAR* Value)
        {
            const std::string UTF8 = TCHAR_TO_UTF8(Value);
            const auto Found = std::find(Package.Names.begin(), Package.Names.end(), UTF8);
            int32 Index = int32(Found - Package.Names.begin()), Number = 0;
            if (Found == Package.Names.end())
            {
                Index = int32(Package.Names.size());
                Package.Names.push_back(UTF8);
            }
            Bytes.Reset();
            FMemoryWriter Payload(Bytes, true);
            Payload << Index << Number;
        };
        EnumBytes(TEXT("TEXTUREGROUP_WorldNormalMap"));
        Tag(TEXT("LODGroup"), TEXT("ByteProperty"), TEXT("TextureGroup"), Bytes);
        EnumBytes(TEXT("TC_Normalmap"));
        Tag(TEXT("CompressionSettings"), TEXT("ByteProperty"), TEXT("TextureCompressionSettings"), Bytes);
        Bytes.Reset();
        {
            FMemoryWriter Payload(Bytes, true);
            int32 Reference = -4;
            Payload << Reference;
        }
        Tag(TEXT("CompositeTexture"), TEXT("ObjectProperty"), nullptr, Bytes);
        Name(TEXT("None"));
        Writer.Close();
        std::vector<std::uint8_t> Body(Archive.GetData(), Archive.GetData() + Archive.Num());
        FPackageExport Export;
        Export.Class = -2;
        Export.Name = "Fixture";
        Export.Size = Body.size();
        const auto Properties = ReadPropertyEnvelopes(Body, Package.Names, 0, Body.size());
        FP4TexturePropertyDocument Document;
        FString Error, Canonical;
        if (!Check(TEXT("All native texture classes inventory all source property tags"),
                P4InspectTextureProperties(Package, Export, Properties, Body, Class, Document, Error)))
            continue;
        Check(TEXT("Omitted original runtime defaults remain visible blockers"),
            Document.MissingRuntimeDefaults.Num() > 0);
        Check(TEXT("Omitted editor source build settings remain visible blockers"),
            Document.MissingEditorSourceSettings.Contains(TEXT("AdjustBrightness")));
        Check(TEXT("Original property references retain source path and class"),
            Document.Dependencies.Num() == 1 &&
                Document.Dependencies[0].Path == TEXT("/Game/TextureDependency.TextureDependency") &&
                Document.Dependencies[0].ExpectedClass == TEXT("/Script/Engine.Texture"));
        TStrongObjectPtr<UTexture> Scratch(NewObject<UTexture>(GetTransientPackage(), Class, NAME_None, RF_Transient));
        Check(TEXT("Unresolved original reference refuses private staging"),
            !P4PrepareTextureProperties(Scratch.Get(), Document, Canonical, Error));
        TStrongObjectPtr<UTexture2D> Dependency(NewObject<UTexture2D>(GetTransientPackage(), NAME_None, RF_Transient));
        TMap<FString, UObject*> References;
        References.Add(TEXT("/Game/TextureDependency.TextureDependency"), Dependency.Get());
        FP4ImportReferenceScope Scope(References);
        if (Check(TEXT("Original properties stage with exact independent readback"),
                P4PrepareTextureProperties(Scratch.Get(), Document, Canonical, Error)))
        {
            Check(TEXT("Native sRGB and streaming values retained"), !Scratch->SRGB && Scratch->NeverStream);
            Check(TEXT("Native texture enum settings retained"),
                Scratch->LODGroup == TEXTUREGROUP_WorldNormalMap && Scratch->CompressionSettings == TC_Normalmap);
            Check(TEXT("Native GUID retains every original bit"), Scratch->GetLightingGuid() == OriginalGuid);
            Check(TEXT("Native reference retains graph prototype identity"),
                Scratch->CompositeTexture == Dependency.Get());
            if (auto Plane = Cast<UTexture2D>(Scratch.Get()))
            {
                // In an editor build GetImportedSize reads Source.GetLogicalSize,
                // not the cooked-only private ImportedSize field. Read the
                // actual serialized field independently; source pixels are not
                // fabricated merely to satisfy the editor accessor.
                auto SizeProperty = CastField<FStructProperty>(
                    Plane->GetClass()->FindPropertyByName(TEXT("ImportedSize")));
                Check(TEXT("Cooked original dimension field retained independently"),
                    SizeProperty && *SizeProperty->ContainerPtrToValuePtr<FIntPoint>(Plane) == FIntPoint(1024, 2048));
                Check(TEXT("Missing editor image source is not invented"),
                    !Plane->Source.IsValid() && Plane->GetImportedSize() == Plane->Source.GetLogicalSize());
            }
        }
        auto Altered = Properties;
        Altered.Properties[0].PayloadOffset++;
        FP4TexturePropertyDocument Refused;
        Check(TEXT("Replaced inventory cannot override original source"),
            !P4InspectTextureProperties(Package, Export, Altered, Body, Class, Refused, Error) &&
                !Refused.TaggedPropertiesQualified);
        auto WrongClass = Export;
        WrongClass.Class = -4;
        Check(TEXT("Original class mismatch refuses"),
            !P4InspectTextureProperties(Package, WrongClass, Properties, Body, Class, Refused, Error));
        auto BadValues = Document;
        BadValues.ValuesJson = TEXT("{\"NoSuchTextureProperty\":1}");
        Check(TEXT("Unknown property refuses without inventing field"),
            !P4PrepareTextureProperties(Scratch.Get(), BadValues, Canonical, Error));
        BadValues.ValuesJson = TEXT("{\"SRGB\":7}");
        Check(TEXT("Mismatched original value type refuses"),
            !P4PrepareTextureProperties(Scratch.Get(), BadValues, Canonical, Error));
        auto BadDocument = Document;
        BadDocument.ClassPath = TEXT("/Script/Engine.DataAsset");
        Check(TEXT("Document class mismatch refuses"),
            !P4PrepareTextureProperties(Scratch.Get(), BadDocument, Canonical, Error));
        auto Report = P4TexturePropertyReport(Document);
        Check(TEXT("Property success never claims complete defaults or editable import"),
            !Report->GetBoolField(TEXT("original_defaults_qualified")) &&
                !Report->GetBoolField(TEXT("original_editor_source_settings_recovered")) &&
                !Report->GetBoolField(TEXT("editable_import_qualified")));
        const auto ChangedNamePackage = [&](const char* Before, const char* After)
        {
            auto Changed = Package;
            for (auto& Item : Changed.Names)
                if (Item == Before)
                    Item = After;
            return Changed;
        };
        // The game's engine adds fifteen texture groups at the values of the stock engine's project
        // groups. Characters_ColorMap is the first (34, Project01) and Debug_Collision the last (48, Project15).
        auto Fork = ChangedNamePackage("TEXTUREGROUP_WorldNormalMap", "TEXTUREGROUP_Characters_ColorMap");
        FP4TexturePropertyDocument ForkDocument;
        Check(TEXT("A group the game's engine adds takes the project group of the same value"),
            P4InspectTextureProperties(Fork, Export, Properties, Body, Class, ForkDocument, Error) &&
                ForkDocument.TaggedPropertiesQualified && ForkDocument.NativePropertiesRepresentable &&
                ForkDocument.NativePropertyIssues.Num() == 0 &&
                ForkDocument.ValuesJson.Contains(TEXT("\"LODGroup\":\"TEXTUREGROUP_Project01\"")) &&
                !ForkDocument.ValuesJson.Contains(TEXT("Characters_ColorMap")) &&
                ForkDocument.GameTextureGroup.Equals(
                    TEXT("TEXTUREGROUP_Characters_ColorMap"), ESearchCase::CaseSensitive));
        Check(TEXT("A stock group keeps its name and records no game group"),
            Document.GameTextureGroup.IsEmpty() &&
                Document.ValuesJson.Contains(TEXT("\"LODGroup\":\"TEXTUREGROUP_WorldNormalMap\"")));
        if (Check(TEXT("The texture stages in that project group"),
                P4PrepareTextureProperties(Scratch.Get(), ForkDocument, Canonical, Error)))
            Check(TEXT("Project01 has the value the game gives Characters_ColorMap"),
                Scratch->LODGroup == TEXTUREGROUP_Project01 && int32(TEXTUREGROUP_Project01) == 34);
        auto Last = ChangedNamePackage("TEXTUREGROUP_WorldNormalMap", "TEXTUREGROUP_Debug_Collision");
        Check(TEXT("The last added group takes the last project group"),
            P4InspectTextureProperties(Last, Export, Properties, Body, Class, ForkDocument, Error) &&
                ForkDocument.NativePropertiesRepresentable &&
                ForkDocument.ValuesJson.Contains(TEXT("\"LODGroup\":\"TEXTUREGROUP_Project15\"")) &&
                int32(TEXTUREGROUP_Project15) == 48);
        const UEnum* Groups = FindObject<UEnum>(ANY_PACKAGE, TEXT("TextureGroup"));
        Check(TEXT("The editor shows the project groups under the game's names"),
            Groups &&
                Groups->GetDisplayNameTextByValue(TEXTUREGROUP_Project01).ToString() == TEXT("Characters_ColorMap") &&
                Groups->GetDisplayNameTextByValue(TEXTUREGROUP_Project07).ToString() == TEXT("Terrain_ColorMap") &&
                Groups->GetDisplayNameTextByValue(TEXTUREGROUP_Project15).ToString() == TEXT("Debug_Collision") &&
                Groups->GetDisplayNameTextByValue(TEXTUREGROUP_World).ToString() == TEXT("World"));
        // The game's own Project01 is its value 49, which the stock list does not have.
        auto GameProject = ChangedNamePackage("TEXTUREGROUP_WorldNormalMap", "TEXTUREGROUP_Project01");
        Check(TEXT("The game's own project groups are not taken for the stock ones of the same name"),
            P4InspectTextureProperties(GameProject, Export, Properties, Body, Class, ForkDocument, Error) &&
                ForkDocument.TaggedPropertiesQualified && !ForkDocument.NativePropertiesRepresentable &&
                ForkDocument.NativePropertyIssues.Num() == 1 && ForkDocument.GameTextureGroup.IsEmpty());
        const bool BeforeSRGB = Scratch->SRGB;
        const FGuid BeforeGuid = Scratch->GetLightingGuid();
        const auto BeforeGroup = Scratch->LODGroup;
        Check(TEXT("A group with no place refuses before any private writes"),
            !P4PrepareTextureProperties(Scratch.Get(), ForkDocument, Canonical, Error) && Scratch->SRGB == BeforeSRGB &&
                Scratch->GetLightingGuid() == BeforeGuid && Scratch->LODGroup == BeforeGroup);
        auto Unknown = ChangedNamePackage("NeverStream", "OriginalGameOnlyTextureSetting");
        const auto UnknownTags = ReadPropertyEnvelopes(Body, Unknown.Names, 0, Body.size());
        Check(TEXT("Unknown target field retains independently decoded original setting"),
            P4InspectTextureProperties(Unknown, Export, UnknownTags, Body, Class, ForkDocument, Error) &&
                !ForkDocument.NativePropertiesRepresentable &&
                ForkDocument.ValuesJson.Contains(TEXT("\"OriginalGameOnlyTextureSetting\":true")));
        auto Mismatch = ChangedNamePackage("SRGB", "LODBias");
        const auto MismatchTags = ReadPropertyEnvelopes(Body, Mismatch.Names, 0, Body.size());
        Check(TEXT("Mismatched target field type retains independent original source type/value"),
            P4InspectTextureProperties(Mismatch, Export, MismatchTags, Body, Class, ForkDocument, Error) &&
                !ForkDocument.NativePropertiesRepresentable &&
                ForkDocument.ValuesJson.Contains(TEXT("\"LODBias\":false")));
        auto UnknownReference = ChangedNamePackage("CompositeTexture", "OriginalOnlyTextureDependency");
        const auto ReferenceTags = ReadPropertyEnvelopes(Body, UnknownReference.Names, 0, Body.size());
        Check(TEXT("Unavailable native reference field preserves source path with unqualified expected class"),
            P4InspectTextureProperties(UnknownReference, Export, ReferenceTags, Body, Class, ForkDocument, Error) &&
                !ForkDocument.NativePropertiesRepresentable && ForkDocument.Dependencies.Num() == 1 &&
                ForkDocument.Dependencies[0].ExpectedClass.IsEmpty() &&
                ForkDocument.Dependencies[0].Path == TEXT("/Game/TextureDependency.TextureDependency"));
    }
    return Good;
}
#endif
