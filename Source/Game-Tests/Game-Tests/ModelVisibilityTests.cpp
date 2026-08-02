#include "Game-Lib/Rendering/Model/View/ModelViewWork.h"
#include "Game-Lib/Rendering/Visibility/VisibilityPayload.h"

#include <catch2/catch2.hpp>

TEST_CASE("Typed visibility payload preserves the shared 3/22/7 fields", "[Rendering][Visibility]")
{
    u32 packed = Visibility::INVALID_PAYLOAD;
    REQUIRE(Visibility::Pack(Visibility::GeometryType::Model, Visibility::MAX_RECORD_ID,
                             Visibility::MAX_TRIANGLE_ID, packed));
    const Visibility::Payload decoded = Visibility::Unpack(packed);
    CHECK(decoded.geometryType == Visibility::GeometryType::Model);
    CHECK(decoded.recordID == Visibility::MAX_RECORD_ID);
    CHECK(decoded.triangleID == Visibility::MAX_TRIANGLE_ID);
}

TEST_CASE("Typed visibility reserves the cleared background encoding", "[Rendering][Visibility]")
{
    u32 packed = 17;
    CHECK_FALSE(Visibility::Pack(Visibility::GeometryType::Background, 0, 0, packed));
    CHECK_FALSE(Visibility::Pack(Visibility::GeometryType::Model, Visibility::MAX_RECORD_ID + 1u, 0, packed));
    CHECK_FALSE(Visibility::Pack(Visibility::GeometryType::Model, 0, Visibility::MAX_TRIANGLE_ID + 1u, packed));
}

TEST_CASE("Terrain visibility preserves all 256 cell triangles in R32", "[Rendering][Visibility]")
{
    for (u32 expectedTriangleID = 0; expectedTriangleID <= Visibility::TERRAIN_MAX_TRIANGLE_ID;
         ++expectedTriangleID)
    {
        u32 packed = Visibility::INVALID_PAYLOAD;
        REQUIRE(Visibility::PackTerrain(Visibility::TERRAIN_MAX_INSTANCE_ID,
                                        expectedTriangleID, packed));

        u32 instanceID = 0;
        u32 triangleID = 0;
        REQUIRE(Visibility::UnpackTerrain(packed, instanceID, triangleID));
        CHECK(instanceID == Visibility::TERRAIN_MAX_INSTANCE_ID);
        CHECK(triangleID == expectedTriangleID);
    }
}

TEST_CASE("Terrain visibility rejects values outside its R32 record mapping", "[Rendering][Visibility]")
{
    u32 packed = Visibility::INVALID_PAYLOAD;
    CHECK_FALSE(Visibility::PackTerrain(Visibility::TERRAIN_MAX_INSTANCE_ID + 1u, 0u, packed));
    CHECK_FALSE(Visibility::PackTerrain(0u, Visibility::TERRAIN_MAX_TRIANGLE_ID + 1u, packed));
}

TEST_CASE("Model visibility records preserve reconstruction indices", "[Rendering][Visibility]")
{
    ModelView::VisibilityRecord packed;
    REQUIRE(ModelView::PackVisibilityRecord((1u << ModelView::MODEL_VISIBILITY_INSTANCE_BITS) - 1u,
                                             (1u << ModelView::MODEL_VISIBILITY_MESH_BITS) - 1u,
                                             (1u << ModelView::MODEL_VISIBILITY_LOD_BITS) - 1u,
                                             (1u << ModelView::MODEL_VISIBILITY_SUBMESH_BITS) - 1u,
                                             (1u << ModelView::MODEL_VISIBILITY_MESHLET_BITS) - 1u, packed));
    const ModelView::DecodedVisibilityRecord decoded = ModelView::UnpackVisibilityRecord(packed);
    CHECK(decoded.instanceIndex == (1u << ModelView::MODEL_VISIBILITY_INSTANCE_BITS) - 1u);
    CHECK(decoded.meshIndex == (1u << ModelView::MODEL_VISIBILITY_MESH_BITS) - 1u);
    CHECK(decoded.lodIndex == (1u << ModelView::MODEL_VISIBILITY_LOD_BITS) - 1u);
    CHECK(decoded.submeshIndex == (1u << ModelView::MODEL_VISIBILITY_SUBMESH_BITS) - 1u);
    CHECK(decoded.meshletIndex == (1u << ModelView::MODEL_VISIBILITY_MESHLET_BITS) - 1u);
}

TEST_CASE("Model visibility records reject indices that exceed their GPU contract", "[Rendering][Visibility]")
{
    ModelView::VisibilityRecord packed;
    CHECK_FALSE(ModelView::PackVisibilityRecord(1u << ModelView::MODEL_VISIBILITY_INSTANCE_BITS, 0, 0, 0, 0, packed));
    CHECK_FALSE(ModelView::PackVisibilityRecord(0, 1u << ModelView::MODEL_VISIBILITY_MESH_BITS, 0, 0, 0, packed));
    CHECK_FALSE(ModelView::PackVisibilityRecord(0, 0, 1u << ModelView::MODEL_VISIBILITY_LOD_BITS, 0, 0, packed));
    CHECK_FALSE(ModelView::PackVisibilityRecord(0, 0, 0, 1u << ModelView::MODEL_VISIBILITY_SUBMESH_BITS, 0, packed));
    CHECK_FALSE(ModelView::PackVisibilityRecord(0, 0, 0, 0, 1u << ModelView::MODEL_VISIBILITY_MESHLET_BITS, packed));
}
