#define P4_REGISTRY_LEASE_HOST_TEST 1
#include "../Kadan/Source/Kadan/Private/P4RegistryInputLease.h"
#include <iostream>
#include <set>
#define CHECK(X) do {if(!(X)){std::cerr<<"failed line "<<__LINE__<<": "<<#X<<" winerror="<<GetLastError()<<'\n';return 1;}}while(false)
static bool Write(const std::wstring& Path,const char* Data)
{
    const HANDLE File=CreateFileW(Path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(File==INVALID_HANDLE_VALUE)return false;
    DWORD Written=0;const DWORD Size=DWORD(std::char_traits<char>::length(Data));const bool Okay=WriteFile(File,Data,Size,&Written,nullptr)&&Written==Size;
    CloseHandle(File);return Okay;
}
static std::set<std::wstring> Members(const std::wstring& Root,bool Relevant=false)
{
    std::set<std::wstring> Out;WIN32_FIND_DATAW Found{};const HANDLE Scan=FindFirstFileW((Root+L"/*").c_str(),&Found);
    if(Scan==INVALID_HANDLE_VALUE)return Out;
    do
    {
        if(Found.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)continue;
        std::wstring Name=Found.cFileName;for(auto& C:Name)if(C>=L'A'&&C<=L'Z')C=wchar_t(C-L'A'+L'a');
        const auto Dot=Name.find_last_of(L'.');const auto Extension=Dot==std::wstring::npos?L"":Name.substr(Dot);
        if(!Relevant||Extension==L".pak"||Extension==L".utoc"||Extension==L".ucas")Out.insert(Found.cFileName);
    }while(FindNextFileW(Scan,&Found));
    FindClose(Scan);return Out;
}
int wmain(int Argc,wchar_t** Argv)
{
    CHECK(Argc==2);const std::wstring Root=Argv[1],File=Root+L"/input.bin",Alias=Root+L"/alias.bin",Moved=Root+L"/moved.bin",Partition=Root+L"/new.ucas";
    // The driver creates this exact empty owned directory. Never clear an unknown folder.
    WIN32_FIND_DATAW Found{};const HANDLE Scan=FindFirstFileW((Root+L"/*").c_str(),&Found);
    CHECK(Scan!=INVALID_HANDLE_VALUE);bool Empty=true;do{if(std::wstring(Found.cFileName)!=L"."&&std::wstring(Found.cFileName)!=L"..")Empty=false;}while(FindNextFileW(Scan,&Found));FindClose(Scan);CHECK(Empty);
    struct FCleanup
    {
        std::wstring File,Alias,Moved,Partition,Selected,Scratch;
        ~FCleanup(){DeleteFileW(Scratch.c_str());DeleteFileW(Selected.c_str());DeleteFileW(Partition.c_str());DeleteFileW(Alias.c_str());DeleteFileW(Moved.c_str());DeleteFileW(File.c_str());}
    } Cleanup{File,Alias,Moved,Partition,Root+L"/selected.bin",Root+L"/scratch.txt"};
    CHECK(Write(File,"source"));CHECK(CreateHardLinkW(Alias.c_str(),File.c_str(),nullptr));
    {
        P4RegistryLease::FInput Lease;CHECK(Lease.Open(File));CHECK(Lease.Current());CHECK(Lease.Size()==6);
        CHECK(!Write(File,"changed"));CHECK(!Write(Alias,"changed"));CHECK(!DeleteFileW(File.c_str()));CHECK(!MoveFileExW(File.c_str(),Moved.c_str(),0));
        const HANDLE Reader=CreateFileW(Alias.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        CHECK(Reader!=INVALID_HANDLE_VALUE);CloseHandle(Reader);CHECK(Lease.Current());CHECK(!Lease.Open(File));
        P4RegistryLease::FInput Second;CHECK(Second.Open(Alias));CHECK(Second.Current());
    }
    // Destruction releases every lock; no weak/static ownership may extend it.
    CHECK(Write(File,"after"));CHECK(MoveFileExW(File.c_str(),Moved.c_str(),0));CHECK(MoveFileExW(Moved.c_str(),File.c_str(),0));
    {
        const HANDLE Writer=CreateFileW(File.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        CHECK(Writer!=INVALID_HANDLE_VALUE);P4RegistryLease::FInput Conflict;CHECK(!Conflict.Open(File));CHECK(!Conflict.Current());CloseHandle(Writer);
    }
    {
        P4RegistryLease::FInput Directory;CHECK(Directory.Open(Root,true));CHECK(Directory.Current());
        // Scratch files do not alter the relevant container epoch; partitions do.
        const auto Before=Members(Root,true);CHECK(Write(Cleanup.Scratch,"scratch"));CHECK(Directory.Current());CHECK(Members(Root,true)==Before);
        CHECK(Write(Partition,"partition"));CHECK(Directory.Current());CHECK(Members(Root,true)!=Before);
        CHECK(DeleteFileW(Partition.c_str()));CHECK(Members(Root,true)==Before);CHECK(DeleteFileW(Cleanup.Scratch.c_str()));
        const HANDLE Metadata=CreateFileW(Root.c_str(),FILE_READ_ATTRIBUTES|FILE_WRITE_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
            nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,nullptr);CHECK(Metadata!=INVALID_HANDLE_VALUE);
        FILETIME Original{};CHECK(GetFileTime(Metadata,nullptr,nullptr,&Original));auto Changed=Original;Changed.dwHighDateTime^=1;
        CHECK(SetFileTime(Metadata,nullptr,nullptr,&Changed));CHECK(Directory.Current());CHECK(SetFileTime(Metadata,nullptr,nullptr,&Original));CloseHandle(Metadata);
    }
    {
        // Actual input-file timestamps remain exact even though directory timestamps do not.
        const HANDLE Metadata=CreateFileW(File.c_str(),FILE_READ_ATTRIBUTES|FILE_WRITE_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
            nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);CHECK(Metadata!=INVALID_HANDLE_VALUE);
        FILETIME Original{};CHECK(GetFileTime(Metadata,nullptr,nullptr,&Original));auto Changed=Original;Changed.dwHighDateTime^=1;
        P4RegistryLease::FInput Input;CHECK(Input.Open(File));CHECK(SetFileTime(Metadata,nullptr,nullptr,&Changed));CHECK(!Input.Current());
        CHECK(SetFileTime(Metadata,nullptr,nullptr,&Original));CHECK(Input.Current());CloseHandle(Metadata);
    }
    P4RegistryLease::FInput Missing,WrongKind,EmptyName;CHECK(!Missing.Open(Root+L"/absent"));CHECK(!WrongKind.Open(Root));CHECK(!EmptyName.Open(L""));
    {
        // Selected hardlink names have the same native deny-delete protection.
        const std::wstring Junction=Root+L"/selected.bin";
        CHECK(CreateHardLinkW(Junction.c_str(),File.c_str(),nullptr));
        P4RegistryLease::FInput Lease;CHECK(Lease.Open(Junction));CHECK(!DeleteFileW(Junction.c_str()));CHECK(Lease.Current());
        // This is the same deny-delete contract for alternative selected spellings.
        // Close before deleting our extra exact-name control.
    }
    CHECK(DeleteFileW((Root+L"/selected.bin").c_str()));
    std::cout<<"PASS: deny write/delete/rename/hardlink mutation, readers, existing-writer refusal, stable directory identity, harmless scratch, partition epoch, exact file time/lifetime\n";
    return 0;
}
