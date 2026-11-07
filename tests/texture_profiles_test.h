#pragma once
#include "core/texture_profiles.h"

static std::vector<P4MapKit::FTextureGroupRecord> P4OriginalTextureGroups()
{
    // Literal independent observed 1.1.1 source record, not stock slot aliases.
    return {{"TEXTUREGROUP_World", 0}, {"TEXTUREGROUP_WorldNormalMap", 1}, {"TEXTUREGROUP_WorldSpecular", 2},
        {"TEXTUREGROUP_Character", 3}, {"TEXTUREGROUP_CharacterNormalMap", 4}, {"TEXTUREGROUP_CharacterSpecular", 5},
        {"TEXTUREGROUP_Weapon", 6}, {"TEXTUREGROUP_WeaponNormalMap", 7}, {"TEXTUREGROUP_WeaponSpecular", 8},
        {"TEXTUREGROUP_Vehicle", 9}, {"TEXTUREGROUP_VehicleNormalMap", 10}, {"TEXTUREGROUP_VehicleSpecular", 11},
        {"TEXTUREGROUP_Cinematic", 12}, {"TEXTUREGROUP_Effects", 13}, {"TEXTUREGROUP_EffectsNotFiltered", 14},
        {"TEXTUREGROUP_Skybox", 15}, {"TEXTUREGROUP_UI", 16}, {"TEXTUREGROUP_Lightmap", 17},
        {"TEXTUREGROUP_RenderTarget", 18}, {"TEXTUREGROUP_MobileFlattened", 19},
        {"TEXTUREGROUP_ProcBuilding_Face", 20}, {"TEXTUREGROUP_ProcBuilding_LightMap", 21},
        {"TEXTUREGROUP_Shadowmap", 22}, {"TEXTUREGROUP_ColorLookupTable", 23}, {"TEXTUREGROUP_Terrain_Heightmap", 24},
        {"TEXTUREGROUP_Terrain_Weightmap", 25}, {"TEXTUREGROUP_Bokeh", 26}, {"TEXTUREGROUP_IESLightProfile", 27},
        {"TEXTUREGROUP_Pixels2D", 28}, {"TEXTUREGROUP_HierarchicalLOD", 29}, {"TEXTUREGROUP_Impostor", 30},
        {"TEXTUREGROUP_ImpostorNormalDepth", 31}, {"TEXTUREGROUP_8BitData", 32}, {"TEXTUREGROUP_16BitData", 33},
        {"TEXTUREGROUP_Characters_ColorMap", 34}, {"TEXTUREGROUP_Characters_NormalMap", 35}, {"TEXTUREGROUP_Characters_MaskMap", 36},
        {"TEXTUREGROUP_Carrot_ColorMap", 37}, {"TEXTUREGROUP_Carrot_NormalMap", 38}, {"TEXTUREGROUP_Carrot_MaskMap", 39},
        {"TEXTUREGROUP_Terrain_ColorMap", 40}, {"TEXTUREGROUP_Terrain_NormalMap", 41}, {"TEXTUREGROUP_Terrain_MaskMap", 42},
        {"TEXTUREGROUP_Objects_ColorMap", 43}, {"TEXTUREGROUP_Objects_NormalMap", 44}, {"TEXTUREGROUP_Objects_MaskMap", 45},
        {"TEXTUREGROUP_UI_2D", 46}, {"TEXTUREGROUP_UI_3D", 47}, {"TEXTUREGROUP_Debug_Collision", 48},
        {"TEXTUREGROUP_Project01", 49}, {"TEXTUREGROUP_Project02", 50}, {"TEXTUREGROUP_Project03", 51},
        {"TEXTUREGROUP_Project04", 52}, {"TEXTUREGROUP_Project05", 53}, {"TEXTUREGROUP_Project06", 54},
        {"TEXTUREGROUP_Project07", 55}, {"TEXTUREGROUP_Project08", 56}, {"TEXTUREGROUP_Project09", 57},
        {"TEXTUREGROUP_Project10", 58}, {"TEXTUREGROUP_Project11", 59}, {"TEXTUREGROUP_Project12", 60},
        {"TEXTUREGROUP_Project13", 61}, {"TEXTUREGROUP_Project14", 62}, {"TEXTUREGROUP_Project15", 63}, {"TEXTUREGROUP_MAX", 64}};
}
static void CheckTextureProfiles()
{
    using namespace P4MapKit;
    const auto Groups = P4OriginalTextureGroups();
    const auto Contract = ETextureGroupContract::Original111_64;
    const std::vector<std::string> Sections{"Windows DeviceProfile", "Switch DeviceProfile"};
    const std::string Row = "+TextureLODGroups=( Group=TEXTUREGROUP_Carrot_ColorMap,Future=(X=1,Text=\"a,b\"),Future=\"c\\\"d\",C4MinResidentMipCount=2)";
    const std::string Source = "\xef\xbb\xbf; comment\r\n[Windows DeviceProfile]\r\nDeviceType=Windows\r\nBaseProfileName=\r\n" + Row + "\r\n" + Row +
        "\n.TextureLODGroups=(Group=TEXTUREGROUP_Project01)\r-TextureLODGroups=(Group=TEXTUREGROUP_Project15)\n" +
        "!TextureLODGroups=ClearArray\nTextureLODGroups=(Group=TEXTUREGROUP_UnknownOriginal,Future=8)\n+CVars=r.Unknown=2\n" +
        "[Unselected]\nbad ignored line\n# comment\n[Switch DeviceProfile]\n+TextureLODGroups=(Group=TEXTUREGROUP_Shadowmap)";
    FTextureProfileInventory Inventory; FReadError Error;
    Check(ReadTextureProfileInventory(Source, Groups, Contract, Sections, Inventory, Error));
    Check(Error.Code.empty()); Check(Inventory.Groups.size() == 65); Check(Inventory.SourceBytes == Source.size());
    Check(Inventory.BomBytes == 3); Check(Inventory.Entries.size() == 10);
    Check(!Inventory.EffectiveConfigResolved && !Inventory.OriginalDefaultsQualified && !Inventory.ProfileInheritanceResolved && !Inventory.C4BehaviorQualified);
    Check(Inventory.Groups[34].Name == "TEXTUREGROUP_Characters_ColorMap"); Check(Inventory.Groups[49].Value == 49);
    Check(Inventory.Groups.back().Name == "TEXTUREGROUP_MAX" && Inventory.Groups.back().Value == 64);
    const auto& A = Inventory.Entries[2]; Check(A.Raw == Row); Check(A.Operation == "+"); Check(A.Group == "TEXTUREGROUP_Carrot_ColorMap");
    Check(A.OriginalGroupKnown); Check(A.Members.size() == 4); Check(A.Members[1].Value == "(X=1,Text=\"a,b\")");
    Check(A.Members[2].DuplicateKey); Check(!A.Members[1].DuplicateKey); Check(A.LineEnding == "\r\n");
    Check(A.Offset == Source.find(Row)); Check(Source.substr(A.Offset, A.End - A.Offset) == A.Raw);
    Check(Inventory.Entries[3].DuplicateRow && !A.DuplicateRow);
    Check(Inventory.Entries[4].Operation == "." && Inventory.Entries[4].LineEnding == "\r");
    Check(Inventory.Entries[5].Operation == "-" && Inventory.Entries[5].OriginalGroupKnown);
    Check(Inventory.Entries[6].Operation == "!" && Inventory.Entries[6].Members.empty());
    Check(Inventory.Entries[7].Operation == "set" && !Inventory.Entries[7].OriginalGroupKnown);
    Check(!Inventory.Entries[8].IsGroupRow && Inventory.Entries[8].Value == "r.Unknown=2");
    Check(Inventory.Entries.back().LineEnding.empty()); Check(Inventory.Entries[1].Value.empty());
    for (const auto& Entry : Inventory.Entries)
        Check(Source.substr(Entry.Offset, Entry.End - Entry.Offset) + Entry.LineEnding == Entry.Raw + Entry.LineEnding);
    const auto Refuse = [&](const std::string& Text, const std::vector<FTextureGroupRecord>& Enum,
        const std::vector<std::string>& Selection, const FTextureProfileLimits& Limits, const char* Code,
        ETextureGroupContract Version = ETextureGroupContract::Original111_64) {
        FTextureProfileInventory Before; Before.SourceBytes = 999; Before.Groups = {{"Before", -1}};
        FReadError Why;
        Check(!ReadTextureProfileInventory(Text, Enum, Version, Selection, Before, Why, Limits));
        Check(Why.Code == Code); Check(Before.SourceBytes == 999 && Before.Groups[0].Name == "Before");
    };
    const FTextureProfileLimits Limits;
    Refuse(Source, {}, Sections, Limits, "texture-group-contract-unqualified");
    Refuse(Source, Groups, Sections, Limits, "texture-group-contract-unqualified", static_cast<ETextureGroupContract>(99));
    for (const std::string Name : {"", "WORLD", "TEXTUREGROUP_", "TEXTUREGROUP_x.y", "TEXTUREGROUP_World", "TEXTUREGROUP_WORLD"})
    { auto Bad = Groups; Bad[1].Name = Name; Refuse(Source, Bad, Sections, Limits, "texture-group-name-or-duplicate"); }
    for (const std::int64_t Value : std::initializer_list<std::int64_t>{-1, 65, 0, INT64_MAX, INT64_MIN})
    { auto Bad = Groups; Bad[1].Value = Value; Refuse(Source, Bad, Sections, Limits, "texture-group-value-or-duplicate"); }
    { auto Bad = Groups; Bad.back().Name = "TEXTUREGROUP_Last"; Refuse(Source, Bad, Sections, Limits, "texture-group-maximum-sentinel"); }
    { auto Bad = Groups; std::swap(Bad[63].Value, Bad[64].Value); Refuse(Source, Bad, Sections, Limits, "texture-group-maximum-sentinel"); }
    { auto Bad = Groups; std::swap(Bad[63], Bad[64]); Refuse(Source, Bad, Sections, Limits, "texture-group-maximum-sentinel"); }
    Refuse(Source, Groups, {}, Limits, "texture-profile-selection-limit");
    for (const auto& Selection : {std::vector<std::string>{""}, {"A\nB"}, {"[A]"}, {"A", "A"}})
        Refuse(Source, Groups, Selection, Limits, "texture-profile-section-selection");
    for (const std::string& Bad : {std::string("\0", 1), std::string("\xff", 1), std::string("\xc0\x80", 2),
        std::string("\xe0\x80\x80", 3), std::string("\xed\xa0\x80", 3), std::string("\xf4\x90\x80\x80", 4),
        std::string("\xc2", 1), std::string("\xe1\x80", 2), std::string("\xf0\x80\x80", 3), std::string("\xc2x", 2)})
        Refuse(Bad, Groups, Sections, Limits, Bad[0] == 0 ? "texture-profile-embedded-nul" : "texture-profile-invalid-utf8");
    for (const auto& Pair : {std::make_pair("[Windows DeviceProfile", "texture-profile-section-boundary"),
        std::make_pair("[Windows DeviceProfile]extra", "texture-profile-section-boundary"),
        std::make_pair("[]", "texture-profile-section-name"),
        std::make_pair("[Windows DeviceProfile]\nbad", "texture-profile-assignment"),
        std::make_pair("[Windows DeviceProfile]\n+=1", "texture-profile-key"),
        std::make_pair("[Windows DeviceProfile]\n=1", "texture-profile-key")})
        Refuse(Pair.first, Groups, Sections, Limits, Pair.second);
    const auto TupleRefuse = [&](const std::string& Tuple, const char* Code) {
        Refuse("[Windows DeviceProfile]\n+TextureLODGroups=" + Tuple, Groups, Sections, Limits, Code);
    };
    for (const std::string Tuple : {"", "x", "(Group=TEXTUREGROUP_World", "Group=TEXTUREGROUP_World)"})
        TupleRefuse(Tuple, "texture-profile-tuple-boundary");
    for (const std::string Tuple : {"(Group=TEXTUREGROUP_World,Other=\"open)", "(Group=TEXTUREGROUP_World,Other=\"x\\)", "(Group=TEXTUREGROUP_World,Other=(X=1)"})
        TupleRefuse(Tuple, "texture-profile-tuple-unbalanced");
    TupleRefuse("(Group=TEXTUREGROUP_World),Other=1)", "texture-profile-tuple-depth");
    TupleRefuse("(Other=1)", "texture-profile-missing-group"); TupleRefuse("(Group=)", "texture-profile-missing-group");
    TupleRefuse("(Group=TEXTUREGROUP_World,group=TEXTUREGROUP_Character)", "texture-profile-duplicate-group-member");
    TupleRefuse("(Group=TEXTUREGROUP_World,Trailing)", "texture-profile-member-assignment");
    TupleRefuse("(Group=TEXTUREGROUP_World,bad.key=1)", "texture-profile-member-key");
    TupleRefuse("(Group=TEXTUREGROUP_World,=1)", "texture-profile-member-key");
    { auto Small = Limits; Small.SourceBytes = 1; Refuse(Source, Groups, Sections, Small, "texture-profile-source-limit"); }
    { auto Small = Limits; Small.CopiedBytes = 1; Refuse(Source, Groups, Sections, Small, "texture-profile-copy-limit"); }
    { auto Small = Limits; Small.NameBytes = 1; Refuse(Source, Groups, Sections, Small, "texture-group-name-or-duplicate"); }
    { auto Small = Limits; Small.SelectedSections = 1; Refuse(Source, Groups, Sections, Small, "texture-profile-selection-limit"); }
    { auto Small = Limits; Small.Lines = 2; Refuse(Source, Groups, Sections, Small, "texture-profile-line-limit"); }
    { auto Small = Limits; Small.Entries = 1; Refuse(Source, Groups, Sections, Small, "texture-profile-entry-limit"); }
    { auto Small = Limits; Small.Members = 1; Refuse(Source, Groups, Sections, Small, "texture-profile-member-limit"); }
    { auto Small = Limits; Small.Depth = 0; Refuse(Source, Groups, Sections, Small, "texture-profile-depth-limit"); }
    for (const std::string Text : {"", " \t\r\n", "; comment\n# comment\n", "[Unselected]\nignored assignmentless", "\xc2\xa3\xe2\x82\xac\xf0\x90\x80\x80"})
    { FTextureProfileInventory Empty; Check(ReadTextureProfileInventory(Text, Groups, Contract, Sections, Empty, Error)); Check(Empty.Entries.empty()); }
    Check(ReadTextureProfileInventory("[Windows DeviceProfile]\ntexturelodgroups=(group=TEXTUREGROUP_WORLD)", Groups, Contract, Sections, Inventory, Error));
    Check(Inventory.Entries[0].OriginalGroupKnown && Inventory.Entries[0].Group == "TEXTUREGROUP_WORLD");
    for (int Stop = 1; Stop <= 100; ++Stop)
    {
        int Steps = 0; FTextureProfileInventory Before; Before.SourceBytes = 999;
        const bool Result = ReadTextureProfileInventory(Source, Groups, Contract, Sections, Before, Error, Limits, [&] { return ++Steps == Stop; });
        if (!Result) { Check(Error.Code == "texture-profile-cancelled"); Check(Before.SourceBytes == 999); }
        else Check(Steps < Stop && Before.SourceBytes == Source.size());
    }
    // Cancellation also remains responsive in long UTF8 scans whose multibyte
    // boundaries never coincide with a 4096-byte modulo checkpoint.
    std::string Long = "\xef\xbb\xbf"; for (int I = 0; I < 10000; ++I) Long += "\xf0\x90\x80\x80";
    int Steps = 0; FTextureProfileInventory Before; Before.SourceBytes = 999;
    Check(!ReadTextureProfileInventory(Long, Groups, Contract, Sections, Before, Error, Limits, [&] { return ++Steps == 70; }));
    Check(Error.Code == "texture-profile-cancelled" && Before.SourceBytes == 999);
}
