#define PLATFORM_WINDOWS 0
#include "../Kadan/Source/Kadan/Private/P4RegistryInputLease.h"
#if defined(_WINDOWS_) || defined(_INC_WINDOWS)
#error The unavailable lease stub must not include Windows headers.
#endif
#include <iostream>
int main()
{
    P4RegistryLease::FInput Input,Directory;
    if(Input.Open(L"anything")||Directory.Open(L"anything",true)||Input.Current()||Directory.Current()||Input.Size()||Directory.Size())return 1;
    std::cout<<"PASS: unavailable native lease API compiles without Windows headers and always refuses\n";
    return 0;
}
