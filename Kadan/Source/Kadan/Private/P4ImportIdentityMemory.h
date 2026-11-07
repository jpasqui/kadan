#pragma once
#include "CoreMinimal.h"
#include <cstdint>
#include <map>
#include <string>
#include <vector>

// The import identities the Blueprint planner verified against the original Zen package, kept for the run and on
// this computer (under the bytes' digests) so a later reader of the same converted bytes names the same objects.
// Retoc writes /Engine/UnknownPackage for an import it cannot name, both for what the game does not ship and for
// what it ships in a package retoc's name table lacks; only these identities tell the two apart.
void P4RememberImportIdentities(const FString& Package, const std::vector<std::uint8_t>& Header,
    const std::vector<std::uint8_t>& Body, const std::map<std::int32_t, std::string>& Imports);

// The identities kept for the package when they were verified against exactly these bytes, else empty.
std::map<std::int32_t, std::string> P4RememberedImportIdentities(
    const FString& Package, const std::vector<std::uint8_t>& Header, const std::vector<std::uint8_t>& Body);
