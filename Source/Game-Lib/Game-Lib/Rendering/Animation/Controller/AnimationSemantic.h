#pragma once

#include <Base/Types.h>
#include <Base/Util/StringUtils.h>

#include <string>
#include <string_view>

namespace Animation
{
    // Produces stable identifiers for authored animation concepts such as slots and synchronization markers.
    inline u64 HashAnimationSemantic(std::string_view category, std::string_view name)
    {
        std::string canonical;
        canonical.reserve(category.size() + name.size() + 1);
        canonical.append(category);
        canonical.push_back('/');
        for (const char character : name)
            canonical.push_back(character >= 'A' && character <= 'Z' ? static_cast<char>(character - 'A' + 'a') : character);
        return XXHash64::hash(canonical.data(), canonical.size(), 0);
    }

    inline u64 HashAnimationSlot(std::string_view name) { return HashAnimationSemantic("animation-slot", name); }
    inline u64 HashSynchronizationMarker(std::string_view name) { return HashAnimationSemantic("animation-sync-marker", name); }
}
