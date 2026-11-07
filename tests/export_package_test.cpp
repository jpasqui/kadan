#include "../core/export_package.h"
#include <fstream>
#include <iostream>
#include <cstdlib>
static void Check(bool Good) { if (!Good) { std::cerr << "export package check failed\n"; std::exit(1); } }
static void Put(std::vector<std::uint8_t>& B,std::uint64_t V,std::size_t W=4)
{ for (std::size_t I=0;I<W;++I) B.push_back(std::uint8_t(V>>(I*8))); }
static void Patch(std::vector<std::uint8_t>& B,std::size_t A,std::uint64_t V,std::size_t W=4)
{ P4MapKit::Export::PackageRewriteDetail::Put(B,A,V,W); }
static void String(std::vector<std::uint8_t>& B,const std::string& V)
{ P4MapKit::Export::PackageRewriteDetail::String(B,V); }
struct Fixture { std::vector<std::uint8_t> Header,Body; std::size_t Flags=0,BulkAt=0,ExportAt=0,NamesAt=0,PreloadAt=0; };
static Fixture Make(bool Wide=false,bool Custom=false)
{
    Fixture F; auto& H=F.Header;
    Put(H,0x9e2a83c1); Put(H,UINT32_MAX-6); for(int I=0;I<3;++I) Put(H,0);
    Put(H,Custom?1:0); if(Custom) for(int I=0;I<5;++I) Put(H,100+I);
    const auto SizeAt=H.size(); Put(H,0); String(H,"/Game/Creator"); F.Flags=H.size();
    Put(H,0x80000000); for(int I=0;I<17;++I) Put(H,0);
    Put(H,1); Put(H,1); Put(H,7); // generation record
    for(int I=0;I<2;++I) { Put(H,4,2); Put(H,26,2); Put(H,0,2); Put(H,0); String(H,""); }
    Put(H,0); Put(H,0); Put(H,123); Put(H,0); // compression/source/additional
    Put(H,0); F.BulkAt=H.size(); Put(H,0,8); Put(H,0); Put(H,0);
    F.PreloadAt=H.size(); Put(H,1); Put(H,0);
    F.NamesAt=H.size();
    const std::vector<std::string> Names{"/Script/Engine","Package","Class","StaticMesh","Mesh","None","PhysXPC"};
    for(const auto& Name:Names)
    {
        if(Wide && Name=="PhysXPC") { Put(H,std::uint32_t(-8)); for(const char C:Name) Put(H,std::uint8_t(C),2); Put(H,0,2); }
        else String(H,Name);
        Put(H,123);
    }
    const auto Imports=H.size();
    for(const auto V: {0u,0u,1u,0u,0u,0u,0u,0u,0u,2u,0u,UINT32_MAX,3u,0u}) Put(H,V);
    F.ExportAt=H.size();
    Put(H,UINT32_MAX-1); for(int I=0;I<3;++I) Put(H,0); Put(H,4); Put(H,0); Put(H,1);
    Put(H,8,8); const auto Offset=H.size(); Put(H,0,8);
    for(int I=0;I<10;++I) Put(H,0); Put(H,UINT32_MAX); for(int I=0;I<4;++I) Put(H,0);
    const auto Preload=H.size(); Put(H,UINT32_MAX);
    F.Body={5,0,0,0,0,0,0,0,0xc1,0x83,0x2a,0x9e};
    Patch(H,SizeAt,H.size()); Patch(H,Offset,H.size(),8); Patch(H,F.BulkAt,H.size()+8,8);
    Patch(H,F.Flags+4,Names.size()); Patch(H,F.Flags+8,F.NamesAt); Patch(H,F.Flags+20,1);
    Patch(H,F.Flags+24,F.ExportAt); Patch(H,F.Flags+28,2); Patch(H,F.Flags+32,Imports);
    Patch(H,F.PreloadAt+4,Preload); return F;
}
static std::vector<std::uint8_t> Read(const char* Path)
{ std::ifstream F(Path,std::ios::binary); Check(bool(F)); return {std::istreambuf_iterator<char>(F),std::istreambuf_iterator<char>()}; }
static void Write(const char* Path,const std::vector<std::uint8_t>& B)
{ std::ofstream F(Path,std::ios::binary); F.write(reinterpret_cast<const char*>(B.data()),std::streamsize(B.size())); Check(bool(F)); }
int main(int Argc,char** Argv)
{
    using namespace P4MapKit; using namespace P4MapKit::Export;
    if(Argc==3) { const auto F=Make(true,true); Write(Argv[1],F.Header); Write(Argv[2],F.Body); return 0; }
    if(Argc==5)
    {
        RewrittenPackage Result; std::string Error;
        if(!RelabelPhysXCook(Read(Argv[1]),Read(Argv[2]),Result,Error)) { std::cerr<<Error<<'\n'; return 2; }
        Write(Argv[3],Result.Header); Write(Argv[4],Result.Body); return 0;
    }
    for(const bool Wide:{false,true}) for(const bool Custom:{false,true})
    {
        const auto F=Make(Wide,Custom); RewrittenPackage Result; std::string Error;
        Check(ReadPackageHeader(F.Header,F.Body.size()).Error.Code.empty());
        Check(RelabelPhysXCook(F.Header,F.Body,Result,Error)); Check(Result.Changed); Check(Result.Body==F.Body);
        const auto R=ReadPackageHeader(Result.Header,Result.Body.size()); Check(R.Error.Code.empty());
        Check(R.Names.back()=="PhysXGeneric"); Check(R.Exports[0].Offset==0); Check(R.Exports[0].Size==8);
        auto Original=Result.Header;
        Patch(Original,F.ExportAt+36,F.Header.size(),8); Patch(Original,F.BulkAt,F.Header.size()+8,8);
        Patch(Original,24+(Custom?20:0),F.Header.size()); Patch(Original,F.Flags+8,F.NamesAt);
        Original.resize(F.Header.size()); Check(Original==F.Header);
        RewrittenPackage Second; Check(RelabelPhysXCook(Result.Header,Result.Body,Second,Error));
        Check(!Second.Changed); Check(Second.Header==Result.Header); Check(Second.Body==Result.Body);
        // The table the relabel wrote ends the header: another relabel replaces it there instead of adding a third.
        RewrittenPackage Back; Check(RewriteCookedNames(Result.Header,Result.Body,{{"PhysXGeneric","PhysXPC"}},Back,Error));
        Check(Back.Changed); Check(Back.Header.size()+5==Result.Header.size());
        const auto BackRead=ReadPackageHeader(Back.Header,Back.Body.size()); Check(BackRead.Error.Code.empty());
        Check(BackRead.Names==ReadPackageHeader(F.Header,F.Body.size()).Names); Check(BackRead.Exports[0].Offset==0);
        const auto Sentinel=Result.Header;
        for(std::size_t Size=0;Size<F.Header.size();++Size)
        {
            Check(!RelabelPhysXCook({F.Header.begin(),F.Header.begin()+Size},F.Body,Result,Error)); Check(Result.Header==Sentinel);
        }
        for(std::size_t Size=0;Size<F.Body.size();++Size)
        { Check(!RelabelPhysXCook(F.Header,{F.Body.begin(),F.Body.begin()+Size},Result,Error)); Check(Result.Header==Sentinel); }
        for(const auto At:{F.Flags+8,F.Flags+24,F.Flags+36,F.Flags+44,F.Flags+48,F.Flags+52,F.PreloadAt+4})
        { auto Bad=F.Header; Patch(Bad,At,UINT32_MAX); Check(!RelabelPhysXCook(Bad,F.Body,Result,Error)); }
        auto Bad=F.Header; Patch(Bad,F.BulkAt,F.Header.size()+7,8); Check(!RelabelPhysXCook(Bad,F.Body,Result,Error));
        auto Body=F.Body; Body.back()^=1; Check(!RelabelPhysXCook(F.Header,Body,Result,Error));
        Body.push_back(0); Check(!RelabelPhysXCook(F.Header,Body,Result,Error));
        Check(!RewriteCookedNames(F.Header,F.Body,{{"PhysXPC","StaticMesh"}},Result,Error));
        Check(!RewriteCookedNames(F.Header,F.Body,{{"PhysXPC",std::string("bad\0name",8)}},Result,Error));
        Check(RewriteCookedNames(F.Header,F.Body,{},Result,Error)); Check(!Result.Changed); Check(Result.Header==F.Header);
        // Summary-table overlap and noncontiguous payloads must fail before any write.
        Bad=F.Header; Patch(Bad,F.PreloadAt+4,F.NamesAt); Check(!RelabelPhysXCook(Bad,F.Body,Result,Error));
        Bad=F.Header; Patch(Bad,F.ExportAt+36,F.Header.size()+1,8); Check(!RelabelPhysXCook(Bad,F.Body,Result,Error));
    }
    std::cout<<"export_package_test PASS\n";
}
