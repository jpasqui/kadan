#pragma once
#include <algorithm>
#include <cstdint>
#include <string>

namespace P4RegistryLease
{
// Exact UTF-8 fields in the validated index's sorted object-key order.
// No separators can alias a different field boundary, even for empty parents.
template <class TRegistry, class TUpdate>
bool CanonicalRows(const TRegistry& Registry, TUpdate&& Update)
{
    if (!Registry.Valid())
        return false;
    const std::string Domain = "p4-material-parent-rows/1\n";
    if (!Update(reinterpret_cast<const std::uint8_t*>(Domain.data()), int(Domain.size())))
        return false;
    const auto Field = [&](const std::string& Value)
    {
        std::uint8_t Size[8];
        for (unsigned I = 0; I < 8; ++I)
            Size[I] = std::uint8_t(std::uint64_t(Value.size()) >> (I * 8));
        if (!Update(Size, 8))
            return false;
        for (std::size_t At = 0; At < Value.size(); At += 65536)
            if (!Update(reinterpret_cast<const std::uint8_t*>(Value.data() + At),
                    int(std::min<std::size_t>(65536, Value.size() - At))))
                return false;
        return true;
    };
    for (const auto& Pair : Registry.Rows())
    {
        const auto& R = Pair.second;
        for (const std::string* Value :
            {&R.Package, &R.Object, &R.Class, &R.ParentPackage, &R.ParentObject, &R.ParentClass})
            if (!Field(*Value))
                return false;
    }
    return true;
}
} //namespace P4RegistryLease
