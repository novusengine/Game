#pragma once

#include <Base/Types.h>

namespace Visibility
{
    inline constexpr u32 GEOMETRY_TYPE_BITS = 3;
    inline constexpr u32 RECORD_ID_BITS = 22;
    inline constexpr u32 TRIANGLE_ID_BITS = 7;
    inline constexpr u32 MAX_GEOMETRY_TYPE = (1u << GEOMETRY_TYPE_BITS) - 1u;
    inline constexpr u32 MAX_RECORD_ID = (1u << RECORD_ID_BITS) - 1u;
    inline constexpr u32 MAX_TRIANGLE_ID = (1u << TRIANGLE_ID_BITS) - 1u;
    inline constexpr u32 INVALID_PAYLOAD = 0;
    inline constexpr u32 TERRAIN_MAX_INSTANCE_ID = MAX_RECORD_ID >> 1u;
    inline constexpr u32 TERRAIN_MAX_TRIANGLE_ID = (MAX_TRIANGLE_ID << 1u) | 1u;

    enum class GeometryType : u32
    {
        Background = 0,
        Model = 1,
        Terrain = 2,
        LegacyModel = 3,
        JoltDebug = 4
    };

    struct Payload
    {
        GeometryType geometryType = GeometryType::Background;
        u32 recordID = 0;
        u32 triangleID = 0;
    };

    inline bool Pack(GeometryType geometryType, u32 recordID, u32 triangleID, u32& outPayload)
    {
        const u32 type = static_cast<u32>(geometryType);
        if (type == 0 || type > MAX_GEOMETRY_TYPE || recordID > MAX_RECORD_ID || triangleID > MAX_TRIANGLE_ID)
            return false;

        outPayload = (type << (RECORD_ID_BITS + TRIANGLE_ID_BITS)) |
                     (recordID << TRIANGLE_ID_BITS) | triangleID;
        return true;
    }

    inline Payload Unpack(u32 payload)
    {
        Payload result;
        result.geometryType = static_cast<GeometryType>(payload >> (RECORD_ID_BITS + TRIANGLE_ID_BITS));
        result.recordID = (payload >> TRIANGLE_ID_BITS) & MAX_RECORD_ID;
        result.triangleID = payload & MAX_TRIANGLE_ID;
        return result;
    }

    inline bool PackTerrain(u32 instanceID, u32 triangleID, u32& outPayload)
    {
        if (instanceID > TERRAIN_MAX_INSTANCE_ID || triangleID > TERRAIN_MAX_TRIANGLE_ID)
            return false;
        const u32 recordID = (instanceID << 1u) | (triangleID >> TRIANGLE_ID_BITS);
        return Pack(GeometryType::Terrain, recordID, triangleID & MAX_TRIANGLE_ID, outPayload);
    }

    inline bool UnpackTerrain(u32 payload, u32& outInstanceID, u32& outTriangleID)
    {
        const Payload decoded = Unpack(payload);
        if (decoded.geometryType != GeometryType::Terrain)
            return false;
        outInstanceID = decoded.recordID >> 1u;
        outTriangleID = decoded.triangleID | ((decoded.recordID & 1u) << TRIANGLE_ID_BITS);
        return true;
    }
} // namespace Visibility
