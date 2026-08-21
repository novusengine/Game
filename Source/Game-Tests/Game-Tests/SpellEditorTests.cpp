#include "Game-Lib/Editor/SpellEditorData.h"

#include <MetaGen/Shared/DatabaseEditor/DatabaseEditor.h>
#include <MetaGen/Shared/Spell/Spell.h>

#include <catch2/catch2.hpp>

TEST_CASE("Spell editor mutation results remain request correlated", "[SpellEditor]")
{
    Editor::SpellEditorData data;
    const u32 firstRequestID = data.StartMutationRequest();
    const u32 secondRequestID = data.StartMutationRequest();

    data.RecordMutationResult({ .requestID = secondRequestID, .artifact = static_cast<u8>(MetaGen::Shared::Spell::SpellEditorArtifactEnum::Spell), .artifactID = 42, .mutationType = MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Update, .succeeded = true, .response = "second" });
    data.RecordMutationResult({ .requestID = firstRequestID, .artifact = static_cast<u8>(MetaGen::Shared::Spell::SpellEditorArtifactEnum::SpellAuraConstraintGroup), .artifactID = 43, .mutationType = MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Create, .succeeded = false, .response = "first" });

    const auto firstResult = data.TakeMutationResult(firstRequestID);
    REQUIRE(firstResult);
    CHECK(firstResult->requestID == firstRequestID);
    CHECK(firstResult->artifact == static_cast<u8>(MetaGen::Shared::Spell::SpellEditorArtifactEnum::SpellAuraConstraintGroup));
    CHECK(firstResult->artifactID == 43);
    CHECK_FALSE(firstResult->succeeded);
    CHECK(firstResult->response == "first");

    const auto secondResult = data.TakeMutationResult(secondRequestID);
    REQUIRE(secondResult);
    CHECK(secondResult->requestID == secondRequestID);
    CHECK(secondResult->artifact == static_cast<u8>(MetaGen::Shared::Spell::SpellEditorArtifactEnum::Spell));
    CHECK(secondResult->artifactID == 42);
    CHECK(secondResult->succeeded);
    CHECK_FALSE(data.TakeMutationResult(secondRequestID));
}

TEST_CASE("Generated spell effect metadata validates typed parameters", "[SpellEditor]")
{
    using namespace MetaGen::Shared::Spell;

    const SpellEffectDescriptor* damage = GetSpellEffectDescriptor(SpellEffectTypeEnum::Damage);
    REQUIRE(damage);
    REQUIRE(damage->owner == SpellEffectOwner::Spell);
    REQUIRE(damage->target.mode == SpellEffectTargetMode::Required);

    std::array<i32, 6> validParameters = { 10, 20, 0, 0, 0, 0 };
    CHECK(ValidateSpellEffectParameters(*damage, validParameters).IsValid());

    std::array<i32, 6> invertedRange = { 20, 10, 0, 0, 0, 0 };
    const SpellEffectParameterValidationResult result = ValidateSpellEffectParameters(*damage, invertedRange);
    CHECK(result.error == SpellEffectParameterValidationError::ConstraintViolation);
    CHECK(result.parameterIndex == 0);
    CHECK(result.relatedParameterIndex == 1);
}
