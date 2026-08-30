#include "Spell.h"

#include "Game-Lib/ECS/Components/Unit.h"
#include "Game-Lib/ECS/Singletons/CharacterSingleton.h"
#include "Game-Lib/ECS/Singletons/NetworkState.h"
#include "Game-Lib/ECS/Singletons/Database/ClientDBSingleton.h"
#include "Game-Lib/ECS/Util/Network/NetworkUtil.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <MetaGen/Shared/ClientDB/ClientDB.h>
#include <MetaGen/Shared/Packet/Packet.h>
#include <MetaGen/Shared/Spell/Spell.h>

#include <Scripting/Zenith.h>

#include <entt/entt.hpp>

#include <cmath>

namespace Scripting::Database
{
    namespace
    {
        const MetaGen::Shared::ClientDB::SpellRecord* GetSpellRecord(u32 spellID)
        {
            entt::registry* registry = ServiceLocator::GetEnttRegistries()->dbRegistry;
            auto& clientDBSingleton = registry->ctx().get<ECS::Singletons::ClientDBSingleton>();
            if (!clientDBSingleton.Has(ClientDBHash::Spell))
                return nullptr;

            auto* spellStorage = clientDBSingleton.Get(ClientDBHash::Spell);
            return spellStorage->Has(spellID)
                ? &spellStorage->Get<MetaGen::Shared::ClientDB::SpellRecord>(spellID)
                : nullptr;
        }

        ObjectGUID GetSelectedTarget(entt::registry& registry, entt::entity casterEntity)
        {
            const auto* caster = registry.try_get<ECS::Components::Unit>(casterEntity);
            const auto* target = caster ? registry.try_get<ECS::Components::Unit>(caster->targetEntity) : nullptr;
            return target ? target->networkID : ObjectGUID::Empty;
        }

        bool SendSpellCast(u32 spellID, ObjectGUID targetGUID, const vec3& targetPosition)
        {
            entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
            auto& networkState = registry->ctx().get<ECS::Singletons::NetworkState>();
            return ECS::Util::Network::SendPacket(networkState, MetaGen::Shared::Packet::ClientSpellCastPacket{
                .spellID = spellID,
                .targetGUID = targetGUID,
                .targetPosition = targetPosition
            });
        }
    }

    void Spell::Register(Zenith* zenith)
    {
        LuaMethodTable::Set(zenith, spellGlobalFunctions, "Spell");

        zenith->CreateTable(MetaGen::Shared::Spell::AuraDispositionEnumMeta::ENUM_NAME.data());
        for (const auto& pair : MetaGen::Shared::Spell::AuraDispositionEnumMeta::ENUM_FIELD_LIST)
        {
            zenith->AddTableField(pair.first.data(), pair.second);
        }
        zenith->Pop();
    }

    namespace SpellMethods
    {
        i32 GetSpellInfo(Zenith* zenith)
        {
            u32 spellID = zenith->CheckVal<u32>(1);

            entt::registry* registry = ServiceLocator::GetEnttRegistries()->dbRegistry;
            auto& clientDBSingleton = registry->ctx().get<ECS::Singletons::ClientDBSingleton>();
            if (!clientDBSingleton.Has(ClientDBHash::Spell))
                return 0;

            auto* db = clientDBSingleton.Get(ClientDBHash::Spell);
            if (!db->Has(spellID))
                spellID = 0;

            const auto& spellInfo = db->Get<MetaGen::Shared::ClientDB::SpellRecord>(spellID);

            // Name, Icon, Description RequiredText, SpecialText
            const std::string& name = db->GetString(spellInfo.name);
            const std::string& description = db->GetString(spellInfo.description);
            const std::string& auraDescription = db->GetString(spellInfo.auraDescription);

            zenith->CreateTable();
            zenith->AddTableField("Name", name.c_str());
            zenith->AddTableField("Description", description.c_str());
            zenith->AddTableField("AuraDescription", auraDescription.c_str());
            zenith->AddTableField("IconID", spellInfo.iconID);

            bool isAura = false;
            u8 auraDisposition = static_cast<u8>(MetaGen::Shared::Spell::AuraDispositionEnum::None);
            u8 auraDispelType = static_cast<u8>(MetaGen::Shared::Spell::AuraDispelTypeEnum::None);
            u16 maximumStacks = 0;
            if (clientDBSingleton.Has(ClientDBHash::SpellAura))
            {
                auto* auraDB = clientDBSingleton.Get(ClientDBHash::SpellAura);
                if (auraDB->Has(spellID))
                {
                    const auto& auraInfo = auraDB->Get<MetaGen::Shared::ClientDB::SpellAuraRecord>(spellID);
                    isAura = true;
                    auraDisposition = auraInfo.disposition;
                    auraDispelType = auraInfo.dispelType;
                    maximumStacks = auraInfo.maximumStacks;
                }
            }

            zenith->AddTableField("IsAura", isAura);
            zenith->AddTableField("AuraDisposition", auraDisposition);
            zenith->AddTableField("AuraDispelType", auraDispelType);
            zenith->AddTableField("MaximumStacks", maximumStacks);
            zenith->AddTableField("TargetSelector", spellInfo.targetSelector);
            zenith->AddTableField("TargetShape", spellInfo.targetShape);
            zenith->AddTableField("TargetRelation", spellInfo.targetRelation);
            zenith->AddTableField("TargetRecipientMask", spellInfo.targetRecipientMask);
            zenith->AddTableField("RangePolicy", spellInfo.rangePolicy);
            zenith->AddTableField("MinimumRange", spellInfo.minimumRange);
            zenith->AddTableField("MaximumRange", spellInfo.maximumRange);
            zenith->AddTableField("TargetRadius", spellInfo.targetRadius);
            zenith->AddTableField("MaximumTargets", spellInfo.maximumTargets);

            return 1;
        }

        i32 GetIconInfo(Zenith* zenith)
        {
            u32 iconID = zenith->CheckVal<u32>(1);

            entt::registry* registry = ServiceLocator::GetEnttRegistries()->dbRegistry;
            auto& clientDBSingleton = registry->ctx().get<ECS::Singletons::ClientDBSingleton>();
            if (!clientDBSingleton.Has(ClientDBHash::Icon))
                return 0;

            auto* db = clientDBSingleton.Get(ClientDBHash::Icon);
            if (!db->Has(iconID))
                iconID = 0;

            const auto& icon = db->Get<MetaGen::Shared::ClientDB::IconRecord>(iconID);
            const std::string& texture = db->GetString(icon.texture);

            zenith->CreateTable();
            zenith->AddTableField("Texture", texture.c_str());
            return 1;
        }

        i32 CastByID(Zenith* zenith)
        {
            const u32 spellID = zenith->CheckVal<u32>(1);
            const auto* spell = GetSpellRecord(spellID);
            if (!spell || static_cast<MetaGen::Shared::Spell::SpellTargetSelectorEnum>(spell->targetSelector) ==
                              MetaGen::Shared::Spell::SpellTargetSelectorEnum::GroundPosition)
            {
                zenith->Push(false);
                return 1;
            }

            entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
            auto& characterSingleton = registry->ctx().get<ECS::Singletons::CharacterSingleton>();
            zenith->Push(SendSpellCast(spellID, GetSelectedTarget(*registry, characterSingleton.moverEntity), vec3(0.0f)));
            return 1;
        }

        i32 CastAtPosition(Zenith* zenith)
        {
            const u32 spellID = zenith->CheckVal<u32>(1);
            const vec3 targetPosition = zenith->CheckVal<vec3>(2);
            const auto* spell = GetSpellRecord(spellID);
            const bool validPosition = std::isfinite(targetPosition.x) && std::isfinite(targetPosition.y) && std::isfinite(targetPosition.z);
            if (!spell || !validPosition ||
                static_cast<MetaGen::Shared::Spell::SpellTargetSelectorEnum>(spell->targetSelector) !=
                    MetaGen::Shared::Spell::SpellTargetSelectorEnum::GroundPosition)
            {
                zenith->Push(false);
                return 1;
            }

            zenith->Push(SendSpellCast(spellID, ObjectGUID::Empty, targetPosition));
            return 1;
        }
    }
}
