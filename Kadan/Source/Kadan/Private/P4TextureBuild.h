#pragma once
#include "P4NumericContract.h"
#include "CoreMinimal.h"
#include "P4MapKitCore/package_reader.h"
#include <string>
#include <vector>

class UTexture;

// Recover the BC3 cook choice from the surviving payload. Stock AutoDXT would
// otherwise turn an opaque BC3 texture into BC1. This changes no source pixels;
// the format-selecting flag is only used with preserved mips (never generated).
KADAN_API bool P4RecoverOriginalTextureCookFormat(UTexture* Texture, const FString& GameFormat);

// What an ordinary texture made from one of the game's textures holds, against the game's file.
struct FP4TextureBuildReport
{
    FString Class, Package, Object, GameFormat, BuiltFormat, GameTextureGroup;
    int32 Width = 0, Height = 0, Slices = 0, Mips = 0;
    // How many larger mips the game's cook left out of its file. They cannot be brought back.
    int32 FirstMip = 0;
    int32 BuiltWidth = 0, BuiltHeight = 0, BuiltSlices = 0, BuiltMips = 0;
    // Every surviving mip of the game's texture, decoded, is the texture's editable source pixel for pixel.
    bool SourceEqual = false;
    // The properties the game's file sets were applied and read back.
    bool PropertiesEqual = false;
    // The engine compressed the texture to the game's pixel format, size, slice count and mip count.
    bool LayoutEqual = false;
    // Compressed payload bytes are a separate comparison from decoded source
    // pixels and layout. Recompression need not reproduce the game's blocks.
    bool BuiltBytesCompared = false, BuiltBytesEqual = false;
    // The largest difference in any channel of any pixel between the engine's compressed texture,
    // decoded again, and the game's, on a scale of 0 to 255. The engine compresses the decoded pixels
    // again, which need not give the game's blocks back. -1 when it was not measured.
    int32 LargestBuiltDifference = -1;
    // Source pixels, properties and layout equal; not compressed-byte identity.
    bool Equal = false;
};

// Makes an ordinary editable texture from one of the game's cooked textures: its surviving mips become
// the source image, the properties its file sets are applied, and the engine builds it. The texture is
// made in the transient package and returned whatever the comparison found; null with Error set when
// it could not be made. HeaderFile is the .uasset; the .uexp and any .ubulk are found next to it.
UTexture* P4BuildOriginalTexture(const FString& HeaderFile, const FString& BodyFile,
    FP4TextureBuildReport& Report, FString& Error);

// The same for one texture export of a package that may hold others (a level's built lighting holds its
// lightmaps): Body is the package's .uexp, read with Package; .ubulk files are found next to HeaderFile. Without
// CompareWithCook the texture is not compressed for the cook platform and compared: for a texture that never ships.
UTexture* P4BuildOriginalPackageTexture(const FString& HeaderFile, const std::vector<std::uint8_t>& Body,
    const P4MapKit::FPackageInventory& Package, const P4MapKit::FPackageExport* Export, FP4TextureBuildReport& Report,
    FString& Error, bool CompareWithCook = true);

// Builds the texture, reports the comparison as JSON and discards it. Nothing is added to Content.
FString P4BuildOriginalTextureReport(const FString& HeaderFile, const FString& BodyFile);

// Reads the cooked image of a texture that lies inside a package, from TailStart to TailEnd of the package's
// .uexp (a level's own height maps, whose mips are all in the .uexp). Class is the texture's class path. With
// Into, Into gets the surviving mips as its source image, kept as they are, as the landscape editor makes its
// height maps. False with Error when the data is not such an image.
bool P4ReadPackageTextureImage(const P4MapKit::FPackageInventory& Package, const std::vector<std::uint8_t>& Body,
    const std::string& Class, std::size_t TailStart, std::size_t TailEnd, UTexture* Into, FString& Error);
// Relocate completely decoded inline texture payloads and qualified BodySetup bulk containers
// after tagged values grow. Pixel/cooking bytes stay exact; physics geometry admission is separate.
// Unknown/external layouts fail before either caller-owned buffer changes.
bool P4RelocatePackageTextureImages(const std::vector<uint8>& BeforeHeader, const std::vector<uint8>& BeforeBody,
    const std::vector<uint8>& AfterHeader, std::vector<uint8>& AfterBody, FString& Error);
// Gives each body setup's inline collision payload the place in the file its own header and body give it, after a
// translation that changed the header's size and left the payloads saying the old place (P4MapKit::
// PlaceInlinePhysicsBodyBulk). A body setup without such a payload is left as it is.
bool P4PlacePackageInlinePhysicsBulk(const std::vector<uint8>& Header, std::vector<uint8>& Body, FString& Error);
