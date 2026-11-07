#pragma once
// Exact identity of the qualified 1.1.1 registry and container-index representation.
// This does not hash or admit every package payload. Native acquisition retains custody.
#include <string>

namespace P4MapKit { namespace OriginalInventory {
inline const char* RegistrySha256()
{ return "ee9aa8962ee1ae27430ef1d4b6713ead564a25119f2b9089bbe56f4ab2d24af1"; }
inline const char* ContainerHeaderSha256()
{ return "08acc5d5323fd2e267aa4661797ae1f4fd04f119b700b49935abb7971eeb1da2"; }
inline const char* UtocSha256()
{ return "1bcee8b564f2621ad70ee26e31b7e04c1e66702fe06c55994dc2d62cbe0e9b24"; }
inline bool RequireSupported(const std::string& Registry,const std::string& Header,
    const std::string& Utoc,std::string& Error)
{
    if(Registry!=RegistrySha256() || Header!=ContainerHeaderSha256() || Utoc!=UtocSha256())
    { Error="original-game-inventory-build-unqualified"; return false; }
    Error.clear(); return true;
}
}}
