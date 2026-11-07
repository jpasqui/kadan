#include "native_custom_backend_archive.h"
#include <cassert>
using namespace P4MapKit::CustomMaterial;
using namespace P4MapKit::CustomMaterial::NativeBackend;
template<class F> void Refuses(F f){bool refused=false;try{f();}catch(const std::exception&){refused=true;}assert(refused);}
int main(){
    Bytes payload={'h','e','l','l','o'};Bytes map(20,1),hash=Sha1(payload),archive;U32(archive,2);
    U32(archive,1);Append(archive,map);U32(archive,1);Append(archive,hash);
    U32(archive,1);U32(archive,0);U32(archive,1);U32(archive,0);U32(archive,1);
    U32(archive,1);U32(archive,0);U32(archive,0);U32(archive,5);U32(archive,5);archive.push_back(3);
    U32(archive,1);for(unsigned i=0;i<16;++i)archive.push_back(0);
    U32(archive,1);U32(archive,0);Append(archive,payload);
    ShaderArchive read(archive);assert(read.Map(map)==std::vector<unsigned>{0});assert(read.Frequency(0)==3);assert(read.Payload(0)==payload);
    Refuses([&]{read.Map(Bytes(20,2));});Refuses([&]{read.Payload(1);});
    assert(read.OriginalIdentity(0)==hash);
    Bytes changed=archive;
    changed.resize(changed.size()-2);ShaderArchive truncated(changed);Refuses([&]{truncated.Payload(0);});
    assert(DecodeBlock(Bytes{0x50,'h','e','l','l','o'},5)==payload);
    assert(DecodeBlock(Bytes{0x11,'a',1,0},6)==Bytes(6,'a'));
    Refuses([]{DecodeBlock(Bytes{0x11,'a',0,0},6);});Refuses([]{DecodeBlock(Bytes{0xf0,255},10);});
    Refuses([]{DecodeBlock(Bytes{0x50,'h'},5);});Refuses([]{DecodeBlock(Bytes{0x11,'a',2,0},6);});
}
