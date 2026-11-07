#pragma once
#include "CoreMinimal.h"

// Package names under the project's content root, /Game/. The root is compared case-sensitively, as
// Unreal writes it, except by the IgnoringCase checks.

// Whether Package starts with /Game/.
bool P4IsContentPackage(const FString& Package);

// Whether Package starts with /Game/ and is a valid long package name.
bool P4IsValidContentPackage(const FString& Package);

// The same two checks with the root's letter case ignored, for the checks that must also catch a
// package named /game/.
bool P4IsContentPackageIgnoringCase(const FString& Package);
bool P4IsValidContentPackageIgnoringCase(const FString& Package);

// Package's path under /Game/: "/Game/A/B" gives "A/B". Package must be a content package.
FString P4ContentRelativePath(const FString& Package);

// Package's path in the game's containers: "/Game/A/B" gives "Carrot4/Content/A/B".
FString P4ContainerRelativePath(const FString& Package);

// The object path of a package's main asset: "/Game/A/B" gives "/Game/A/B.B".
FString P4ObjectPathOf(const FString& Package);

// The class a Blueprint package generates: "/Game/A/B" gives "/Game/A/B.B_C".
FString P4ClassPathOf(const FString& Package);

// Whether Package is in a folder Kadan makes for a mod: a mod's folder under /Game/Mods or the live preview's
// folder. Letter case is ignored.
bool P4IsModFolderPackage(const FString& Package);

// The folder of a mod-folder package's mod: "/Game/Mods/M/A/B" gives "/Game/Mods/M" and a package under the live
// preview's folder its preview mod's folder. Empty when Package is not in a mod's folder (or lies directly in the
// folder of all mods).
FString P4ModFolderRootOf(const FString& Package);
