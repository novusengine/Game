#include "Spell.h"

#include "Game-Lib/ECS/Components/Unit.h"
#include "Game-Lib/ECS/Singletons/CharacterSingleton.h"
#include "Game-Lib/ECS/Singletons/NetworkState.h"
#include "Game-Lib/ECS/Singletons/Database/ClientDBSingleton.h"
#include "Game-Lib/ECS/Util/Network/NetworkUtil.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Meta/Generated/Shared/ClientDB.h>
#include <Meta/Generated/Shared/NetworkPacket.h>

#include <Scripting/Zenith.h>

#include <entt/entt.hpp>

namespace Scripting::Database
{

    void Spell::Register(Zenith* zenith)
    {
        LuaMethodTable::Set(zenith, spellGlobalFunctions, "Spell");
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

            const auto& spellInfo = db->Get<Generated::SpellRecord>(spellID);

            // Name, Icon, Description RequiredText, SpecialText
            const std::string& name = db->GetString(spellInfo.name);
            const std::string& description = db->GetString(spellInfo.description);
            const std::string& auraDescription = db->GetString(spellInfo.auraDescription);

            zenith->CreateTable();
            zenith->AddTableField("Name", name.c_str());
            zenith->AddTableField("Description", description.c_str());
            zenith->AddTableField("AuraDescription", auraDescription.c_str());
            zenith->AddTableField("IconID", spellInfo.iconID);

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

            const auto& icon = db->Get<Generated::IconRecord>(iconID);
            const std::string& texture = db->GetString(icon.texture);

            zenith->CreateTable();
            zenith->AddTableField("Texture", texture.c_str());
            return 1;
        }

        i32 CastByID(Zenith* zenith)
        {
            u32 spellID = zenith->CheckVal<u32>(1);

            entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
            auto& characterSingleton = registry->ctx().get<ECS::Singletons::CharacterSingleton>();
            auto& networkState = registry->ctx().get<ECS::Singletons::NetworkState>();

            if (!ECS::Util::Network::IsConnected(networkState))
                return 0;

            ECS::Util::Network::SendPacket(networkState, Generated::ClientSpellCastPacket{
                .spellID = spellID
            });

            return 0;
        }
    }
}