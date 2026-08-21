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
#define TYPED_VISIBILITY_MODEL_LAYERED 5u
#define TYPED_VISIBILITY_MODEL_PREVIOUS 6u
#define TYPED_VISIBILITY_MODEL_LAYERED_PREVIOUS 7u

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
    if (targetValue == TYPED_VISIBILITY_INVALID)
        return false;
    uint geometryTypeID = UnpackTypedVisibility(targetValue).geometryTypeID;
    return geometryTypeID == TYPED_VISIBILITY_MODEL || geometryTypeID == TYPED_VISIBILITY_MODEL_LAYERED ||
           geometryTypeID == TYPED_VISIBILITY_MODEL_PREVIOUS ||
           geometryTypeID == TYPED_VISIBILITY_MODEL_LAYERED_PREVIOUS;
}

uint ModelVisibilityGeometryType(uint resolveClass, bool previousResource)
{
    if (previousResource)
        return resolveClass == 0u ? TYPED_VISIBILITY_MODEL_PREVIOUS : TYPED_VISIBILITY_MODEL_LAYERED_PREVIOUS;
    return resolveClass == 0u ? TYPED_VISIBILITY_MODEL : TYPED_VISIBILITY_MODEL_LAYERED;
}

uint ModelVisibilityResolveClass(uint geometryTypeID)
{
    return geometryTypeID == TYPED_VISIBILITY_MODEL_LAYERED ||
           geometryTypeID == TYPED_VISIBILITY_MODEL_LAYERED_PREVIOUS ? 1u : 0u;
}

bool ModelVisibilityUsesPreviousResource(uint geometryTypeID)
{
    return geometryTypeID == TYPED_VISIBILITY_MODEL_PREVIOUS ||
           geometryTypeID == TYPED_VISIBILITY_MODEL_LAYERED_PREVIOUS;
}

#endif
