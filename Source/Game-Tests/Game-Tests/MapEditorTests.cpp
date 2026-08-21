#include "Game-Lib/Editor/MapEditorData.h"

#include <MetaGen/Shared/DatabaseEditor/DatabaseEditor.h>

#include <catch2/catch2.hpp>

TEST_CASE("Map editor mutation results preserve request correlation and assigned IDs", "[MapEditor]")
{
    Editor::MapEditorData data;
    const u32 createRequestID = data.StartMutationRequest();
    const u32 updateRequestID = data.StartMutationRequest();

    data.RecordMutationResult({ .requestID = updateRequestID, .artifact = 0, .artifactID = 41, .mutationType = MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Update, .succeeded = false, .response = "update rejected" });
    data.RecordMutationResult({ .requestID = createRequestID, .artifact = 0, .artifactID = 73, .mutationType = MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Create, .succeeded = true, .response = "created" });

    const auto createResult = data.TakeMutationResult(createRequestID);
    REQUIRE(createResult);
    CHECK(createResult->requestID == createRequestID);
    CHECK(createResult->artifactID == 73);
    CHECK(createResult->mutationType == MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Create);
    CHECK(createResult->succeeded);

    const auto updateResult = data.TakeMutationResult(updateRequestID);
    REQUIRE(updateResult);
    CHECK(updateResult->requestID == updateRequestID);
    CHECK(updateResult->artifactID == 41);
    CHECK_FALSE(updateResult->succeeded);
    CHECK_FALSE(data.TakeMutationResult(createRequestID));
}
