#include "Item.h"

#include "Game-Lib/ECS/Singletons/ClientDBCollection.h"
#include "Game-Lib/Scripting/LuaMethodTable.h"
#include "Game-Lib/Scripting/LuaState.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <FileFormat/Novus/ClientDB/Definitions.h>

#include <entt/entt.hpp>

namespace Scripting::Database
{
    static LuaMethod itemStaticFunctions[] =
    {
        { "GetItemInfo", ItemMethods::GetItemInfo },

        { nullptr, nullptr }
    };

    static LuaMethod cdbMethods[] =
    {
        { nullptr, nullptr }
    };

    void Item::Register(lua_State* state)
    {
        LuaMethodTable::Set(state, itemStaticFunctions, "Item");
    }

    namespace ItemMethods
    {
        i32 GetItemInfo(lua_State* state)
        {
            LuaState ctx(state);

            i32 itemID = ctx.Get(0);

            entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
            auto& clientDBCollection = registry->ctx().get<ECS::Singletons::ClientDBCollection>();
            if (!clientDBCollection.Has(ECS::Singletons::ClientDBHash::Item))
                return 0;

            auto* db = clientDBCollection.Get(ECS::Singletons::ClientDBHash::Item);
            if (!db->Has(itemID))
                itemID = 0;

            struct ItemInfo
            {
            public:
                u32 displayID;
                u8 flags;
                u8 rarity;
                u8 category;
                u8 type;
                u16 virtualLevel;
                u16 requiredLevel;
                u32 name;
                u32 description;
                u32 requiredText;
                u32 specialText;
                u32 customInts[10];
                f32 customFloats[5];
            };

            const auto& itemInfo = db->Get<ItemInfo>(itemID);

            // Name, Description RequiredText, SpecialText
            const std::string& name = db->GetString(itemInfo.name);
            const std::string& description = db->GetString(itemInfo.description);
            const std::string& requiredText = db->GetString(itemInfo.requiredText);
            const std::string& specialText = db->GetString(itemInfo.specialText);

            ctx.CreateTableAndPopulate(nullptr, [&ctx, &itemInfo, &name, &description, &requiredText, &specialText]()
            {
                ctx.SetTable("DisplayID", itemInfo.displayID);
                ctx.SetTable("Flags", itemInfo.flags);
                ctx.SetTable("Rarity", itemInfo.rarity);
                ctx.SetTable("Category", itemInfo.category);
                ctx.SetTable("Type", itemInfo.type);
                ctx.SetTable("VirtualLevel", itemInfo.virtualLevel);
                ctx.SetTable("RequiredLevel", itemInfo.requiredLevel);

                ctx.SetTable("Name", name.c_str());
                ctx.SetTable("Description", description.c_str());
                ctx.SetTable("RequiredText", requiredText.c_str());
                ctx.SetTable("SpecialText", specialText.c_str());

                ctx.SetTable("CustomInt1", itemInfo.customInts[0]);
                ctx.SetTable("CustomInt2", itemInfo.customInts[1]);
                ctx.SetTable("CustomInt3", itemInfo.customInts[2]);
                ctx.SetTable("CustomInt4", itemInfo.customInts[3]);
                ctx.SetTable("CustomInt5", itemInfo.customInts[4]);
                ctx.SetTable("CustomInt6", itemInfo.customInts[5]);
                ctx.SetTable("CustomInt7", itemInfo.customInts[6]);
                ctx.SetTable("CustomInt8", itemInfo.customInts[7]);
                ctx.SetTable("CustomInt9", itemInfo.customInts[8]);
                ctx.SetTable("CustomInt10", itemInfo.customInts[9]);

                ctx.SetTable("CustomFloat1", itemInfo.customFloats[0]);
                ctx.SetTable("CustomFloat2", itemInfo.customFloats[1]);
                ctx.SetTable("CustomFloat3", itemInfo.customFloats[2]);
                ctx.SetTable("CustomFloat4", itemInfo.customFloats[3]);
                ctx.SetTable("CustomFloat5", itemInfo.customFloats[4]);
            });

            return 1;
        }
    }
}