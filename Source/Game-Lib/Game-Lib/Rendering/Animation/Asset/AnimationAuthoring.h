#pragma once

#include <Base/Types.h>
#include <FileFormat/Novus/FileHeader.h>

#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstring>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace glm
{
    template <typename T, typename F> requires std::is_same_v<std::remove_const_t<T>, vec2>
    void VisitMembers(T& value, F&& field) { field("0", value.x); field("1", value.y); }
    template <typename T, typename F> requires std::is_same_v<std::remove_const_t<T>, vec3>
    void VisitMembers(T& value, F&& field) { field("0", value.x); field("1", value.y); field("2", value.z); }
    template <typename T, typename F> requires std::is_same_v<std::remove_const_t<T>, quat>
    void VisitMembers(T& value, F&& field) { field("0", value.w); field("1", value.x); field("2", value.y); field("3", value.z); }
    template <typename T, typename F> requires std::is_same_v<std::remove_const_t<T>, mat4x3>
    void VisitMembers(T& value, F&& field) { field("0", value[0]); field("1", value[1]); field("2", value[2]); field("3", value[3]); }
}

namespace Animation::Authoring
{
    template <typename T> inline constexpr bool IsVector = false;
    template <typename T, typename A> inline constexpr bool IsVector<std::vector<T, A>> = true;
    template <typename T> inline constexpr bool IsArray = false;
    template <typename T, size_t N> inline constexpr bool IsArray<std::array<T, N>> = true;
    template <typename T> inline constexpr bool IsMath = std::is_same_v<T, vec2> || std::is_same_v<T, vec3> || std::is_same_v<T, quat> || std::is_same_v<T, glm::mat4x3>;
    template <typename T> inline constexpr bool IsScalar = std::is_arithmetic_v<T> || std::is_enum_v<T> || std::is_same_v<T, std::string>;
    inline constexpr u32 MaximumSourceBytes = 4 * 1024 * 1024;

    // Source files use the asset type/version header followed by a source signature.
    // Fields have a fixed schema order; only strings and vectors carry a u32 length.
    // No property names, type tags, pointers or struct padding are written.
    class BinaryWriter
    {
      public:
        std::vector<u8> bytes;
        void Append(const void* value, size_t size)
        {
            if (size > MaximumSourceBytes - bytes.size()) throw std::runtime_error("Source exceeds the 4 MiB editor limit");
            if (!size) return;
            const auto* begin = static_cast<const u8*>(value);
            bytes.insert(bytes.end(), begin, begin + size);
        }
        template <typename T> void Value(const T& value)
        {
            static_assert(std::endian::native == std::endian::little);
            if constexpr (std::is_enum_v<T>) Value(static_cast<std::underlying_type_t<T>>(value));
            else if constexpr (std::is_same_v<T, bool>) Value(static_cast<u8>(value));
            else if constexpr (std::is_arithmetic_v<T>)
            {
                if constexpr (std::is_floating_point_v<T>) if (!std::isfinite(value)) throw std::runtime_error("Source contains a non-finite number");
                Append(&value, sizeof(T));
            }
            else if constexpr (std::is_same_v<T, std::string>)
            {
                if (value.size() > MaximumSourceBytes) throw std::runtime_error("Source string exceeds the editor limit");
                Value(static_cast<u32>(value.size())); Append(value.data(), value.size());
            }
            else if constexpr (IsVector<T> || IsArray<T>)
            {
                if constexpr (IsVector<T>)
                {
                    if (value.size() > 65536) throw std::runtime_error("Source collection exceeds the editor limit");
                    Value(static_cast<u32>(value.size()));
                }
                for (const auto& element : value) Value(element);
            }
            else VisitMembers(value, [&](std::string_view, const auto& member) { Value(member); });
        }
    };

    class BinaryReader
    {
      public:
        explicit BinaryReader(std::span<const u8> bytes) : _bytes(bytes)
        {
            if (bytes.size() > MaximumSourceBytes) throw std::runtime_error("Source exceeds the 4 MiB editor limit");
        }
        size_t Remaining() const { return _bytes.size() - _offset; }
        void Read(void* value, size_t size)
        {
            if (size > Remaining()) throw std::runtime_error("Truncated animation source");
            if (!size) return;
            std::memcpy(value, _bytes.data() + _offset, size); _offset += size;
        }
        template <typename T> void Value(T& value)
        {
            if constexpr (std::is_enum_v<T>)
            {
                std::underlying_type_t<T> raw; Value(raw); value = static_cast<T>(raw);
            }
            else if constexpr (std::is_same_v<T, bool>)
            {
                u8 raw; Value(raw); if (raw > 1) throw std::runtime_error("Invalid boolean in animation source"); value = raw != 0;
            }
            else if constexpr (std::is_arithmetic_v<T>)
            {
                Read(&value, sizeof(T));
                if constexpr (std::is_floating_point_v<T>) if (!std::isfinite(value)) throw std::runtime_error("Source contains a non-finite number");
            }
            else if constexpr (std::is_same_v<T, std::string>)
            {
                u32 count; Value(count);
                if (count > Remaining()) throw std::runtime_error("Invalid source string length");
                value.assign(reinterpret_cast<const char*>(_bytes.data() + _offset), count); _offset += count;
            }
            else if constexpr (IsVector<T>)
            {
                u32 count; Value(count);
                if (count > Remaining() || count > 65536 || count > _allocationBudget / sizeof(typename T::value_type)) throw std::runtime_error("Invalid source collection length");
                _allocationBudget -= count * sizeof(typename T::value_type);
                value.resize(count);
                for (auto& element : value) Value(element);
            }
            else if constexpr (IsArray<T>) for (auto& element : value) Value(element);
            else VisitMembers(value, [&](std::string_view, auto& member) { Value(member); });
        }
      private:
        std::span<const u8> _bytes;
        size_t _offset = 0;
        size_t _allocationBudget = 16 * MaximumSourceBytes;
    };

    inline void WriteHeader(BinaryWriter& writer, FileHeader::Type type)
    {
        writer.Value(type); writer.Value(u32{1}); writer.Value(u32{0x43525341}); // ASRC
    }
    inline void ReadHeader(BinaryReader& reader, FileHeader::Type expected)
    {
        FileHeader::Type type; u32 version, signature;
        reader.Value(type); reader.Value(version); reader.Value(signature);
        if (type != expected || version != 1 || signature != 0x43525341) throw std::runtime_error("Unsupported animation source type or version");
    }

    // These paths identify typed editor fields; they are not a storage format.
    template <typename T, typename F> void Walk(T& value, const std::string& path, F&& field)
    {
        field(path, value);
        using V = std::remove_const_t<T>;
        if constexpr (IsVector<V> || IsArray<V>)
            for (size_t i = 0; i < value.size(); ++i) Walk(value[i], path + "/" + std::to_string(i), field);
        else if constexpr (!IsScalar<V>)
            VisitMembers(value, [&](std::string_view name, auto& member) { Walk(member, path + "/" + std::string(name), field); });
    }

    template <typename T> std::string Text(const T& value)
    {
        if constexpr (std::is_same_v<T, std::string>) return value;
        else if constexpr (std::is_same_v<T, bool>) return value ? "true" : "false";
        else if constexpr (std::is_enum_v<T>) return Text(static_cast<std::underlying_type_t<T>>(value));
        else if constexpr (std::is_arithmetic_v<T>)
        {
            char buffer[64];
            auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
            if (result.ec != std::errc{}) throw std::runtime_error("Could not format editor value");
            return {buffer, result.ptr};
        }
        else
        {
            std::string result = "[";
            auto add = [&](const auto& member) { if (result.size() > 1) result += ","; result += Text(member); };
            if constexpr (IsArray<T>) for (const auto& member : value) add(member);
            else VisitMembers(value, [&](std::string_view, const auto& member) { add(member); });
            return result + "]";
        }
    }

    inline std::string_view Trim(std::string_view value)
    {
        const auto first = value.find_first_not_of(" \t\r\n");
        if (first == std::string_view::npos) return {};
        return value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
    }
    template <typename T> void ParseValue(std::string_view text, T& value)
    {
        if constexpr (std::is_same_v<T, std::string>) value = text;
        else if constexpr (std::is_enum_v<T>)
        {
            std::underlying_type_t<T> raw; ParseValue(text, raw); value = static_cast<T>(raw);
        }
        else if constexpr (std::is_same_v<T, bool>)
        {
            text = Trim(text);
            if (text != "true" && text != "false") throw std::runtime_error("Expected true or false");
            value = text == "true";
        }
        else if constexpr (std::is_arithmetic_v<T>)
        {
            text = Trim(text);
            T parsed{};
            if (text.empty()) throw std::runtime_error("Expected a numeric value");
            const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
            if (text.empty() || result.ec != std::errc{} || result.ptr != text.data() + text.size()) throw std::runtime_error("Value is outside the field range or has an invalid numeric form");
            if constexpr (std::is_floating_point_v<T>) if (!std::isfinite(parsed)) throw std::runtime_error("Expected a finite number");
            value = parsed;
        }
        else if constexpr (IsMath<T> || IsArray<T>)
        {
            text = Trim(text);
            if (text.size() < 2 || text.front() != '[' || text.back() != ']') throw std::runtime_error("Expected a component list");
            text = text.substr(1, text.size() - 2);
            size_t consumed = 0;
            auto parse = [&](auto& component)
            {
                if (consumed > text.size()) throw std::runtime_error("Missing vector component");
                const auto comma = text.find(',', consumed);
                const auto end = comma == std::string_view::npos ? text.size() : comma;
                ParseValue(text.substr(consumed, end - consumed), component); consumed = end + 1;
            };
            if constexpr (IsArray<T>) for (auto& component : value) parse(component);
            else VisitMembers(value, [&](std::string_view, auto& component) { parse(component); });
            if (consumed != text.size() + 1) throw std::runtime_error("Too many vector components");
        }
        else throw std::runtime_error("Select a scalar or component field");
    }

    template <typename T> void EditField(T& source, const std::string& path, std::string_view value)
    {
        bool found = false;
        Walk(source, "", [&](const std::string& candidate, auto& member)
        {
            if (candidate == path) { ParseValue(value, member); found = true; }
        });
        if (!found) throw std::runtime_error("Unknown authoring field");
    }
}
