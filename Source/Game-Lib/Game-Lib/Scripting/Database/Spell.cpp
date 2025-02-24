#include "Spell.h"

#include "Game-Lib/ECS/Singletons/Database/ClientDBSingleton.h"
#include "Game-Lib/Gameplay/Database/Spell.h"
#include "Game-Lib/Scripting/LuaMethodTable.h"
#include "Game-Lib/Scripting/LuaState.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <FileFormat/Novus/ClientDB/Definitions.h>

#include <entt/entt.hpp>

namespace Scripting::Database
{
    static LuaMethod spellStaticFunctions[] =
    {
        { "GetSpellInfo", SpellMethods::GetSpellInfo }
    };

    void Spell::Register(lua_State* state)
    {
        LuaMethodTable::Set(state, spellStaticFunctions, "Spell");
    }

    namespace SpellMethods
    {
        i32 GetSpellInfo(lua_State* state)
        {
            LuaState ctx(state);

            i32 spellID = ctx.Get(0);

            entt::registry* registry = ServiceLocator::GetEnttRegistries()->dbRegistry;
            auto& clientDBSingleton = registry->ctx().get<ECS::Singletons::Database::ClientDBSingleton>();
            if (!clientDBSingleton.Has(ClientDBHash::Spell))
                return 0;

            auto* db = clientDBSingleton.Get(ClientDBHash::Spell);
            if (!db->Has(spellID))
                spellID = 0;

            const auto& spellInfo = db->Get<::Database::Spell::Spell>(spellID);

            // Name, Icon, Description RequiredText, SpecialText
            const std::string& name = db->GetString(spellInfo.name);
            const std::string& description = db->GetString(spellInfo.description);
            const std::string& auraDescription = db->GetString(spellInfo.auraDescription);

            ctx.CreateTableAndPopulate(nullptr, [&ctx, &name, &description, &auraDescription]()
            {
                ctx.SetTable("Name", name.c_str());
                ctx.SetTable("Description", description.c_str());
                ctx.SetTable("AuraDescription", auraDescription.c_str());
            });

            return 1;
        }
    }
}