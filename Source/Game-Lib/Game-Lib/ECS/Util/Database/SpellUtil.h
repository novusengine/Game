#pragma once
#include "Game-Lib/ECS/Singletons/Database/SpellSingleton.h"

#include <Base/Types.h>

#include <Gameplay/GameDefine.h>

namespace ClientDB
{
    struct Data;
}

namespace ECSUtil::Spell
{
    bool Refresh();

    void AddSpellEffect(ECS::Singletons::SpellSingleton& spellSingleton, u32 spellID, u32 spellEffectID);
    void RemoveSpellEffect(ECS::Singletons::SpellSingleton& spellSingleton, u32 spellID, u32 spellEffectIndex);
    void SortSpellEffects(ECS::Singletons::SpellSingleton& spellSingleton, ClientDB::Data* spellEffectsStorage, u32 spellID);
    const std::vector<u32>* GetSpellEffectList(const ECS::Singletons::SpellSingleton& spellSingleton, u32 spellID);
}