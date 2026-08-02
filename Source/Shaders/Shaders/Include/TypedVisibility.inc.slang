#ifndef TYPED_VISIBILITY_INCLUDED
#define TYPED_VISIBILITY_INCLUDED

#define TYPED_VISIBILITY_GEOMETRY_BITS 3u
#define TYPED_VISIBILITY_RECORD_BITS 22u
#define TYPED_VISIBILITY_TRIANGLE_BITS 7u
#define TYPED_VISIBILITY_MAX_RECORD_ID ((1u << TYPED_VISIBILITY_RECORD_BITS) - 1u)
#define TYPED_VISIBILITY_MAX_TRIANGLE_ID ((1u << TYPED_VISIBILITY_TRIANGLE_BITS) - 1u)
#define TYPED_VISIBILITY_INVALID 0u
#define TYPED_VISIBILITY_MODEL 1u
#define TYPED_VISIBILITY_TERRAIN 2u
#define TYPED_VISIBILITY_LEGACY_MODEL 3u
#define TYPED_VISIBILITY_JOLT_DEBUG 4u

struct TypedVisibilityPayload
{
    uint geometryTypeID;
    uint recordID;
    uint triangleID;
};

uint PackTypedVisibility(uint geometryTypeID, uint recordID, uint triangleID)
{
    return (geometryTypeID << (TYPED_VISIBILITY_RECORD_BITS + TYPED_VISIBILITY_TRIANGLE_BITS)) |
           (recordID << TYPED_VISIBILITY_TRIANGLE_BITS) | triangleID;
}

TypedVisibilityPayload UnpackTypedVisibility(uint payload)
{
    TypedVisibilityPayload result;
    result.geometryTypeID = payload >> (TYPED_VISIBILITY_RECORD_BITS + TYPED_VISIBILITY_TRIANGLE_BITS);
    result.recordID = (payload >> TYPED_VISIBILITY_TRIANGLE_BITS) & TYPED_VISIBILITY_MAX_RECORD_ID;
    result.triangleID = payload & TYPED_VISIBILITY_MAX_TRIANGLE_ID;
    return result;
}

bool IsModelVisibility(uint targetValue)
{
    return targetValue != TYPED_VISIBILITY_INVALID &&
           UnpackTypedVisibility(targetValue).geometryTypeID == TYPED_VISIBILITY_MODEL;
}

#endif
