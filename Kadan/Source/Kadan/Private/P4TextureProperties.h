#pragma once
#include "CoreMinimal.h"
#include "P4MapKitCore/property_decoder.h"

class UTexture;
class FJsonObject;

// A source-derived property document, never an editable asset admission token.
// Complete source decoding and installed native representability are separate:
// unknown target enum/field/type values remain exact source values with issues.
// Omitted fields have unknown original defaults until separately qualified.
struct FP4TexturePropertyDocument
{
    FString ClassPath, Package, ValuesJson;
    // The texture's group as the game names it, when it is one of the fifteen the game's engine adds.
    // ValuesJson then holds the stock project group of the same value. Empty otherwise.
    FString GameTextureGroup;
    TArray<FString> ExplicitFields, MissingRuntimeDefaults, MissingEditorSourceSettings;
    TArray<FString> NativePropertyIssues;
    // Properties the game's engine has and this one does not, as "Name = value" lines, when the caller
    // asked for them to be kept beside the asset instead of refusing it. They are not in ValuesJson.
    TArray<FString> NotHeld;
    struct FDependency
    {
        FString Field, Path, ExpectedClass;
        bool Soft = false;
    };
    TArray<FDependency> Dependencies;
    bool TaggedPropertiesQualified = false;
    bool NativePropertiesRepresentable = false;
};

KADAN_API bool P4InspectTextureProperties(const P4MapKit::FPackageInventory& Package,
    const P4MapKit::FPackageExport& Export, const P4MapKit::FPropertyInventory& Properties,
    const std::vector<std::uint8_t>& Body, UClass* Schema, FP4TexturePropertyDocument& Document, FString& Error);

// The same inspection for an export of any class (Label names it in refusals). Skip names tagged
// properties the caller reads itself, which are left out of the document. Adapt may change a decoded value
// before it is checked against the installed class, and returns false with Issue set for a value that
// class cannot hold.
bool P4InspectObjectProperties(const P4MapKit::FPackageInventory& Package, const P4MapKit::FPackageExport& Export,
    const P4MapKit::FPropertyInventory& Properties, const std::vector<std::uint8_t>& Body, UClass* Schema,
    const TCHAR* Label, const TSet<FString>& Skip, FP4TexturePropertyDocument& Document, FString& Error,
    const TFunction<bool(const FString&, P4MapKit::FDecodedValue&, FP4TexturePropertyDocument&, FString&)>& Adapt = {},
    bool AbsentAsNotHeld = false);
// Applies a document to a disposable object of the document's class, exactly, with readback.
bool P4PrepareObjectProperties(
    UObject* Scratch, const FP4TexturePropertyDocument& Document, FString& Canonical, FString& Error);

// Requires a private transient editor-thread texture and the original class.
// Does not touch Source/platform data, fire build callbacks, publish or save.
// Exact generic property staging/readback uses the owning authoring helpers.
KADAN_API bool P4PrepareTextureProperties(
    UTexture* Scratch, const FP4TexturePropertyDocument& Document, FString& Canonical, FString& Error);
KADAN_API TSharedPtr<FJsonObject> P4TexturePropertyReport(const FP4TexturePropertyDocument& Document);

// Shows the stock engine's fifteen project texture groups under the names of the groups the game's
// engine has at the same values, for this editor session. Names the project's own ini gives are kept.
void P4NameGameTextureGroups();

#if WITH_DEV_AUTOMATION_TESTS && P4_MAPKIT_TESTS
class FAutomationTestBase;
bool P4TestTextureProperties(FAutomationTestBase& Test);
#endif
