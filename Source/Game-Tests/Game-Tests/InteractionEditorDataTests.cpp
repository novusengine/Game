#include "Game-Lib/Editor/InteractionEditorData.h"

#include <Base/Memory/Bytebuffer.h>

#include <MetaGen/Shared/ClientDB/ClientDB.h>

#include <catch2/catch2.hpp>

#include <limits>
#include <memory>
#include <string_view>

namespace
{
    using Artifact = MetaGen::Shared::Interaction::InteractionEditorArtifactEnum;
    using MutationType = MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum;

    template <typename Record>
    void AppendEmptyArtifact(Editor::InteractionEditorData& data, u32 requestID, Artifact artifact)
    {
        ::ClientDB::Data storage;
        REQUIRE(storage.Initialize<Record>());
        std::shared_ptr<Bytebuffer> snapshot = Bytebuffer::BorrowRuntime(storage.GetSerializedSize());
        REQUIRE(snapshot);
        REQUIRE(storage.Save(snapshot));
        REQUIRE(snapshot->writtenData <= std::numeric_limits<u16>::max());
        REQUIRE(data.AppendSnapshotChunk(requestID, static_cast<u8>(artifact), static_cast<u32>(snapshot->writtenData), 0, snapshot->GetDataPointer(), static_cast<u16>(snapshot->writtenData)));
    }

    void LoadEmptyInteractionSnapshot(Editor::InteractionEditorData& data)
    {
        const u32 requestID = data.StartRequest();
        REQUIRE(data.BeginSnapshot(requestID, static_cast<u8>(Artifact::Count), 0));
        AppendEmptyArtifact<MetaGen::Shared::ClientDB::LocalizedTextEditorRecord>(data, requestID, Artifact::LocalizedText);
        AppendEmptyArtifact<MetaGen::Shared::ClientDB::LocalizedTextTranslationEditorRecord>(data, requestID, Artifact::LocalizedTextTranslation);
        AppendEmptyArtifact<MetaGen::Shared::ClientDB::ConditionDescriptorEditorRecord>(data, requestID, Artifact::ConditionDescriptor);
        AppendEmptyArtifact<MetaGen::Shared::ClientDB::ConditionSetEditorRecord>(data, requestID, Artifact::ConditionSet);
        AppendEmptyArtifact<MetaGen::Shared::ClientDB::ConditionGroupEditorRecord>(data, requestID, Artifact::ConditionGroup);
        AppendEmptyArtifact<MetaGen::Shared::ClientDB::ConditionEditorRecord>(data, requestID, Artifact::Condition);
        AppendEmptyArtifact<MetaGen::Shared::ClientDB::GossipActionDescriptorEditorRecord>(data, requestID, Artifact::GossipActionDescriptor);
        AppendEmptyArtifact<MetaGen::Shared::ClientDB::GossipMenuEditorRecord>(data, requestID, Artifact::GossipMenu);
        AppendEmptyArtifact<MetaGen::Shared::ClientDB::GossipMenuOptionEditorRecord>(data, requestID, Artifact::GossipMenuOption);
        AppendEmptyArtifact<MetaGen::Shared::ClientDB::CreatureTemplateDescriptorEditorRecord>(data, requestID, Artifact::CreatureTemplateDescriptor);
        AppendEmptyArtifact<MetaGen::Shared::ClientDB::CreatureTemplateInteractionEditorRecord>(data, requestID, Artifact::CreatureTemplateInteraction);
        AppendEmptyArtifact<MetaGen::Shared::ClientDB::CreatureTemplateGossipEditorRecord>(data, requestID, Artifact::CreatureTemplateGossip);
        AppendEmptyArtifact<MetaGen::Shared::ClientDB::CostEditorRecord>(data, requestID, Artifact::Cost);
        AppendEmptyArtifact<MetaGen::Shared::ClientDB::CostCurrencyComponentEditorRecord>(data, requestID, Artifact::CostCurrencyComponent);
        AppendEmptyArtifact<MetaGen::Shared::ClientDB::CostItemComponentEditorRecord>(data, requestID, Artifact::CostItemComponent);
        AppendEmptyArtifact<MetaGen::Shared::ClientDB::VendorListEditorRecord>(data, requestID, Artifact::VendorList);
        AppendEmptyArtifact<MetaGen::Shared::ClientDB::VendorListItemEditorRecord>(data, requestID, Artifact::VendorListItem);
        AppendEmptyArtifact<MetaGen::Shared::ClientDB::CreatureTemplateVendorEditorRecord>(data, requestID, Artifact::CreatureTemplateVendor);
        AppendEmptyArtifact<MetaGen::Shared::ClientDB::QuestTemplateEditorRecord>(data, requestID, Artifact::QuestTemplate);
        AppendEmptyArtifact<MetaGen::Shared::ClientDB::QuestPrerequisiteEditorRecord>(data, requestID, Artifact::QuestPrerequisite);
        AppendEmptyArtifact<MetaGen::Shared::ClientDB::CreatureTemplateQuestGiverEditorRecord>(data, requestID, Artifact::CreatureTemplateQuestGiver);
        AppendEmptyArtifact<MetaGen::Shared::ClientDB::CreatureTemplateQuestEditorRecord>(data, requestID, Artifact::CreatureTemplateQuest);
        AppendEmptyArtifact<MetaGen::Shared::ClientDB::QuestObjectiveEditorRecord>(data, requestID, Artifact::QuestObjective);
        AppendEmptyArtifact<MetaGen::Shared::ClientDB::QuestObjectiveTargetEditorRecord>(data, requestID, Artifact::QuestObjectiveTarget);
        AppendEmptyArtifact<MetaGen::Shared::ClientDB::QuestRewardGroupEditorRecord>(data, requestID, Artifact::QuestRewardGroup);
        AppendEmptyArtifact<MetaGen::Shared::ClientDB::QuestRewardItemEditorRecord>(data, requestID, Artifact::QuestRewardItem);
        AppendEmptyArtifact<MetaGen::Shared::ClientDB::QuestRewardCurrencyEditorRecord>(data, requestID, Artifact::QuestRewardCurrency);
        AppendEmptyArtifact<MetaGen::Shared::ClientDB::QuestRewardReputationEditorRecord>(data, requestID, Artifact::QuestRewardReputation);
        AppendEmptyArtifact<MetaGen::Shared::ClientDB::QuestRewardExperienceEditorRecord>(data, requestID, Artifact::QuestRewardExperience);
        REQUIRE(data.CompleteSnapshot(requestID, true));
    }

    std::shared_ptr<Bytebuffer> MakeMenuPayload(std::string_view internalName, u32 greetingTextID, u32 flags)
    {
        std::shared_ptr<Bytebuffer> payload = Bytebuffer::BorrowRuntime(internalName.size() + 1 + sizeof(u32) * 2);
        REQUIRE(payload);
        REQUIRE(payload->PutString(internalName) > 0);
        REQUIRE(payload->PutU32(greetingTextID));
        REQUIRE(payload->PutU32(flags));
        return payload;
    }

    std::shared_ptr<Bytebuffer> MakeOptionPayload(u32 menuID, i32 priority, u32 textID, u8 actionType)
    {
        std::shared_ptr<Bytebuffer> payload = Bytebuffer::BorrowRuntime(128);
        REQUIRE(payload);
        REQUIRE(payload->PutU32(menuID));
        REQUIRE(payload->PutI32(priority));
        REQUIRE(payload->PutU32(textID));
        REQUIRE(payload->PutU16(4));
        REQUIRE(payload->PutU32(5));
        REQUIRE(payload->PutU32(0));
        REQUIRE(payload->PutU32(0));
        REQUIRE(payload->PutU32(0));
        REQUIRE(payload->PutU8(actionType));
        REQUIRE(payload->PutI64(101));
        REQUIRE(payload->PutI64(102));
        REQUIRE(payload->PutI64(103));
        REQUIRE(payload->PutI64(104));
        return payload;
    }

    std::shared_ptr<Bytebuffer> MakeTextPayload(std::string_view internalName, std::string_view englishValue, std::string_view translatorContext)
    {
        std::shared_ptr<Bytebuffer> payload = Bytebuffer::BorrowRuntime(internalName.size() + englishValue.size() + translatorContext.size() + 3);
        REQUIRE(payload);
        REQUIRE(payload->PutString(internalName) > 0);
        REQUIRE(payload->PutString(englishValue) > 0);
        REQUIRE(payload->PutString(translatorContext) > 0);
        return payload;
    }

    std::shared_ptr<Bytebuffer> MakeTranslationPayload(u32 textID, u8 locale, std::string_view value = {})
    {
        std::shared_ptr<Bytebuffer> payload = Bytebuffer::BorrowRuntime(sizeof(u32) + sizeof(u8) + value.size() + 1);
        REQUIRE(payload);
        REQUIRE(payload->PutU32(textID));
        REQUIRE(payload->PutU8(locale));
        if (!value.empty())
            REQUIRE(payload->PutString(value) > 0);
        return payload;
    }

    std::shared_ptr<Bytebuffer> MakeVendorItemPayload(u32 vendorListID, i32 priority, u32 itemTemplateID, u32 costID)
    {
        std::shared_ptr<Bytebuffer> payload = Bytebuffer::BorrowRuntime(64);
        REQUIRE(payload);
        REQUIRE(payload->PutU32(vendorListID));
        REQUIRE(payload->PutI32(priority));
        REQUIRE(payload->PutU32(itemTemplateID));
        REQUIRE(payload->PutU32(1));
        REQUIRE(payload->PutU32(costID));
        REQUIRE(payload->PutU32(0));
        REQUIRE(payload->PutU32(0));
        REQUIRE(payload->PutU32(0));
        REQUIRE(payload->PutU32(0));
        return payload;
    }

    std::shared_ptr<Bytebuffer> MakeQuestObjectivePayload(u32 questID, i32 priority, u8 objectiveType, u32 requiredCount, u32 textID)
    {
        std::shared_ptr<Bytebuffer> payload = Bytebuffer::BorrowRuntime(32);
        REQUIRE(payload);
        REQUIRE(payload->PutU32(questID));
        REQUIRE(payload->PutI32(priority));
        REQUIRE(payload->PutU8(objectiveType));
        REQUIRE(payload->PutU32(requiredCount));
        REQUIRE(payload->PutU32(textID));
        REQUIRE(payload->PutU32(0));
        return payload;
    }

    void AppendChange(Bytebuffer& changeSet, Artifact artifact, MutationType mutationType, u32 artifactID, const std::shared_ptr<Bytebuffer>& canonicalPayload = nullptr)
    {
        const u32 payloadLength = canonicalPayload ? static_cast<u32>(canonicalPayload->writtenData) : 0;
        REQUIRE(changeSet.PutU8(static_cast<u8>(artifact)));
        REQUIRE(changeSet.PutU8(static_cast<u8>(mutationType)));
        REQUIRE(changeSet.PutU32(artifactID));
        REQUIRE(changeSet.PutU32(payloadLength));
        if (payloadLength > 0)
            REQUIRE(changeSet.PutBytes(canonicalPayload->GetDataPointer(), payloadLength));
    }

    bool ReceiveOne(Editor::InteractionEditorData& data, u64 revision, Artifact artifact, MutationType mutationType, u32 artifactID, const std::shared_ptr<Bytebuffer>& canonicalPayload = nullptr)
    {
        std::shared_ptr<Bytebuffer> changeSet = Bytebuffer::BorrowRuntime(4096);
        REQUIRE(changeSet);
        AppendChange(*changeSet, artifact, mutationType, artifactID, canonicalPayload);
        return data.ReceiveChangeSet(revision, 1, changeSet->GetDataPointer(), changeSet->writtenData);
    }
}

TEST_CASE("Interaction editor applies canonical Gossip menu and option change sets", "[InteractionEditor]")
{
    Editor::InteractionEditorData data;
    LoadEmptyInteractionSnapshot(data);

    std::shared_ptr<Bytebuffer> menuCreate = MakeMenuPayload("FirstMenu", 10, 1);
    REQUIRE(ReceiveOne(data, 1, Artifact::GossipMenu, MutationType::Create, 42, menuCreate));
    ::ClientDB::Data* menus = data.GetStorage(Artifact::GossipMenu);
    auto* menu = menus->TryGet<MetaGen::Shared::ClientDB::GossipMenuEditorRecord>(42);
    REQUIRE(menu);
    CHECK(menus->GetString(menu->internalName) == "FirstMenu");
    CHECK(menu->greetingTextID == 10);

    std::shared_ptr<Bytebuffer> menuUpdate = MakeMenuPayload("RenamedMenu", 11, 2);
    REQUIRE(ReceiveOne(data, 2, Artifact::GossipMenu, MutationType::Update, 42, menuUpdate));
    menu = menus->TryGet<MetaGen::Shared::ClientDB::GossipMenuEditorRecord>(42);
    REQUIRE(menu);
    CHECK(menus->GetString(menu->internalName) == "RenamedMenu");
    CHECK(menu->flags == 2);

    std::shared_ptr<Bytebuffer> optionCreate = MakeOptionPayload(42, 3, 20, 6);
    REQUIRE(ReceiveOne(data, 3, Artifact::GossipMenuOption, MutationType::Create, 77, optionCreate));
    ::ClientDB::Data* options = data.GetStorage(Artifact::GossipMenuOption);
    auto* option = options->TryGet<MetaGen::Shared::ClientDB::GossipMenuOptionEditorRecord>(77);
    REQUIRE(option);
    CHECK(option->menuID == 42);
    CHECK(option->priority == 3);
    CHECK(option->actionParameters[3] == 104);

    std::shared_ptr<Bytebuffer> optionUpdate = MakeOptionPayload(42, 1, 21, 7);
    REQUIRE(ReceiveOne(data, 4, Artifact::GossipMenuOption, MutationType::Update, 77, optionUpdate));
    option = options->TryGet<MetaGen::Shared::ClientDB::GossipMenuOptionEditorRecord>(77);
    REQUIRE(option);
    CHECK(option->priority == 1);
    CHECK(option->textID == 21);

    REQUIRE(ReceiveOne(data, 5, Artifact::GossipMenuOption, MutationType::Delete, 77));
    CHECK(options->TryGet<MetaGen::Shared::ClientDB::GossipMenuOptionEditorRecord>(77) == nullptr);

    std::shared_ptr<Bytebuffer> secondOption = MakeOptionPayload(42, 0, 22, 8);
    REQUIRE(ReceiveOne(data, 6, Artifact::GossipMenuOption, MutationType::Create, 78, secondOption));
    std::shared_ptr<Bytebuffer> deleteSet = Bytebuffer::BorrowRuntime(128);
    REQUIRE(deleteSet);
    AppendChange(*deleteSet, Artifact::GossipMenu, MutationType::Delete, 42);
    AppendChange(*deleteSet, Artifact::GossipMenuOption, MutationType::Delete, 78);
    REQUIRE(data.ReceiveChangeSet(7, 2, deleteSet->GetDataPointer(), deleteSet->writtenData));
    CHECK(menus->TryGet<MetaGen::Shared::ClientDB::GossipMenuEditorRecord>(42) == nullptr);
    CHECK(options->TryGet<MetaGen::Shared::ClientDB::GossipMenuOptionEditorRecord>(78) == nullptr);
}

TEST_CASE("Interaction editor applies canonical localized text change sets", "[InteractionEditor]")
{
    Editor::InteractionEditorData data;
    LoadEmptyInteractionSnapshot(data);

    REQUIRE(ReceiveOne(data, 1, Artifact::LocalizedText, MutationType::Create, 20, MakeTextPayload("Greeting", "Hello", "NPC greeting")));
    ::ClientDB::Data* texts = data.GetStorage(Artifact::LocalizedText);
    auto* text = texts->TryGet<MetaGen::Shared::ClientDB::LocalizedTextEditorRecord>(20);
    REQUIRE(text);
    CHECK(texts->GetString(text->englishValue) == "Hello");

    REQUIRE(ReceiveOne(data, 2, Artifact::LocalizedText, MutationType::Update, 20, MakeTextPayload("Greeting", "Welcome", "Updated")));
    text = texts->TryGet<MetaGen::Shared::ClientDB::LocalizedTextEditorRecord>(20);
    REQUIRE(text);
    CHECK(texts->GetString(text->englishValue) == "Welcome");

    REQUIRE(ReceiveOne(data, 3, Artifact::LocalizedTextTranslation, MutationType::Create, 20, MakeTranslationPayload(20, 1, "Bonjour")));
    ::ClientDB::Data* translations = data.GetStorage(Artifact::LocalizedTextTranslation);
    u32 translationRowID = 0;
    translations->Each([&](u32 rowID, const MetaGen::Shared::ClientDB::LocalizedTextTranslationEditorRecord& record)
    {
        if (record.textID == 20 && record.locale == 1)
            translationRowID = rowID;
        return true;
    });
    REQUIRE(translationRowID != 0);

    REQUIRE(ReceiveOne(data, 4, Artifact::LocalizedTextTranslation, MutationType::Update, 20, MakeTranslationPayload(20, 1, "Salut")));
    auto* translation = translations->TryGet<MetaGen::Shared::ClientDB::LocalizedTextTranslationEditorRecord>(translationRowID);
    REQUIRE(translation);
    CHECK(translations->GetString(translation->value) == "Salut");

    std::shared_ptr<Bytebuffer> deleteSet = Bytebuffer::BorrowRuntime(128);
    REQUIRE(deleteSet);
    AppendChange(*deleteSet, Artifact::LocalizedText, MutationType::Delete, 20);
    AppendChange(*deleteSet, Artifact::LocalizedTextTranslation, MutationType::Delete, 20, MakeTranslationPayload(20, 1));
    REQUIRE(data.ReceiveChangeSet(5, 2, deleteSet->GetDataPointer(), deleteSet->writtenData));
    CHECK(texts->TryGet<MetaGen::Shared::ClientDB::LocalizedTextEditorRecord>(20) == nullptr);
    CHECK(translations->TryGet<MetaGen::Shared::ClientDB::LocalizedTextTranslationEditorRecord>(translationRowID) == nullptr);
}

TEST_CASE("Interaction editor applies vendor and quest authoring change sets", "[InteractionEditor]")
{
    Editor::InteractionEditorData data;
    LoadEmptyInteractionSnapshot(data);

    REQUIRE(ReceiveOne(data, 1, Artifact::VendorListItem, MutationType::Create, 40, MakeVendorItemPayload(7, 50, 900, 3)));
    auto* vendorItem = data.GetStorage(Artifact::VendorListItem)->TryGet<MetaGen::Shared::ClientDB::VendorListItemEditorRecord>(40);
    REQUIRE(vendorItem);
    CHECK(vendorItem->vendorListID == 7);
    CHECK(vendorItem->priority == 50);
    CHECK(vendorItem->costID == 3);

    REQUIRE(ReceiveOne(data, 2, Artifact::QuestObjective, MutationType::Create, 80, MakeQuestObjectivePayload(12, 25, 1, 8, 100)));
    auto* objective = data.GetStorage(Artifact::QuestObjective)->TryGet<MetaGen::Shared::ClientDB::QuestObjectiveEditorRecord>(80);
    REQUIRE(objective);
    CHECK(objective->questID == 12);
    CHECK(objective->requiredCount == 8);

    std::shared_ptr<Bytebuffer> deleteSet = Bytebuffer::BorrowRuntime(128);
    REQUIRE(deleteSet);
    AppendChange(*deleteSet, Artifact::VendorListItem, MutationType::Delete, 40);
    AppendChange(*deleteSet, Artifact::QuestObjective, MutationType::Delete, 80);
    REQUIRE(data.ReceiveChangeSet(3, 2, deleteSet->GetDataPointer(), deleteSet->writtenData));
    CHECK(data.GetStorage(Artifact::VendorListItem)->TryGet<MetaGen::Shared::ClientDB::VendorListItemEditorRecord>(40) == nullptr);
    CHECK(data.GetStorage(Artifact::QuestObjective)->TryGet<MetaGen::Shared::ClientDB::QuestObjectiveEditorRecord>(80) == nullptr);
}

TEST_CASE("Interaction editor rejects a malformed change set atomically", "[InteractionEditor]")
{
    Editor::InteractionEditorData data;
    LoadEmptyInteractionSnapshot(data);

    std::shared_ptr<Bytebuffer> changeSet = Bytebuffer::BorrowRuntime(256);
    REQUIRE(changeSet);
    AppendChange(*changeSet, Artifact::GossipMenu, MutationType::Create, 42, MakeMenuPayload("MustNotApply", 10, 0));
    REQUIRE(changeSet->PutU8(static_cast<u8>(Artifact::GossipMenu)));
    REQUIRE(changeSet->PutU8(static_cast<u8>(MutationType::Update)));
    REQUIRE(changeSet->PutU32(43));
    REQUIRE(changeSet->PutU32(8));
    REQUIRE(changeSet->PutU8(1));

    CHECK_FALSE(data.ReceiveChangeSet(1, 2, changeSet->GetDataPointer(), changeSet->writtenData));
    CHECK(data.state == Editor::DatabaseEditorDataState::Failed);
    CHECK(data.GetRevision() == 0);
    CHECK(data.GetStorage(Artifact::GossipMenu)->TryGet<MetaGen::Shared::ClientDB::GossipMenuEditorRecord>(42) == nullptr);
}
