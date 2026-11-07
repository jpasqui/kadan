#include "../core/game_builds.h"
#include <cstdlib>
#include <iostream>
#include <set>
using namespace P4MapKit;
static void Check(bool Good, const char* What)
{
    if (!Good) { std::cerr << "game builds check failed: " << What << "\n"; std::exit(1); }
}
static const FGameBuild& Named(const char* Id)
{
    for (const FGameBuild& Build : GameBuilds())
        if (std::string(Build.Id) == Id)
            return Build;
    std::cerr << "no build " << Id << "\n";
    std::exit(1);
}
static std::vector<std::string> Folder(const char* Stem, bool Global = true)
{
    std::vector<std::string> Names = {std::string(Stem) + ".pak", std::string(Stem) + ".utoc", std::string(Stem) + ".ucas"};
    if (Global) { Names.push_back("global.utoc"); Names.push_back("global.ucas"); }
    return Names;
}
static const FGameBuild* Identify(const std::vector<std::string>& Names, const FGameBuild& Global, std::string& Error)
{
    return IdentifyGameBuild(Names, Global.GlobalTocSize, Global.GlobalTocSha256, Global.GlobalCasSize,
        Global.GlobalCasSha256, Error);
}
int main()
{
    // Every build names a family, a 64-digit digest pair and a 32-digit program build id, and ids are unique.
    std::set<std::string> Ids;
    for (const FGameBuild& Build : GameBuilds())
    {
        Check(Ids.insert(Build.Id).second, "build ids are unique");
        Check(FindGameFamily(Build.Family) != nullptr, "every build names a known family");
        Check(std::string(Build.GlobalTocSha256).size() == 64 && std::string(Build.GlobalCasSha256).size() == 64,
            "global digests are SHA-256");
        Check(std::string(Build.ProgramBuildId).size() == 32, "program build ids are 16 bytes");
        Check(std::string(Build.ShaderArchive).find(FindGameFamily(Build.Family)->ShaderFormat) != std::string::npos,
            "a build's shader library is in its family's format");
    }
    std::string Error;
    // Each Switch build is told apart by its global container.
    Check(Identify(Folder("Carrot4-Switch"), Named("1.1.1"), Error) == &Named("1.1.1"), "1.1.1 identified");
    Check(Identify(Folder("Carrot4-Switch"), Named("2.0.0-switch"), Error) == &Named("2.0.0-switch"),
        "Switch 2.0.0 identified");
    // The Switch 2 demo and 2.0.0 share a global container; their own containers' names tell them apart.
    Check(Identify(Folder("Carrot4-Sage"), Named("demo-switch2"), Error) == &Named("demo-switch2"), "demo identified");
    Check(Identify(Folder("pakchunk0-Sage"), Named("2.0.0-switch2"), Error) == &Named("2.0.0-switch2"),
        "Switch 2 Edition identified");
    Check(Identify(Folder("PAKCHUNK0-sage"), Named("2.0.0-switch2"), Error) == &Named("2.0.0-switch2"),
        "container names compare without case");
    // A global container of another family's build, or an unknown one, is no build.
    Check(!Identify(Folder("Carrot4-Switch"), Named("2.0.0-switch2"), Error) &&
            Error == "game-build-global-container-unknown",
        "a Switch 2 global container in a Switch folder is refused");
    Check(!IdentifyGameBuild(Folder("Carrot4-Switch"), 573, std::string(64, '0'), 1396336, std::string(64, '0'), Error),
        "an unknown digest is refused");
    // A container needs all three files.
    Check(!Identify({"Carrot4-Switch.pak", "Carrot4-Switch.utoc"}, Named("1.1.1"), Error) &&
            Error == "game-build-container-not-found",
        "a partial container is no container");
    // Families from container names alone, and their engine versions.
    Check(FamilyOfContainers(Folder("Carrot4-Switch", false)) == FindGameFamily("switch"), "Switch family by name");
    Check(FamilyOfContainers(Folder("pakchunk0-Sage", false)) == FindGameFamily("switch2"), "Switch 2 family by name");
    std::vector<std::string> Mixed = Folder("Carrot4-Switch", false);
    for (const std::string& Name : Folder("pakchunk0-Sage", false))
        Mixed.push_back(Name);
    Check(FamilyOfContainers(Mixed) == nullptr, "two families in one folder is no family");
    Check(FamilyOfContainers({"mod_P.utoc"}) == nullptr, "a mod's container names no family");
    Check(std::string(FindGameFamily("switch")->Engine) == "UE4_26" && FindGameFamily("switch")->TocVersion == 2,
        "Switch is UE 4.26");
    Check(std::string(FindGameFamily("switch2")->Engine) == "UE4_27" && FindGameFamily("switch2")->TocVersion == 3,
        "Switch 2 is UE 4.27");
    Check(FindGameFamily("switch")->KadanShaders && FindGameFamily("switch2")->KadanShaders,
        "Kadan compiles shaders for both families (Switch 2's converted from Switch's, core/sage_own_map.h)");
    std::cout << "game builds: all checks passed\n";
    return 0;
}
