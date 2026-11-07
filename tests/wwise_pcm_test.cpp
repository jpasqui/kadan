#include "core/wwise_pcm.h"
#include "core/wwise_inventory.h"
#include <cassert>
#include <fstream>
#include <iostream>
using namespace P4MapKit::WwisePCM;
template <class F> void Refuses(F Work)
{ bool Bad = false; try { Work(); } catch (const std::runtime_error&) { Bad = true; } assert(Bad); }
std::vector<std::uint32_t> ActualEventIds(const Bytes& Data)
{
    Inventory Checked; ReadBankInventory(Data, false, Checked);
    std::vector<std::uint32_t> Out;
    for (std::size_t At = 0; At < Data.size(); At += 8 + ReadU32(Data, At + 4))
        if (std::memcmp(Data.data() + At, "HIRC", 4) == 0)
        {
            const auto Count = ReadU32(Data, At + 8); std::size_t Cursor = At + 12;
            for (std::uint32_t I = 0; I < Count; ++I)
            {
                if (Data[Cursor] == 4) Out.push_back(ReadU32(Data, Cursor + 5));
                Cursor += 5 + ReadU32(Data, Cursor + 1);
            }
        }
    return Out;
}
int main(int Argc, char** Argv)
{
    Sample Mono; Mono.Label = "mono"; Mono.Rate = 8000; Mono.Channels = 1;
    Mono.PCM = {0,0,0xff,0x7f,0,0x80,1,0};
    Sample Stereo = Mono; Stereo.Label = "stereo"; Stereo.Rate = 48000; Stereo.Channels = 2;
    const auto A = Build({Mono, Stereo}, "test.creator", 12345, Id("spatial"));
    assert(A.Data == Build({Mono, Stereo}, "test.creator", 12345, Id("spatial")).Data);
    assert(A.Events.size() == 2 && A.Events[0].MediaId == Id("test.creator.mono.media"));
    assert(Media(Mono).size() == 72 && Media(Stereo).size() == 72);
    assert(Id("SPATIAL") == Id("spatial"));
    const std::vector<std::string> Names = {"Ak_Creator_Mono", "Ak_Creator_Stereo"};
    const auto Named = Build({Mono, Stereo}, "test.creator", 12345, Id("spatial"), {}, true, Names);
    assert(Named.Events[0].Name == Names[0] && Named.Events[0].EventId == Id(Names[0]));
    assert(Named.Events[1].Name == Names[1] && Named.Events[1].EventId == Id(Names[1]));
    assert(ActualEventIds(Named.Data) == std::vector<std::uint32_t>({Id(Names[0]), Id(Names[1])}));
    assert(Named.Events[0].MediaId == A.Events[0].MediaId && Named.Events[0].SoundId == A.Events[0].SoundId);
    assert(A.Events[0].Name == "test.creator.mono");
    Refuses([&] { Build({Mono, Stereo}, "test.creator", 12345, 1, {}, true, {Names[0]}); });
    Refuses([&] { Build({Mono, Stereo}, "test.creator", 12345, 1, {}, true, {Names[0], "ak_creator_mono"}); });
    Refuses([&] { Build({Mono}, "test.creator", 12345, 1, {Id(Names[0])}, true, {Names[0]}); });
    for (const std::string Invalid : {"", "None", "nOnE", "a.b", "a/b", "a:b", "a-b", "a b", "a\"b", "a\\b"})
        Refuses([&] { Build({Mono}, "test.creator", 12345, 1, {}, true, {Invalid}); });
    assert(!EventAssetName(std::string(97, 'a')) && !EventAssetName(std::string(1, char(0xff))));
    Refuses([&] { Build({Mono, Mono}, "test.creator", 12345, 1); });
    Refuses([&] { Build({Mono}, "test.creator", 12345, 1, {A.Id}); });
    Refuses([&] { Build({Mono}, "test.creator", 12345, 1, {A.Events[0].MediaId}); });
    Refuses([&] { Build({}, "test.creator", 12345, 1); });
    Refuses([&] { Build({Mono}, "Test.creator", 12345, 1); });
    Sample Bad = Mono; Bad.Channels = 0; Refuses([&] { Media(Bad); });
    Bad = Mono; Bad.Rate = 7999; Refuses([&] { Media(Bad); });
    Bad = Mono; Bad.PCM.pop_back(); Refuses([&] { Media(Bad); });
    Bad = Mono; Bad.PCM.resize(8000 * 2 * 30 + 2); Refuses([&] { Media(Bad); });
    Bad = Mono; Bad.Label = "invalid.wav"; Refuses([&] { Build({Bad}, "test.creator", 12345, 1); });
    Refuses([&] { Build(std::vector<Sample>(65, Mono), "test.creator", 12345, 1); });
    if (Argc == 2)
    {
        for (bool Spatial : {true, false})
        {
            auto Bank = Build({Mono, Stereo}, "test.creator", 12345, Id("spatial"), {}, Spatial);
            const std::string Path = std::string(Argv[1]) + (Spatial ? "/spatial.bnk" : "/flat.bnk");
            std::ofstream F(Path, std::ios::binary); F.write(reinterpret_cast<const char*>(Bank.Data.data()), Bank.Data.size()); assert(F.good());
        }
    }
    std::cout << "Wwise PCM authoring validation passed\n";
}
