#include "../core/wwise_inventory.h"
#include <cassert>
#include <iostream>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
using namespace P4MapKit::WwisePCM;
template<class F> void Refuses(F Work) { bool Failed = false; try { Work(); } catch (const std::runtime_error&) { Failed = true; } assert(Failed); }
int main(int argc, char**)
{
    Sample S; S.Label = "tone"; S.Rate = 8000; S.Channels = 1; S.PCM = {0,0,1,0};
    const auto Bank = Build({S}, "test.inventory", 4956, Id("spatial"));
    Inventory Read; ReadBankInventory(Bank.Data, true, Read);
    assert(Read.HasInit && Read.Project == 4956 && Read.Banks == 1 && Read.Ids.size() == 5);
    const auto Before = Read;
    for (std::size_t I = 0; I < Bank.Data.size(); ++I)
    {
        Bytes Bad(Bank.Data.begin(), Bank.Data.begin() + I);
        Refuses([&] { ReadBankInventory(Bad, true, Read); });
        assert(Read.Ids == Before.Ids && Read.Banks == Before.Banks);
    }
    Refuses([&] { ReadBankInventory(Build({S}, "other.inventory", 1, 1).Data, true, Read); });
    if (argc > 1)
    {
#ifdef _WIN32
        _setmode(_fileno(stdin), _O_BINARY);
#endif
        Inventory Stock;
        for (;;)
        {
            unsigned char Header[5]; std::cin.read(reinterpret_cast<char*>(Header), 5);
            if (std::cin.gcount() == 0) break;
            if (std::cin.gcount() != 5) return 2;
            const std::uint32_t Size = std::uint32_t(Header[0]) | (std::uint32_t(Header[1]) << 8) |
                (std::uint32_t(Header[2]) << 16) | (std::uint32_t(Header[3]) << 24);
            if (Size > 64 * 1024 * 1024) return 3;
            Bytes Data(Size); std::cin.read(reinterpret_cast<char*>(Data.data()), Size);
            if (std::cin.gcount() != Size) return 4;
            try { ReadBankInventory(Data, Header[4] != 0, Stock); }
            catch (const std::exception& Error) { std::cerr << "Bank " << Stock.Banks << " size " << Size << ": " << Error.what() << ", header " << ReadU32(Data, 4) << " version " << ReadU32(Data, 8) << '\n'; return 5; }
        }
        assert(Stock.HasInit && Stock.Ids.count(Id("spatial")));
        std::cout << Stock.Banks << ' ' << Stock.Project << '\n';
        for (auto Id : Stock.Ids) std::cout << Id << '\n';
    }
    else std::cout << "Wwise inventory framing PASS\n";
}
