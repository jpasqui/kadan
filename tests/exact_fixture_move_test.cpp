#define P4_EXACT_MOVE_HOST_TEST 1
#include "../Kadan/Source/P4MapKitTests/Private/Authoring/P4ExactFixtureMove.h"
#include <bcrypt.h>
#include <winioctl.h>
#include <iostream>
#include <vector>
#include <array>
#define CHECK(X) do{if(!(X)){std::cerr<<"line "<<__LINE__<<": "<<#X<<" error="<<GetLastError()<<'\n';return 1;}}while(false)
namespace M=P4ExactFixtureMove;
static bool Hash(const std::wstring& Path,std::uint64_t Limit,std::string& Out)
{
    M::FHandle File(CreateFileW(Path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
    if(File.Value==INVALID_HANDLE_VALUE)return false;
    BCRYPT_ALG_HANDLE Algorithm=nullptr;BCRYPT_HASH_HANDLE Digest=nullptr;
    if(BCryptOpenAlgorithmProvider(&Algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)return false;
    struct FCleanup{BCRYPT_ALG_HANDLE A;BCRYPT_HASH_HANDLE& H;~FCleanup(){if(H)BCryptDestroyHash(H);BCryptCloseAlgorithmProvider(A,0);}} Cleanup{Algorithm,Digest};
    DWORD Length=0,Received=0;if(BCryptGetProperty(Algorithm,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&Length),sizeof(Length),&Received,0)<0)return false;
    std::vector<unsigned char> Object(Length);if(BCryptCreateHash(Algorithm,&Digest,Object.data(),Length,nullptr,0,0)<0)return false;
    std::array<unsigned char,4096> Buffer{};std::uint64_t Total=0;
    for(;;){DWORD N=0;if(!ReadFile(File.Value,Buffer.data(),DWORD(Buffer.size()),&N,nullptr))return false;if(!N)break;
        Total+=N;if(Total>Limit||BCryptHashData(Digest,Buffer.data(),N,0)<0)return false;}
    std::array<unsigned char,32> Bytes{};if(BCryptFinishHash(Digest,Bytes.data(),DWORD(Bytes.size()),0)<0)return false;
    static const char Hex[]="0123456789abcdef";Out.clear();for(auto B:Bytes){Out+=Hex[B>>4];Out+=Hex[B&15];}return true;
}
static bool Write(const std::wstring& Path,const char* Text)
{
    M::FHandle File(CreateFileW(Path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));
    DWORD N=0;const DWORD Size=DWORD(std::char_traits<char>::length(Text));
    return File.Value!=INVALID_HANDLE_VALUE&&WriteFile(File.Value,Text,Size,&N,nullptr)&&N==Size;
}
static bool Attributes(const std::wstring& Path,DWORD Value)
{
    M::FHandle H(M::Open(Path,true));FILE_BASIC_INFO Info{};Info.FileAttributes=Value;
    return H.Value!=INVALID_HANDLE_VALUE&&SetFileInformationByHandle(H.Value,FileBasicInfo,&Info,sizeof(Info));
}
static bool Exact(const std::wstring& Path,const M::FSnapshot& Expected)
{M::FSnapshot Now;return M::Capture(Path,Hash,Now)&&M::Same(Expected.Info,Now.Info)&&Expected.Sha256==Now.Sha256;}
static bool Junction(const std::wstring& Link,const std::wstring& Target)
{
    std::wstring Native=L"\\??\\"+Target;for(auto& C:Native)if(C==L'/')C=L'\\';
    struct FMountPoint{DWORD Tag;WORD Size,Reserved,SubOffset,SubLength,PrintOffset,PrintLength;wchar_t Path[32768];};
    FMountPoint Data{};Data.Tag=IO_REPARSE_TAG_MOUNT_POINT;Data.SubLength=WORD(Native.size()*sizeof(wchar_t));
    Data.PrintOffset=WORD(Data.SubLength+sizeof(wchar_t));Data.Size=WORD(8+Data.PrintOffset+sizeof(wchar_t));
    std::copy(Native.begin(),Native.end(),Data.Path);
    M::FHandle H(CreateFileW(Link.c_str(),GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS,nullptr));DWORD N=0;
    return H.Value!=INVALID_HANDLE_VALUE&&DeviceIoControl(H.Value,FSCTL_SET_REPARSE_POINT,&Data,DWORD(8+Data.Size),nullptr,0,&N,nullptr);
}
int wmain(int Argc,wchar_t** Argv)
{
    CHECK(Argc==2);const std::wstring Root=Argv[1];
    WIN32_FIND_DATAW Found{};HANDLE Search=FindFirstFileW((Root+L"/*").c_str(),&Found);CHECK(Search!=INVALID_HANDLE_VALUE);
    bool Empty=true;do{const std::wstring Name=Found.cFileName;if(Name!=L"."&&Name!=L"..")Empty=false;}while(FindNextFileW(Search,&Found));FindClose(Search);CHECK(Empty);
    const auto A=Root+L"/source.bin",B=Root+L"/moved.bin",L1=Root+L"/link1.bin",L2=Root+L"/link2.bin",Extra=Root+L"/extra.bin",Occupied=Root+L"/occupied.bin",Symbol=Root+L"/symbol.bin";
    struct FCleanup{std::vector<std::wstring> Paths;~FCleanup(){for(const auto& P:Paths){SetFileAttributesW(P.c_str(),FILE_ATTRIBUTE_NORMAL);DeleteFileW(P.c_str());}}}Cleanup{{Symbol,Extra,Occupied,L2,L1,B,A}};
    CHECK(Write(A,"owned exact rename control\n"));CHECK(CreateHardLinkW(L1.c_str(),A.c_str(),nullptr));CHECK(CreateHardLinkW(L2.c_str(),A.c_str(),nullptr));CHECK(Attributes(A,FILE_ATTRIBUTE_READONLY));
    M::FSnapshot Baseline;CHECK(M::Capture(A,Hash,Baseline));CHECK(Baseline.Info.dwFileAttributes==1&&Baseline.Info.nNumberOfLinks==3);
    auto R=M::Move(A,B,Baseline,Hash);CHECK(R.Moved&&R.AttrsRestored&&R.Exact&&R.Error.empty());CHECK(Exact(B,Baseline)&&Exact(L1,Baseline)&&Exact(L2,Baseline));
    R=M::Move(B,A,Baseline,Hash);CHECK(R.Moved&&R.AttrsRestored&&R.Exact);CHECK(Exact(A,Baseline));
    R=M::Move(A,B,Baseline,Hash,M::EHostDelta::AddedArchive);CHECK(R.Moved&&R.AttrsRestored&&R.Exact);CHECK(Exact(B,Baseline)&&Exact(L1,Baseline)&&Exact(L2,Baseline));
    R=M::Move(B,A,Baseline,Hash);CHECK(R.Exact);
    CHECK(Write(Occupied,"other owned file"));M::FSnapshot Other;CHECK(M::Capture(Occupied,Hash,Other));
    R=M::Move(A,Occupied,Baseline,Hash);CHECK(!R.Moved&&!R.Exact);CHECK(Exact(A,Baseline)&&Exact(Occupied,Other));
    CHECK(Attributes(A,FILE_ATTRIBUTE_READONLY|FILE_ATTRIBUTE_HIDDEN));R=M::Move(A,B,Baseline,Hash);CHECK(!R.Moved&&!R.Exact);CHECK(Attributes(A,FILE_ATTRIBUTE_READONLY));CHECK(Exact(A,Baseline));
    {
        M::FHandle H(M::Open(A,true));CHECK(H.Value!=INVALID_HANDLE_VALUE);FILETIME Changed=Baseline.Info.ftLastWriteTime;Changed.dwHighDateTime^=1;
        CHECK(SetFileTime(H.Value,nullptr,nullptr,&Changed));R=M::Move(A,B,Baseline,Hash);CHECK(!R.Moved&&!R.Exact);CHECK(SetFileTime(H.Value,nullptr,nullptr,&Baseline.Info.ftLastWriteTime));
    }
    CHECK(Exact(A,Baseline));CHECK(CreateHardLinkW(Extra.c_str(),A.c_str(),nullptr));R=M::Move(A,B,Baseline,Hash);CHECK(!R.Moved&&!R.Exact);
    CHECK(Attributes(A,FILE_ATTRIBUTE_NORMAL));CHECK(DeleteFileW(Extra.c_str()));CHECK(Attributes(A,FILE_ATTRIBUTE_READONLY));CHECK(Exact(A,Baseline));
    auto Bad=Baseline;Bad.Sha256[0]=Bad.Sha256[0]=='a'?'b':'a';R=M::Move(A,B,Bad,Hash);CHECK(!R.Moved&&!R.Exact&&R.Error=="source-hash");CHECK(Exact(A,Baseline));
    auto Rewrite=[&](const char* Text)
    {
        if(!Attributes(A,FILE_ATTRIBUTE_NORMAL))return false;
        {
            M::FHandle H(CreateFileW(A.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr));DWORD N=0;
            const DWORD Size=DWORD(std::char_traits<char>::length(Text));
            if(H.Value==INVALID_HANDLE_VALUE||!WriteFile(H.Value,Text,Size,&N,nullptr)||N!=Size||!FlushFileBuffers(H.Value)||
                !SetFileTime(H.Value,nullptr,nullptr,&Baseline.Info.ftLastWriteTime))return false;
        }
        return Attributes(A,FILE_ATTRIBUTE_READONLY);
    };
    // Real same-size content tamper with original write time/attrs restored must still refuse.
    CHECK(Rewrite("OWNED exact rename control\n"));R=M::Move(A,B,Baseline,Hash);CHECK(!R.Moved&&!R.Exact&&R.Error=="source-hash");
    CHECK(Rewrite("owned exact rename control\n"));CHECK(Exact(A,Baseline));
    // Use writable attributes here so refusal proves sharing, not READONLY enforcement.
    CHECK(Attributes(A,FILE_ATTRIBUTE_HIDDEN));M::FSnapshot Writable;CHECK(M::Capture(A,Hash,Writable));bool DeniedWrite=false;
    auto CheckLease=[&](const std::wstring& Path,std::uint64_t Limit,std::string& Out){M::FHandle Writer(CreateFileW(L1.c_str(),FILE_WRITE_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr));DeniedWrite=Writer.Value==INVALID_HANDLE_VALUE&&GetLastError()==ERROR_SHARING_VIOLATION;return Hash(Path,Limit,Out);};
    R=M::Move(A,B,Writable,CheckLease);if(!R.Exact||!DeniedWrite)std::cerr<<"lease stage="<<R.Error<<" moved="<<R.Moved<<" restored="<<R.AttrsRestored<<" denied="<<DeniedWrite<<" before="<<R.Before.dwFileAttributes<<" after="<<R.After.dwFileAttributes<<'\n';CHECK(R.Exact&&DeniedWrite);R=M::Move(B,A,Writable,Hash);CHECK(R.Exact);CHECK(Attributes(A,FILE_ATTRIBUTE_READONLY));CHECK(Exact(A,Baseline));
    R=M::Move(A,B,Baseline,Hash,M::EHostDelta::AddedHidden);CHECK(R.Moved&&!R.AttrsRestored&&!R.Exact&&R.Error=="unsupported-attributes");
    CHECK(Attributes(B,FILE_ATTRIBUTE_READONLY));CHECK(Exact(B,Baseline));R=M::Move(B,A,Baseline,Hash);CHECK(R.Exact);
    auto Oversize=Baseline;Oversize.Info.nFileSizeHigh=1;R=M::Move(A,B,Oversize,Hash);CHECK(!R.Moved&&!R.Exact);
    R=M::Move(A,Root+L"/../escape.bin",Baseline,Hash);CHECK(!R.Moved&&!R.Exact);CHECK(Exact(A,Baseline));
    const auto LinkDir=Root+L"/junction";CHECK(CreateDirectoryW(LinkDir.c_str(),nullptr));
    struct FDirCleanup{std::wstring Path;~FDirCleanup(){RemoveDirectoryW(Path.c_str());}}DirCleanup{LinkDir};
    CHECK(Junction(LinkDir,Root));M::FSnapshot RefusedParent;CHECK(!M::Capture(LinkDir+L"/source.bin",Hash,RefusedParent));
    R=M::Move(A,LinkDir+L"/new.bin",Baseline,Hash);CHECK(!R.Moved&&!R.Exact);CHECK(Exact(A,Baseline));
    int Reparse=0;
    if(CreateSymbolicLinkW(Symbol.c_str(),A.c_str(),2))
    {Reparse=1;M::FSnapshot Refused;CHECK(!M::Capture(Symbol,Hash,Refused));R=M::Move(Symbol,B,Baseline,Hash);CHECK(!R.Moved&&!R.Exact);CHECK(Exact(A,Baseline));}
    else CHECK(GetLastError()==ERROR_PRIVILEGE_NOT_HELD||GetLastError()==ERROR_INVALID_PARAMETER);
    std::cout<<"PASS 15 direct Win32 controls; junction_reparse=pass; optional_file_symlink="<<(Reparse?"pass":"unavailable-no-symlink-privilege")<<"; three hardlinks, exact attrs=1, SHA256/ID/creation/write/linkcount preserved\n";
    return 0;
}
