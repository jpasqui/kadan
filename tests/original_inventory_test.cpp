#include "../core/original_inventory.h"
#include <cassert>
int main()
{
    using namespace P4MapKit::OriginalInventory;
    std::string Error;
    assert(RequireSupported(RegistrySha256(),ContainerHeaderSha256(),UtocSha256(),Error));
    assert(Error.empty());
    for(unsigned I=0;I<3;++I)
    {
        std::string Values[]={RegistrySha256(),ContainerHeaderSha256(),UtocSha256()};
        Values[I][0]=Values[I][0]=='0'?'1':'0';
        assert(!RequireSupported(Values[0],Values[1],Values[2],Error));
        assert(Error=="original-game-inventory-build-unqualified");
    }
    assert(!RequireSupported("","","",Error));
    assert(!RequireSupported(RegistrySha256(),ContainerHeaderSha256(),"unknown-build",Error));
}
