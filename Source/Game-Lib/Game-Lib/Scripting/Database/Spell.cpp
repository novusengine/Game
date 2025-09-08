#include "Spell.h"

#include "Game-Lib/ECS/Singletons/Database/ClientDBSingleton.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Meta/Generated/Shared/ClientDB.h>

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

            return 1;
        }
    }
}