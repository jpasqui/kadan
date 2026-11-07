// The game builds Kadan knows, and how to tell which one a Paks folder holds.
//
// A build is named by its global container (global.utoc and global.ucas, the engine's script objects), which
// differs between every two builds Kadan has seen and is small enough to hash on every use. Builds come in
// families that share an engine version, container layout and shader format: what Kadan converts with and packs
// for. Within a family a mod's packages are the same; across families their native data and shaders differ.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace P4MapKit
{
struct FGameFamily
{
    const char* Id;           // "switch", "switch2"
    const char* Engine;       // retoc's --version: "UE4_26", "UE4_27"
    std::uint8_t TocVersion;  // the IoStore table-of-contents version of its containers
    const char* ShaderFormat; // the format suffix of its shader libraries
    bool KadanShaders;        // whether Kadan can compile shaders for it
};

struct FGameBuild
{
    const char* Id;        // "1.1.1", "2.0.0-switch", "2.0.0-switch2", "demo-switch2"
    const char* Label;     // as a person names it
    const char* Family;    // FGameFamily::Id
    const char* Container; // the stem of the game's own .pak/.utoc/.ucas in its Paks folder
    const char* ShaderArchive; // the game's shader library in Carrot4/Content
    const char* ProgramBuildId; // the program's build id (first 16 bytes, hexadecimal)
    std::uint64_t GlobalTocSize;
    const char* GlobalTocSha256;
    std::uint64_t GlobalCasSize;
    const char* GlobalCasSha256;
};

inline const std::vector<FGameFamily>& GameFamilies()
{
    static const std::vector<FGameFamily> Families = {
        {"switch", "UE4_26", 2, "GLSL_SWITCH", true},
        {"switch2", "UE4_27", 3, "SF_GLSL_SAGE", true},
    };
    return Families;
}

inline const std::vector<FGameBuild>& GameBuilds()
{
    static const std::vector<FGameBuild> Builds = {
        {"1.1.1", "Pikmin 4 1.1.1 (Nintendo Switch)", "switch", "Carrot4-Switch",
            "ShaderArchive-Carrot4-GLSL_SWITCH.ushaderbytecode", "060F827BD2B38703E9EA11E3210D89BC", 573,
            "68f12a6d9496faea1e44d31161d7ec4c7abbfaca1397e6aeceab98d78a5ef7db", 1396336,
            "05040e0443f5a661d6a310c5384a7714815a20098321ffe83b703a83281ef52d"},
        {"2.0.0-switch", "Pikmin 4 2.0.0 (Nintendo Switch)", "switch", "Carrot4-Switch",
            "ShaderArchive-Carrot4-GLSL_SWITCH.ushaderbytecode", "1F66B8766B2E3B4B113EA2D2EF57FF26", 585,
            "c176eb033819864144ebc1d1fafe4bd7971c0cb16f8671e81e3e7cd21b5c275f", 1399696, "7a9ef1ee5bd765817b42ba363e6488efb89a66104dd30076d708d36f69b412c7"},
        {"demo-switch2", "Pikmin 4 Nintendo Switch 2 Edition demo", "switch2", "Carrot4-Sage",
            "ShaderArchive-Carrot4-SF_GLSL_SAGE.ushaderbytecode", "FB43FD7D0A63D26A805B54D3EA308225", 597,
            "05c6adf127bae5fc4e2d3ac111c87b436314936865e3d038b2eff30536b2e064", 1467088,
            "5168d71fce401f376892545993edef3b9be381522d1421821efa6658723039ef"},
        {"2.0.0-switch2", "Pikmin 4 2.0.0 (Nintendo Switch 2 Edition)", "switch2", "pakchunk0-Sage",
            "ShaderArchive-Carrot4_Chunk0-SF_GLSL_SAGE.ushaderbytecode", "D3A417650133546A11EDFB4EF0FE70B9", 597,
            "05c6adf127bae5fc4e2d3ac111c87b436314936865e3d038b2eff30536b2e064", 1467088,
            "5168d71fce401f376892545993edef3b9be381522d1421821efa6658723039ef"},
    };
    return Builds;
}

inline const FGameFamily* FindGameFamily(const std::string& Id)
{
    for (const FGameFamily& Family : GameFamilies())
        if (Id == Family.Id)
            return &Family;
    return nullptr;
}

// The builds whose own container a folder holding Names (file names, any case) has: all three of its files.
// The demo and 2.0.0 on Switch 2 share a global container, so the container's name tells them apart.
inline std::vector<const FGameBuild*> BuildsWithContainer(const std::vector<std::string>& Names)
{
    auto Lower = [](std::string Text) {
        for (char& C : Text)
            if (C >= 'A' && C <= 'Z')
                C = char(C - 'A' + 'a');
        return Text;
    };
    std::vector<std::string> Folded;
    for (const std::string& Name : Names)
        Folded.push_back(Lower(Name));
    auto Has = [&](const std::string& Name) {
        const std::string Want = Lower(Name);
        for (const std::string& Each : Folded)
            if (Each == Want)
                return true;
        return false;
    };
    std::vector<const FGameBuild*> Out;
    for (const FGameBuild& Build : GameBuilds())
    {
        const std::string Stem = Build.Container;
        if (Has(Stem + ".pak") && Has(Stem + ".utoc") && Has(Stem + ".ucas"))
            Out.push_back(&Build);
    }
    return Out;
}

// The build a Paks folder holds: its own container is there and its global container is that build's, byte for
// byte. Null when no known build matches; Error then says what differed.
inline const FGameBuild* IdentifyGameBuild(const std::vector<std::string>& Names, std::uint64_t TocSize,
    const std::string& TocSha256, std::uint64_t CasSize, const std::string& CasSha256, std::string& Error)
{
    const std::vector<const FGameBuild*> Candidates = BuildsWithContainer(Names);
    if (Candidates.empty())
    {
        Error = "game-build-container-not-found";
        return nullptr;
    }
    for (const FGameBuild* Build : Candidates)
        if (Build->GlobalTocSize == TocSize && Build->GlobalCasSize == CasSize && TocSha256 == Build->GlobalTocSha256 &&
            CasSha256 == Build->GlobalCasSha256)
            return Build;
    Error = "game-build-global-container-unknown";
    return nullptr;
}

// The family of the containers in a folder from its own container's name alone, for folders whose global container
// is not there (a mod's output, a test fixture). Null when no known container is present, or names disagree.
inline const FGameFamily* FamilyOfContainers(const std::vector<std::string>& Names)
{
    const FGameFamily* Found = nullptr;
    for (const FGameBuild* Build : BuildsWithContainer(Names))
    {
        const FGameFamily* Family = FindGameFamily(Build->Family);
        if (Found && Found != Family)
            return nullptr;
        Found = Family;
    }
    return Found;
}
} // namespace P4MapKit
