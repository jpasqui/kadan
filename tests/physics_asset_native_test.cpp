#include "core/physics_asset_native.h"
#include "core/animation_asset_native.h"
#include <cassert>
#include <iostream>
#include <fstream>
#include <iterator>
using namespace P4MapKit;
static void Word(std::vector<std::uint8_t>& B, std::uint32_t V)
{ for (int I = 0; I < 4; ++I) B.push_back(std::uint8_t(V >> (8 * I))); }
static std::vector<std::uint8_t> Table()
{ std::vector<std::uint8_t> B; for (auto V : {0u, 2u, 0u, 2u, 0u, 1u, 3u, 1u}) Word(B, V); return B; }
static std::vector<std::uint8_t> InlineBody(bool Wide=false)
{
    std::vector<std::uint8_t> B;
    for (auto V : {0u,1u,2u,3u,4u,1u,1u,1u,0u,0u}) Word(B,V);
    Word(B,0x48u|(Wide?BulkSize64:0));Word(B,4);if(Wide)Word(B,0);
    Word(B,4);if(Wide)Word(B,0);Word(B,100+B.size()+8);Word(B,0);
    B.insert(B.end(),{0x43,0x56,0x58,0x4d});return B; // Opaque cooking bytes; no geometry qualification.
}
static void InlineBulkChecks()
{
    const std::vector<std::string> Names={"PhysXPC"};auto B=InlineBody();
    FByteBulkContext Context;Context.HeaderSize=100;Context.FileSizes[0]=B.size();
    const auto Layout=ReadInlinePhysicsBodyBulk(B,Names,0,B.size(),Context);
    assert(Layout.Error.Code.empty()&&Layout.Payloads.size()==1&&Layout.Payloads[0].Offset==60);
    assert(ReadPrimitivePhysicsBodyNative(B,0,B.size()).Error.Code=="physics-body-cooked-shapes-not-decoded");
    for(std::size_t N=0;N<B.size();++N)assert(!ReadInlinePhysicsBodyBulk(B,Names,0,N,Context).Error.Code.empty());
    for(const auto At:{24u,28u,40u,52u})
    {auto Bad=B;Bad[At]^=1;assert(!ReadInlinePhysicsBodyBulk(Bad,Names,0,Bad.size(),Context).Error.Code.empty());}
    assert(!ReadInlinePhysicsBodyBulk(B,{"Unknown"},0,B.size(),Context).Error.Code.empty());
    auto Extra=B;Extra.push_back(0);Context.FileSizes[0]=Extra.size();
    assert(!ReadInlinePhysicsBodyBulk(Extra,Names,0,Extra.size(),Context).Error.Code.empty());
    std::vector<std::uint8_t> After(17,0xa5);After.insert(After.end(),B.begin(),B.end());const auto Unpatched=After;
    FReadError Error;
    assert(RelocateInlinePhysicsBodyBulk(B,Names,0,B.size(),100,After,Names,17,After.size(),200,Error));
    Context.HeaderSize=200;Context.FileSizes[0]=After.size();
    const auto ReadBack=ReadInlinePhysicsBodyBulk(After,Names,17,After.size(),Context);
    assert(ReadBack.Error.Code.empty()&&ReadBack.Payloads[0].Header.Offset==277);
    for(std::size_t I=0;I<After.size();++I)if(I<69||I>=77)assert(After[I]==Unpatched[I]);
    After=Unpatched;
    assert(!RelocateInlinePhysicsBodyBulk(B,Names,0,B.size(),100,After,{"Unknown"},17,After.size(),200,Error));
    assert(After==Unpatched); // Readback failure rolls back all offset edits.
    assert(!RelocateInlinePhysicsBodyBulk(B,Names,0,B.size(),100,After,{"PhysXGeneric"},17,After.size(),200,Error));
    assert(After==Unpatched); // A different supported format is still a semantic change.
    After.back()^=1;const auto Changed=After;
    assert(!RelocateInlinePhysicsBodyBulk(B,Names,0,B.size(),100,After,Names,17,After.size(),200,Error)&&After==Changed);
    auto Wide=InlineBody(true);std::vector<std::uint8_t> WideAfter(17,0);WideAfter.insert(WideAfter.end(),Wide.begin(),Wide.end());
    assert(RelocateInlinePhysicsBodyBulk(Wide,Names,0,Wide.size(),100,WideAfter,Names,17,WideAfter.size(),200,Error));
    Context.FileSizes[0]=WideAfter.size();
    const auto WideBack=ReadInlinePhysicsBodyBulk(WideAfter,Names,17,WideAfter.size(),Context);
    assert(WideBack.Error.Code.empty()&&WideBack.Payloads[0].Header.Offset==285);
    After=Unpatched;
    assert(!RelocateInlinePhysicsBodyBulk(B,Names,0,B.size(),100,After,Names,17,After.size(),UINT64_MAX,Error)&&After==Unpatched);
    // A header that grew leaves the payload saying its old place: placing gives it header size plus where it is.
    auto Stale=B;bool Changed2=false;Context.HeaderSize=300;Context.FileSizes[0]=Stale.size();
    assert(!ReadInlinePhysicsBodyBulk(Stale,Names,0,Stale.size(),Context).Error.Code.empty());
    assert(ReadInlinePhysicsBodyBulk(Stale,Names,0,Stale.size(),Context,false).Error.Code.empty());
    assert(PlaceInlinePhysicsBodyBulk(Stale,Names,0,Stale.size(),300,Changed2,Error)&&Changed2);
    const auto Placed=ReadInlinePhysicsBodyBulk(Stale,Names,0,Stale.size(),Context);
    assert(Placed.Error.Code.empty()&&Placed.Payloads[0].Header.Offset==360);
    for(std::size_t I=0;I<B.size();++I)if(I<52||I>=60)assert(Stale[I]==B[I]);
    assert(PlaceInlinePhysicsBodyBulk(Stale,Names,0,Stale.size(),300,Changed2,Error)&&!Changed2);
    auto Broken=B;Broken[24]^=1;
    assert(!PlaceInlinePhysicsBodyBulk(Broken,Names,0,Broken.size(),300,Changed2,Error));
}
int main(int Argc, char** Argv)
{
    InlineBulkChecks();
    auto B = Table(); auto R = ReadPhysicsAssetNative(B, 0, B.size());
    assert(R.Error.Code.empty() && R.Disabled.size() == 2);
    assert(R.Disabled[0].First == 0 && R.Disabled[0].Second == 2 && !R.Disabled[0].Value);
    assert(R.Disabled[1].First == 1 && R.Disabled[1].Second == 3 && R.Disabled[1].Value);
    for (std::size_t N = 0; N < B.size(); ++N)
    { R = ReadPhysicsAssetNative(B, 0, N); assert(!R.Error.Code.empty() && R.Disabled.empty()); }
    B = Table(); B.push_back(0); assert(!ReadPhysicsAssetNative(B, 0, B.size()).Error.Code.empty());
    B = Table(); B[24] = 0; assert(!ReadPhysicsAssetNative(B, 0, B.size()).Error.Code.empty());
    B = Table(); B[20] = 0; B[24] = 2;
    assert(ReadPhysicsAssetNative(B, 0, B.size()).Error.Code == "physics-collision-pair-duplicate");
    B = Table(); B[16] = 2; assert(!ReadPhysicsAssetNative(B, 0, B.size()).Error.Code.empty());
    B = Table(); B[8] = 2; assert(!ReadPhysicsAssetNative(B, 0, B.size()).Error.Code.empty());
    B.clear(); Word(B, 0); Word(B, 0); R = ReadPhysicsAssetNative(B, 0, B.size());
    assert(R.Error.Code.empty() && R.Disabled.empty());
    B.clear(); Word(B, 1); for (int I = 0; I < 16; ++I) B.push_back(std::uint8_t(I)); Word(B, 0);
    assert(ReadPhysicsAssetNative(B, 0, B.size()).Error.Code.empty());
    B.clear(); for (auto V : {0u, 1u, 2u, 3u, 4u, 1u, 0u, 0u}) Word(B, V);
    auto Body = ReadPrimitivePhysicsBodyNative(B, 0, B.size());
    assert(Body.Error.Code.empty() && Body.Guid[0] == 1 && Body.Guid[3] == 4);
    for (std::size_t N = 0; N < B.size(); ++N) assert(!ReadPrimitivePhysicsBodyNative(B, 0, N).Error.Code.empty());
    auto Changed = B; Changed[24] = 1;
    assert(ReadPrimitivePhysicsBodyNative(Changed, 0, Changed.size()).Error.Code == "physics-body-cooked-shapes-not-decoded");
    Changed = B; Changed[28] = 1; assert(!ReadPrimitivePhysicsBodyNative(Changed, 0, Changed.size()).Error.Code.empty());
    Changed = B; Changed[20] = 0; assert(!ReadPrimitivePhysicsBodyNative(Changed, 0, Changed.size()).Error.Code.empty());
    Changed = B; Changed.push_back(0); assert(!ReadPrimitivePhysicsBodyNative(Changed, 0, Changed.size()).Error.Code.empty());
    B.resize(20); auto Montage = ReadAnimationAssetNative(B, 0, B.size());
    assert(Montage.Error.Code.empty() && Montage.SkeletonGuid[0] == 1 && Montage.SkeletonGuid[3] == 4);
    for (std::size_t N = 0; N < B.size(); ++N) assert(!ReadAnimationAssetNative(B, 0, N).Error.Code.empty());
    B.push_back(0); assert(!ReadAnimationAssetNative(B, 0, B.size()).Error.Code.empty());
    if (Argc == 2)
    {
        std::ifstream File(Argv[1], std::ios::binary);
        std::vector<std::uint8_t> Corpus((std::istreambuf_iterator<char>(File)), std::istreambuf_iterator<char>());
        assert(File.good() || File.eof());
        FByteReader Input(Corpus); std::size_t Count = 0; assert(Input.Count(Count, 10000));
        std::size_t Assets = 0, Bodies = 0, Montages = 0;
        for (std::size_t I = 0; I < Count; ++I)
        {
            std::uint32_t Kind = 0; std::size_t Size = 0;
            assert(Input.U32(Kind) && Input.Count(Size, 4096));
            const std::size_t Start = Input.Tell(); assert(Input.Skip(Size));
            if (Kind == 0) { assert(ReadPhysicsAssetNative(Corpus, Start, Start + Size).Error.Code.empty()); ++Assets; }
            else if (Kind == 1) { assert(ReadPrimitivePhysicsBodyNative(Corpus, Start, Start + Size).Error.Code.empty()); ++Bodies; }
            else { assert(Kind == 2 && ReadAnimationAssetNative(Corpus, Start, Start + Size).Error.Code.empty()); ++Montages; }
        }
        assert(Input.Tell() == Corpus.size());
        std::cout << Assets << " physics assets, " << Bodies << " primitive bodies, " << Montages << " montages decoded completely\n";
    }
    std::cout << "Physics collision table validation passed\n";
}
