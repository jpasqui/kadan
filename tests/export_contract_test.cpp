#include "core/export_contract.h"
#include <cstdlib>
#include <iostream>
#include <limits>
using namespace P4MapKit::Export;
#define CHECK(X) do { if (!(X)) { std::cerr << "Failed at " << __LINE__ << ": " << #X << '\n'; return 1; } } while (false)
int main(int Count, char** Args) {
    Descriptor Mod{"example.cave", "Cave \"mine\"", "1.2.3"};
    std::string Error; Names Naming;
    CHECK(DerivedNames(Mod, Naming, Error));
    CHECK(Naming.Root == "/Game/Mods/example_cave");
    CHECK(Naming.Container == "kadan_example_cave");
    Names Other; CHECK(DerivedNames({"examplec.ave", "Other", "0.0.0"}, Other, Error));
    CHECK(Naming.Root != Other.Root);
    CHECK(Naming.Root.compare(0, 11, "/Game/Mods/") == 0);
    Names Preview;
    CHECK(DerivedNames({"kadan-preview-0123456789abcdef0123456789abcdef", "Kadan live preview", "0.0.0"}, Preview, Error));
    CHECK(Preview.Root.compare(0, 19, "/Game/KadanPreview/") == 0);
    Names Lookalike;
    CHECK(DerivedNames({"kadan-preview-mine", "Mine", "1.0.0"}, Lookalike, Error));
    CHECK(Lookalike.Root.compare(0, 11, "/Game/Mods/") == 0);
    CHECK(ModFolderPackage(Preview.Root + "/Scene"));
    CHECK(ModFolderPackage(Naming.Root + "/Mine"));
    CHECK(!ModFolderPackage("/Game/Folder/Cave_P"));
    for (const char* Id : {"", ".abc", "abc.", "../abc", "ABC", "tsuchi.example", "a/b", "a:b"}) CHECK(!ValidId(Id));
    CHECK(ValidId(std::string(47, 'a'))); CHECK(!ValidId(std::string(48, 'a')));
    // K06: Tsuchi blocks a mod folder named exactly tsuchi, as it does every tsuchi. ID.
    CHECK(!ValidId("tsuchi"));
    CHECK(ValidId("tsuchi_mod"));
    CHECK(ValidId("tsuchimod"));
    for (const char* Version : {"", "1", "1.2", "01.2.3", "1.2.3-beta", "1.2.1000000000", "1.2.3.4"}) CHECK(!ValidVersion(Version));
    CHECK(ValidVersion("999999999.0.1"));
    MapEdit Map; Map.Package = "/Game/Folder/Cave_P"; Map.Levels = {Naming.Root + "/Mine"}; Map.Navigation = true;
    Map.Changes["Cave_GameArea"].Remove = {"Rock_3"};
    Map.Changes["Cave_GameArea"].Move["Wall_1"].Location = {{120,-40,0}};
    Map.Changes["Cave_GameArea"].Move["Wall_1"].Rotation = {{0,90,0}};
    std::map<std::string,std::string> Files;
    CHECK(FrameworkFiles(Mod, Map, Files, Error)); CHECK(Files.size() == 2);
    CHECK(Files["mod.json"].find("\"framework\":0,") != std::string::npos);
    {
        // A creator's level ships at its own path, in /Game/Mods too, as review accepts it. Kadan's live preview
        // folder is refused, and so is a level named twice, which Tsuchi would refuse while loading the map.
        MapEdit Own; Own.Package = "/Game/Folder/Cave_P";
        Own.Levels = {"/Game/MyLevels/Mine", Naming.Root + "/Other"};
        std::map<std::string,std::string> Written;
        CHECK(FrameworkFiles(Mod, Own, Written, Error));
        CHECK(Written["mod.json"].find("\"levels\":[\"/Game/MyLevels/Mine\",\"" + Naming.Root + "/Other\"]") != std::string::npos);
        Own.Levels = {Other.Root + "/Mine"};
        CHECK(FrameworkFiles(Mod, Own, Written, Error));
        Own.Levels = {Preview.Root + "/Mine"};
        CHECK(!FrameworkFiles(Mod, Own, Written, Error));
        Own.Levels = {"/Game/MyLevels/Mine", "/Game/MyLevels/mine"};
        CHECK(!FrameworkFiles(Mod, Own, Written, Error));
    }
    auto Before = Files;
    Map.Changes["Cave_GameArea"].Move["Wall_1"].Scale[0] = std::numeric_limits<double>::infinity();
    CHECK(!FrameworkFiles(Mod, Map, Files, Error)); CHECK(Files == Before);
    {
        MapEdit Values; Values.Package = "/Game/Folder/Cave_P";
        SetValue Hidden; Hidden.Type = SetValue::Kind::Boolean; Hidden.Boolean = false;
        SetValue Kill; Kill.Type = SetValue::Kind::Number; Kill.Number = -500.5;
        SetValue Mode; Mode.Type = SetValue::Kind::Integer; Mode.Integer = 2;
        SetValue Mesh; Mesh.Type = SetValue::Kind::Text; Mesh.Text = "/Game/Meshes/Rock.Rock";
        Values.Changes["Cave_GameArea"].Set["Lamp_2"]["LightComponent.CastShadows"] = Hidden;
        Values.Changes["Cave_GameArea"].Set["Lamp_2"]["Mobility"] = Mode;
        Values.Changes["Cave_GameArea"].Set["Rock_1"]["StaticMeshComponent.StaticMesh"] = Mesh;
        Values.Changes["Cave_GameArea"].Set["MyWorldSettings"]["KillZ"] = Kill;
        std::map<std::string,std::string> Written;
        CHECK(FrameworkFiles(Mod, Values, Written, Error));
        CHECK(Written["maps/Cave_P.json"] == "{\"levels\":{\"Cave_GameArea\":{\"set\":{\"Lamp_2\":{\"LightComponent.CastShadows\":false,\"Mobility\":2},"
            "\"MyWorldSettings\":{\"KillZ\":-500.5},\"Rock_1\":{\"StaticMeshComponent.StaticMesh\":\"/Game/Meshes/Rock.Rock\"}}}}}\n");
        Values.Changes["Cave_GameArea"].Set["Lamp_2"]["Light..Bad"] = Hidden;
        CHECK(!FrameworkFiles(Mod, Values, Written, Error));
    }
    {
        // Placement lists: only the mod's own, each once, and a map entry may have nothing else.
        MapEdit Lists; Lists.Package = "/Game/Folder/Cave_P"; Lists.Placements = {Naming.Root + "/Placements/Cave_P_Objects"};
        std::map<std::string,std::string> Written;
        CHECK(FrameworkFiles(Mod, Lists, Written, Error)); CHECK(Written.size() == 1);
        CHECK(Written["mod.json"].find("\"maps\":[{\"map\":\"/Game/Folder/Cave_P\",\"placements\":[\"" + Naming.Root + "/Placements/Cave_P_Objects\"]}]") != std::string::npos);
        Lists.Placements.push_back(Lists.Placements[0]); CHECK(!FrameworkFiles(Mod, Lists, Written, Error));
        Lists.Placements = {"/Game/Carrot4/Maps/AP_Cave"}; CHECK(!FrameworkFiles(Mod, Lists, Written, Error));
    }
    {
        // Moves and removals of the game's own placement rows go into the map entry, by list and number.
        const std::string List = "/Game/Carrot4/Maps/Madori/Cave/Cave000/Cave000_F00/ActorPlacementInfo/AP_Cave000_F00_P_Objects";
        MapEdit Rows; Rows.Package = "/Game/Carrot4/Maps/Madori/Cave/Cave000/Cave000_F00/Cave000_F00_P";
        Rows.PlacementChanges[List].Move[UINT64_C(17179869271)].Location = {{121, -49.5, 0}};
        Rows.PlacementChanges[List].Move[UINT64_C(17179869271)].Rotation = {{0, 28.5, 0}};
        Rows.PlacementChanges[List].Remove = {UINT64_C(17179869268)};
        std::map<std::string,std::string> Written;
        CHECK(FrameworkFiles(Mod, Rows, Written, Error)); CHECK(Written.size() == 1);
        CHECK(Written["mod.json"].find("\"maps\":[{\"map\":\"" + Rows.Package + "\",\"placement_changes\":{\"" + List +
            "\":{\"move\":{\"17179869271\":{\"location\":[121,-49.5,0],\"rotation\":[0,28.5,0],\"scale\":[1,1,1]}},\"remove\":[\"17179869268\"]}}}]") != std::string::npos);
        const auto Saved = Written;
        Rows.PlacementChanges[List].Remove.push_back(UINT64_C(17179869271)); CHECK(!FrameworkFiles(Mod, Rows, Written, Error)); CHECK(Written == Saved);
        Rows.PlacementChanges[List].Remove = {0}; CHECK(!FrameworkFiles(Mod, Rows, Written, Error));
        Rows.PlacementChanges.clear(); Rows.PlacementChanges[Naming.Root + "/Placements/AP_Mine"].Remove = {7}; CHECK(!FrameworkFiles(Mod, Rows, Written, Error));
        Rows.PlacementChanges.clear(); Rows.PlacementChanges["/Game/Carrot4/Maps/APL_Cave"].Remove = {7}; CHECK(!FrameworkFiles(Mod, Rows, Written, Error));
        CHECK(Written == Saved);
    }
    {
        MapEdit First; First.Package = "/Game/First/Shared_P";
        First.Changes["Shared_Area"].Remove = {"Rock_1"};
        MapEdit Second; Second.Package = "/Game/Second/Shared_P";
        Second.Changes["Shared_Area"].Move["Rock_2"].Location = {{42,0,0}};
        std::map<std::string,std::string> Written;
        CHECK(FrameworkFiles(Mod, std::vector<MapEdit>{First, Second}, Written, Error));
        CHECK(Written.size() == 3);
        CHECK(Written.count("maps/1_Shared_P.json") && Written.count("maps/2_Shared_P.json"));
        CHECK(Written["maps/1_Shared_P.json"].find("Rock_1") != std::string::npos);
        CHECK(Written["maps/2_Shared_P.json"].find("Rock_2") != std::string::npos);
        CHECK(Written["mod.json"].find(First.Package) != std::string::npos && Written["mod.json"].find(Second.Package) != std::string::npos);
        const auto Saved = Written;
        CHECK(!FrameworkFiles(Mod, std::vector<MapEdit>{First, First}, Written, Error)); CHECK(Written == Saved);
        Second.Package = "/game/first/shared_p";
        CHECK(!FrameworkFiles(Mod, std::vector<MapEdit>{First, Second}, Written, Error)); CHECK(Written == Saved);
        CHECK(!FrameworkFiles(Mod, std::vector<MapEdit>{}, Written, Error)); CHECK(Written == Saved);
    }
    CHECK(!PackageName("/Game/Foo/../Other")); CHECK(!PackageName("/Game/Foo/"));
    CHECK(AdmitPackage("/Game/Mods/Mine", false, nullptr, Error));
    CHECK(!CheckPackageInventory({{"/Game/Mods/Mine",false,nullptr}}, false, Error));
    CHECK(CheckPackageInventory({{"/Game/Mods/Mine",false,nullptr}}, true, Error));
    CHECK(!CheckPackageInventory({{"/Game/Mods/Mine",false,nullptr},{"/Game/Mods/Mine",false,nullptr}}, true, Error));
    CHECK(!CheckPackageInventory({{"/Game/Mods/Mine",false,nullptr},{"/Game/Mods/mine",false,nullptr}}, true, Error));
    CHECK(AllowedFile(Mod, "romfs/tsuchi/mods/example.cave/mod.json", Error));
    CHECK(AllowedFile(Mod, "romfs/tsuchi/mods/example.cave/sounds.bnk", Error));
    CHECK(!AllowedFile(Mod, "romfs/tsuchi/mods/another.mod/sounds.bnk", Error));
    const std::string Archive = "ShaderArchive-kadan_" + Naming.Container.substr(Naming.Container.size()-16) + "-GLSL_SWITCH.ushaderbytecode";
    CHECK(!AllowedFile(Mod, "romfs/tsuchi/mods/example.cave/" + Archive, Error));   // the library travels inside the .pak
    CHECK(!AllowedFile(Mod, "romfs/tsuchi/mods/example.cave/shaders.bin", Error));
    CHECK(!AllowedFile(Mod, "romfs/tsuchi/mods/another.mod/" + Archive, Error));
    CHECK(!AllowedFile(Mod, "romfs/tsuchi/mods/another.mod/shaders.bin", Error));
    CHECK(!AllowedFile(Mod, "romfs/tsuchi/mods/example.cave/materials/shaders.bin", Error));
    CHECK(AllowedFile(Mod, "romfs/tsuchi/mods/example.cave/maps/Cave_P.nav", Error));
    for (const char* Extension : {".pak", ".ucas", ".utoc"}) {
        CHECK(AllowedFile(Mod, "romfs/tsuchi/mods/example.cave/" + Naming.Container + "_P" + Extension, Error));
        CHECK(!AllowedFile(Mod, "romfs/Carrot4/Content/Paks/" + Naming.Container + "_P" + Extension, Error));
        CHECK(!AllowedFile(Mod, "romfs/tsuchi/mods/another.mod/" + Naming.Container + "_P" + Extension, Error));
        CHECK(!AllowedFile(Mod, "romfs/tsuchi/mods/example.cave/unowned_P" + std::string(Extension), Error));
    }
    CHECK(!AllowedFile(Mod, "romfs/tsuchi/mods/example.cave/maps/../../game.uasset", Error));
    CHECK(!AllowedFile(Mod, "romfs/p4m/mods/example.cave/levels.ini", Error));
    CHECK(!AllowedFile(Mod, "private/game.bin", Error));
    CHECK(!AdmitPackage("/Game/GameMesh", true, nullptr, Error));
    Origin Proof{"/Game/GameMesh", "StaticMesh", std::string(64,'a'), std::string(64,'b'), true, true, true};
    CHECK(AdmitPackage(Proof.Package, true, &Proof, Error));
    Proof.SourceVerified = false; CHECK(!AdmitPackage(Proof.Package, true, &Proof, Error));
    Proof.SourceVerified = true; Proof.Kind = "World"; CHECK(!AdmitPackage(Proof.Package, true, &Proof, Error));
    Proof.Kind = "StaticMesh"; Proof.CookedDigest = Proof.OriginalDigest; CHECK(!AdmitPackage(Proof.Package, true, &Proof, Error));
    // A JSON envelope lets the Python fixture compare parsed contents with its existing writer.
    if (Count > 1 && std::string(Args[1]) == "--json") {
        std::cout << "{\"root\":" << Quote(Naming.Root) << ",\"container\":" << Quote(Naming.Container) << ",\"files\":{";
        bool First = true;
        for (const auto& File : Before) { if (!First) std::cout << ','; First = false; std::cout << Quote(File.first) << ':' << File.second; }
        std::cout << "}}\n";
    } else std::cout << "Native export contract checks passed\n";
}
