#include "SpellUtil.h"

#include "Game-Lib/ECS/Singletons/Database/ClientDBSingleton.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <MetaGen/Shared/ClientDB/ClientDB.h>

#include <entt/entt.hpp>

namespace ECSUtil::Spell
{
    bool Refresh()
    {
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->dbRegistry;
        auto& ctx = registry->ctx();

        if (!ctx.contains<ECS::Singletons::SpellSingleton>())
            ctx.emplace<ECS::Singletons::SpellSingleton>();

        auto& spellSingleton = ctx.get<ECS::Singletons::SpellSingleton>();
        
        spellSingleton.spellIDToEffectList.clear();

        auto& clientDBSingleton = ctx.get<ECS::Singletons::ClientDBSingleton>();

        if (clientDBSingleton.Register<MetaGen::Shared::ClientDB::SpellRecord>())
        {
            auto* storage = clientDBSingleton.Get(ClientDBHash::Spell);

            MetaGen::Shared::ClientDB::SpellRecord defaultSpell;
            defaultSpell.name = storage->AddString("Unused");
            defaultSpell.description = storage->AddString("Unused");
            defaultSpell.auraDescription = storage->AddString("Unused");

            storage->Replace(0, defaultSpell);
            storage->MarkDirty();
        }

        if (clientDBSingleton.Register<MetaGen::Shared::ClientDB::SpellEffectsRecord>())
        {
            auto* storage = clientDBSingleton.Get(ClientDBHash::SpellEffects);

            MetaGen::Shared::ClientDB::SpellEffectsRecord defaultSpellEffect;
            defaultSpellEffect.spellID = 0;
            defaultSpellEffect.effectPriority = 0;
            defaultSpellEffect.effectType = 0;
            defaultSpellEffect.effectValues = { 0 };
            defaultSpellEffect.effectMiscValues = { 0 };

            storage->Replace(0, defaultSpellEffect);
        }

        if (clientDBSingleton.Register<MetaGen::Shared::ClientDB::SpellProcDataRecord>())
        {
            auto* storage = clientDBSingleton.Get(ClientDBHash::SpellProcData);

            MetaGen::Shared::ClientDB::SpellProcDataRecord defaultSpellProcData;
            defaultSpellProcData.phaseMask = 0;
            defaultSpellProcData.typeMask = 0;
            defaultSpellProcData.hitMask = 0;
            defaultSpellProcData.flags = std::numeric_limits<u64>().max();
            defaultSpellProcData.procsPerMinute = 0.0f;
            defaultSpellProcData.chanceToProc = 0.0f;
            defaultSpellProcData.internalCooldownMS = std::numeric_limits<u32>().max();
            defaultSpellProcData.charges = 0;

            storage->Replace(0, defaultSpellProcData);
        }

        if (clientDBSingleton.Register<MetaGen::Shared::ClientDB::SpellProcLinkRecord>())
        {
            auto* storage = clientDBSingleton.Get(ClientDBHash::SpellProcLink);

            MetaGen::Shared::ClientDB::SpellProcLinkRecord defaultSpellProcLink;
            defaultSpellProcLink.spellID = 0;
            defaultSpellProcLink.effectMask = 0;
            defaultSpellProcLink.procDataID = 0;

            storage->Replace(0, defaultSpellProcLink);
        }

        auto* spellStorage = clientDBSingleton.Get(ClientDBHash::Spell);
        auto* spellEffectsStorage = clientDBSingleton.Get(ClientDBHash::SpellEffects);

        spellEffectsStorage->Each([&](u32 id, MetaGen::Shared::ClientDB::SpellEffectsRecord& spellEffect) -> bool
        {
            AddSpellEffect(spellSingleton, spellEffect.spellID, id);
            return true;
        });

        for (auto& pair : spellSingleton.spellIDToEffectList)
        {
            SortSpellEffects(spellSingleton, spellEffectsStorage, pair.first);
        }

        return true;
    }

    void AddSpellEffect(ECS::Singletons::SpellSingleton& spellSingleton, u32 spellID, u32 spellEffectID)
    {
        spellSingleton.spellIDToEffectList[spellID].push_back(spellEffectID);
    }

    void RemoveSpellEffect(ECS::Singletons::SpellSingleton& spellSingleton, u32 spellID, u32 spellEffectIndex)
    {
        if (!spellSingleton.spellIDToEffectList.contains(spellID))
            return;

        auto& effectList = spellSingleton.spellIDToEffectList.at(spellID);
        if (spellEffectIndex >= effectList.size())
            return;

        effectList.erase(effectList.begin() + spellEffectIndex);
    }

    void SortSpellEffects(ECS::Singletons::SpellSingleton& spellSingleton, ClientDB::Data* spellEffectsStorage, u32 spellID)
    {
        if (!spellSingleton.spellIDToEffectList.contains(spellID))
            return;

        auto& effectList = spellSingleton.spellIDToEffectList[spellID];
        std::ranges::sort(effectList, [&spellEffectsStorage](const u32 spellEffectIDA, const u32 spellEffectIDB)
        {
            const auto& spellEffectA = spellEffectsStorage->Get<MetaGen::Shared::ClientDB::SpellEffectsRecord>(spellEffectIDA);
            const auto& spellEffectB = spellEffectsStorage->Get<MetaGen::Shared::ClientDB::SpellEffectsRecord>(spellEffectIDB);

            return spellEffectA.effectPriority > spellEffectB.effectPriority;
        });
    }

    const std::vector<u32>* GetSpellEffectList(const ECS::Singletons::SpellSingleton& spellSingleton, u32 spellID)
    {
        if (!spellSingleton.spellIDToEffectList.contains(spellID))
            return nullptr;

        return &spellSingleton.spellIDToEffectList.at(spellID);
    }
}