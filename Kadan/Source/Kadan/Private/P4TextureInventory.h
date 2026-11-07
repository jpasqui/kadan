#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "P4MapKitCore/package_reader.h"
#include "P4MapKitCore/texture_payload.h"

// The shape of a texture class by its path, and whether it is a lightmap. False for other classes.
bool P4TextureShape(const std::string& Class, P4MapKit::ETextureShape& Shape, bool& LightMap);
// The block layout of an engine pixel format by its name. False for formats without a plain layout.
bool P4TexturePixelLayout(const std::string& Name, P4MapKit::FTexturePixelLayout& Layout);

// Null for other classes. Read-only native archive inventory never admits Content.
KADAN_API TSharedPtr<FJsonObject> P4TexturePayloadInventory(const P4MapKit::FPackageInventory& Inventory,
    const P4MapKit::FPackageExport& Export, const P4MapKit::FPropertyInventory& Properties,
    const std::vector<std::uint8_t>& Body, const FString& HeaderFile, bool DecodePixels = false);
