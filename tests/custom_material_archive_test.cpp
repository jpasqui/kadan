#include "../core/custom_material_archive.h"
#include <functional>
#include <iostream>
using namespace P4MapKit::CustomMaterial;
using namespace P4MapKit::CustomMaterial::NativeBackend;
static unsigned checks = 0;
static void Check(bool ok) { ++checks; if (!ok) throw std::runtime_error("archive test failed"); }
static void Refuses(const std::function<void()>& action) {
    bool rejected = false; try { action(); } catch (const std::runtime_error&) { rejected = true; }
    Check(rejected);
}
int main() {
    const Bytes a = ByteText("creator vertex program"), b = ByteText("creator pixel program");
    const auto h1 = Sha1(ByteText("creator map one")), h2 = Sha1(ByteText("creator map two"));
    std::vector<OwnedArchiveMap> maps = {{h1, {{0,a}, {3,b}}}, {h2, {{3,b}, {0,a}, {3,b}}}};
    const auto bytes = WriteOwnedShaderArchive(maps);
    const ShaderArchive archive(bytes);
    const auto first = archive.Map(h1), second = archive.Map(h2);
    Check(first.size() == 2 && second.size() == 3);
    Check(first[0] == second[1] && first[1] == second[0] && second[0] == second[2]);
    Check(archive.Frequency(first[0]) == 0 && archive.Frequency(first[1]) == 3);
    Check(archive.Payload(first[0]) == a && archive.Payload(first[1]) == b);
    Check(archive.OriginalIdentity(first[0]) == Sha1(a));
    Check(WriteOwnedShaderArchive(maps) == bytes);
    Refuses([&] { archive.Map(Sha1(ByteText("missing map"))); });
    Refuses([&] { WriteOwnedShaderArchive({}); });
    Refuses([&] { auto bad=maps; bad[1].hash=h1; WriteOwnedShaderArchive(bad); });
    Refuses([&] { auto bad=maps; bad[0].hash.pop_back(); WriteOwnedShaderArchive(bad); });
    Refuses([&] { auto bad=maps; bad[0].shaders.clear(); WriteOwnedShaderArchive(bad); });
    Refuses([&] { auto bad=maps; bad[0].shaders[0].payload.clear(); WriteOwnedShaderArchive(bad); });
    Refuses([&] { auto bad=maps; bad[0].shaders[0].frequency=6; WriteOwnedShaderArchive(bad); });
    Refuses([&] { auto bad=maps; bad[0].shaders[0].payload.resize((1u<<20)+1); WriteOwnedShaderArchive(bad); });
    // The consumer caps both index and preload arrays at 65536. Repeated
    // payloads still count as map positions, even though their bytes deduplicate.
    Refuses([&] {
        std::vector<OwnedArchiveMap> crowded;
        for (unsigned i=0;i<17;++i)
            crowded.push_back({Sha1(ByteText("collection"+std::to_string(i))),std::vector<OwnedArchiveShader>(4096,{3,b})});
        WriteOwnedShaderArchive(crowded);
    });
    // Equal bytes with different engine stages retain separate index entries.
    const auto separate = WriteOwnedShaderArchive({{h1, {{0,a}, {3,a}}}});
    const ShaderArchive stages(separate); const auto entries = stages.Map(h1);
    Check(entries[0] != entries[1]);
    SetMaterial material; material.material="/Game/Mods/Flowers/M_Red.M_Red";
    material.game_material="/Game/Carrot4/Donor"; material.map_id_hash=h1;
    material.map_shaders=8; material.shaders={{2,b}, {7,a}};
    const auto bundle = BuildOwnedShaderArchiveBundle({material}, "Kadan_Flowers");
    const ShaderArchive creator(bundle.archive);
    const auto& row=bundle.manifest.At("materials").array.at(0);
    const auto owned=creator.Map(Unhex(row.At("archive_map").Text()));
    Check(owned.size()==2 && creator.Frequency(owned[0])==3 && creator.Payload(owned[0])==b);
    Check(row.At("shaders").array.at(0).At("place").Numeric()==2);
    Check(row.At("shaders").array.at(1).At("hash").Text()==Hex(Sha1(a)));
    Check(row.At("map_id").Text()==Hex(h1) && row.At("game_material").Text()==material.game_material);
    Check(!row.Has("payload") && !row.Has("template_file") && !row.Has("template_sha256"));
    auto secondMaterial=material; secondMaterial.material="/Game/Mods/Flowers/M_Blue.M_Blue";
    const auto duplicate=BuildOwnedShaderArchiveBundle({material,secondMaterial},"Kadan_Flowers");
    Check(duplicate.archive==bundle.archive && duplicate.manifest.At("materials").array.size()==2);
    Refuses([&]{ BuildOwnedShaderArchiveBundle({material},"../Flowers"); });
    std::cout << checks << " archive checks passed\n";
}
