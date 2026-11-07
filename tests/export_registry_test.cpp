#include "../core/export_registry.h"
#include <cstdlib>
#include <iostream>
static void Check(bool V) { if(!V) { std::cerr<<"registry census check failed\n"; std::exit(1); } }
int main()
{
    using P4MapKit::Export::RegistryPackageLines;
    const std::string Input="Registry {\n  asset: \"ignored\",\n    package_name: \"/Game/One\",\r\n  package_name: \"/Script/Engine\",\n package_name: \"/Game/Two\"";
    for(std::size_t Width=1;Width<=Input.size();++Width)
    {
        RegistryPackageLines R;
        for(std::size_t At=0;At<Input.size();At+=Width)
            Check(R.Feed(reinterpret_cast<const std::uint8_t*>(Input.data()+At),std::min(Width,Input.size()-At)));
        Check(R.Finish()); Check(R.Names==std::vector<std::string>({"/Game/One","/Script/Engine","/Game/Two"}));
    }
    RegistryPackageLines Large;
    const std::string Metadata="tag: "+std::string(1024*1024,'x');
    for(int I=0;I<80;++I) Check(Large.Feed(reinterpret_cast<const std::uint8_t*>(Metadata.data()),Metadata.size()));
    const std::string Tail="\npackage_name: \"/Game/AfterLargeMetadata\",\n";
    Check(Large.Feed(reinterpret_cast<const std::uint8_t*>(Tail.data()),Tail.size())); Check(Large.Finish());
    Check(Large.Names==std::vector<std::string>({"/Game/AfterLargeMetadata"}));
    for(const std::string& Bad:std::vector<std::string>{"package_name: \"broken","package_name: \"/Game/a\\nb\",\n",
        "package_name: \"/Game/A\"more\n","package_name: \"/Game/A\",,\n",
        std::string("package_name: \"/Game/")+char(0xff)+"\"\n"})
    {
        RegistryPackageLines R;
        const bool Fed=R.Feed(reinterpret_cast<const std::uint8_t*>(Bad.data()),Bad.size()); Check(!Fed || !R.Finish());
    }
    std::cout<<"export_registry_test PASS\n";
}
