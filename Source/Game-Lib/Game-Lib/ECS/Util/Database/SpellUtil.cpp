#include "SpellUtil.h"

#include "Game-Lib/ECS/Singletons/Database/ClientDBSingleton.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Meta/Generated/Shared/ClientDB.h>

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

        if (!clientDBSingleton.Has(ClientDBHash::Spell))
        {
            clientDBSingleton.Register(ClientDBHash::Spell, Generated::SpellEffectsRecord::Name);

            auto* storage = clientDBSingleton.Get(ClientDBHash::Spell);
            storage->Initialize<Generated::SpellRecord>();

            Generated::SpellRecord defaultSpell;
            defaultSpell.name = storage->AddString("Unused");
            defaultSpell.description = storage->AddString("Unused");
            defaultSpell.auraDescription = storage->AddString("Unused");

            storage->Replace(0, defaultSpell);
            storage->MarkDirty();
        }

        if (!clientDBSingleton.Has(ClientDBHash::SpellEffects))
        {
            clientDBSingleton.Register<Generated::SpellEffectsRecord>();
            auto* storage = clientDBSingleton.Get(ClientDBHash::SpellEffects);

            Generated::SpellEffectsRecord defaultSpellEffect;
            defaultSpellEffect.spellID = 0;
            defaultSpellEffect.effectPriority = 0;
            defaultSpellEffect.effectType = 0;
            defaultSpellEffect.effectValue1 = 0;
            defaultSpellEffect.effectValue2 = 0;
            defaultSpellEffect.effectValue3 = 0;
            defaultSpellEffect.effectMiscValue1 = 0;
            defaultSpellEffect.effectMiscValue2 = 0;
            defaultSpellEffect.effectMiscValue3 = 0;

            storage->Replace(0, defaultSpellEffect);
        }

        auto* spellStorage = clientDBSingleton.Get(ClientDBHash::Spell);
        auto* spellEffectsStorage = clientDBSingleton.Get(ClientDBHash::SpellEffects);

        spellEffectsStorage->Each([&](u32 id, Generated::SpellEffectsRecord& spellEffect) -> bool
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
            const auto& spellEffectA = spellEffectsStorage->Get<Generated::SpellEffectsRecord>(spellEffectIDA);
            const auto& spellEffectB = spellEffectsStorage->Get<Generated::SpellEffectsRecord>(spellEffectIDB);

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