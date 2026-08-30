#pragma once

#include <Gameplay/GameDefine.h>

#include <MetaGen/Shared/Interaction/Interaction.h>

#include <robinhood/robinhood.h>

#include <optional>
#include <string>
#include <vector>

namespace ECS::Singletons
{
    struct InteractionOptionState
    {
    public:
        struct CostCurrencyComponent
        {
        public:
            u16 currencyID = 0;
            u64 amount = 0;
        };

        struct CostItemComponent
        {
        public:
            u32 itemTemplateID = 0;
            u32 amount = 0;
        };

        u64 token = 0;
        u16 icon = 0;
        MetaGen::Shared::Interaction::InteractionOptionKindEnum kind = MetaGen::Shared::Interaction::InteractionOptionKindEnum::Generic;
        bool enabled = false;
        u32 itemTemplateID = 0;
        u32 quantity = 0;
        u32 costID = 0;
        bool costDataAvailable = true;
        std::vector<CostCurrencyComponent> costCurrencyComponents;
        std::vector<CostItemComponent> costItemComponents;
        std::string text;
        std::string disabledReason;
    };

    struct InteractionSessionState
    {
    public:
        struct QuestObjective
        {
        public:
            std::string text;
            std::string displayText;
            u32 progress = 0;
            u32 required = 0;
            bool optional = false;
        };

        struct QuestReward
        {
        public:
            u32 groupID = 0;
            u32 selectionID = 0;
            MetaGen::Shared::Interaction::QuestRewardTypeEnum type = MetaGen::Shared::Interaction::QuestRewardTypeEnum::Item;
            u32 resourceID = 0;
            u64 amount = 0;
            bool amountNegative = false;
            u8 selectionCount = 0;
            u32 flags = 0;
        };

        u64 id = 0;
        u32 revision = 0;
        ObjectGUID sourceGUID = ObjectGUID::Empty;
        MetaGen::Shared::Interaction::InteractionSurfaceTypeEnum surfaceType = MetaGen::Shared::Interaction::InteractionSurfaceTypeEnum::Menu;
        MetaGen::Shared::Interaction::QuestInteractionMaskEnum questMask = MetaGen::Shared::Interaction::QuestInteractionMaskEnum::None;
        std::string heading;
        std::string greeting;
        std::vector<InteractionOptionState> options;
        std::vector<QuestObjective> objectives;
        std::vector<QuestReward> rewards;
        robin_hood::unordered_flat_set<u32> selectedRewardSelectionIDs;
    };

    struct InteractionState
    {
    public:
        struct QuestLogObjective
        {
        public:
            u32 id = 0;
            std::string text;
            std::string displayText;
            u32 progress = 0;
            u32 required = 0;
            bool optional = false;
        };

        struct QuestLogEntry
        {
        public:
            u32 id = 0;
            MetaGen::Shared::Interaction::CharacterQuestStateEnum state = MetaGen::Shared::Interaction::CharacterQuestStateEnum::Active;
            std::string title;
            std::string logText;
            std::vector<QuestLogObjective> objectives;
        };

        void Reset()
        {
            activeSession.reset();
            lastClosedSessionID = 0;
            lastCloseReason = MetaGen::Shared::Interaction::InteractionCloseReasonEnum::Count;
            lastResultSessionID = 0;
            lastResultRevision = 0;
            lastResult = MetaGen::Shared::Interaction::InteractionResultEnum::Count;
            lastQuestCommandQuestID = 0;
            lastQuestCommandResult = MetaGen::Shared::Interaction::InteractionResultEnum::Count;
            questLogRevision = 0;
            questLog.clear();
            Touch();
        }

        void Touch()
        {
            changeVersion++;
            if (changeVersion == 0)
                changeVersion = 1;
        }

    public:
        u64 changeVersion = 1;
        std::optional<InteractionSessionState> activeSession;
        u64 lastClosedSessionID = 0;
        MetaGen::Shared::Interaction::InteractionCloseReasonEnum lastCloseReason = MetaGen::Shared::Interaction::InteractionCloseReasonEnum::Count;
        u64 lastResultSessionID = 0;
        u32 lastResultRevision = 0;
        MetaGen::Shared::Interaction::InteractionResultEnum lastResult = MetaGen::Shared::Interaction::InteractionResultEnum::Count;
        u32 lastQuestCommandQuestID = 0;
        MetaGen::Shared::Interaction::InteractionResultEnum lastQuestCommandResult = MetaGen::Shared::Interaction::InteractionResultEnum::Count;
        u64 questLogRevision = 0;
        std::vector<QuestLogEntry> questLog;
    };
}
