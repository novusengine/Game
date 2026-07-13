#pragma once

#include <Base/Types.h>
#include <xxhash/xxhash64.h>

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace Util::AssetPath
{
    inline std::string Create(std::string_view root, std::string_view path)
    {
        std::string result(root);
        result += '/';
        result += path;

        std::replace(result.begin(), result.end(), '\\', '/');
        std::transform(result.begin(), result.end(), result.begin(), [](unsigned char character)
        {
            return static_cast<char>(std::tolower(character));
        });
        return result;
    }

    inline std::string Model(std::string_view path) { return Create("model", path); }
    inline std::string Texture(std::string_view path) { return Create("texture", path); }
    inline std::string Map(std::string_view path) { return Create("map", path); }
    inline u64 Hash(std::string_view path) { return XXHash64::hash(path.data(), path.size(), 0); }
}
