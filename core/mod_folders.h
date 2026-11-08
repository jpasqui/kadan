// The folders Kadan makes packages in for a mod: each mod's own folder under /Game/Mods, and the folder
// of Kadan's live preview, whose packages are a preview mod's. The names are narrow literals, so the
// plugin joins them with its own in TEXT().
#pragma once
#include <cctype>
#include <string>

#define P4_MODS_FOLDER "/Game/Mods"
#define P4_PREVIEW_FOLDER "/Game/KadanPreview"
#define P4_PREVIEW_MOD_ID_PREFIX "kadan-preview-"

namespace P4MapKit
{
namespace Export
{
// Whether Id is the id of a live preview's mod: the prefix and 32 lowercase hexadecimal digits.
inline bool PreviewModId(const std::string& Id)
{
    const std::string Prefix = P4_PREVIEW_MOD_ID_PREFIX;
    if (Id.size() != Prefix.size() + 32 || Id.compare(0, Prefix.size(), Prefix) != 0)
        return false;
    for (std::size_t At = Prefix.size(); At < Id.size(); ++At)
        if (!std::isxdigit(static_cast<unsigned char>(Id[At])) || std::isupper(static_cast<unsigned char>(Id[At])))
            return false;
    return true;
}

// Whether Path is a package in Kadan's live preview folder, which no mod ships.
inline bool PreviewFolderPackage(const std::string& Path)
{
    const std::string Preview = P4_PREVIEW_FOLDER "/";
    return Path.compare(0, Preview.size(), Preview) == 0;
}

// Whether Path is a package in a mod's folder or in the live preview's folder.
inline bool ModFolderPackage(const std::string& Path)
{
    const std::string Mods = P4_MODS_FOLDER "/";
    const std::string Preview = P4_PREVIEW_FOLDER "/";
    return Path.compare(0, Mods.size(), Mods) == 0 || Path.compare(0, Preview.size(), Preview) == 0;
}
} // namespace Export
} // namespace P4MapKit
