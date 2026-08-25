#pragma once
#include "Game-Lib/Gameplay/Database/Item.h"

#include <Base/Types.h>

#include <Gameplay/GameDefine.h>

#include <MetaGen/Shared/ClientDB/ClientDB.h>
#include <MetaGen/Shared/Unit/Unit.h>

#include <entt/fwd.hpp>

namespace ECS
{
    struct UnitPower;
    struct UnitResistance;
    struct UnitStat;

    namespace Components
    {
        struct DisplayInfo;
        struct Model;
        struct UnitCustomization;
        struct UnitPowersComponent;
        struct UnitResistancesComponent;
        struct UnitStatsComponent;
    }

    namespace Singletons
    {
        struct ClientDBSingleton;
        struct UnitCustomizationSingleton;
    }
}

namespace Util::Unit
{
    bool HasPower(const ECS::Components::UnitPowersComponent& unitPowersComponent, MetaGen::Shared::Unit::PowerTypeEnum powerType);
    ECS::UnitPower& GetPower(ECS::Components::UnitPowersComponent& unitPowersComponent, MetaGen::Shared::Unit::PowerTypeEnum powerType);
    bool AddPower(ECS::Components::UnitPowersComponent& unitPowersComponent, MetaGen::Shared::Unit::PowerTypeEnum powerType, f64 base, f64 current, f64 max);
    bool SetPower(ECS::Components::UnitPowersComponent& unitPowersComponent, MetaGen::Shared::Unit::PowerTypeEnum powerType, f64 base, f64 current, f64 max);

    bool HasResistance(const ECS::Components::UnitResistancesComponent& unitResistancesComponent, MetaGen::Shared::Unit::ResistanceTypeEnum resistanceType);
    ECS::UnitResistance& GetResistance(ECS::Components::UnitResistancesComponent& unitResistancesComponent, MetaGen::Shared::Unit::ResistanceTypeEnum resistanceType);
    bool AddResistance(ECS::Components::UnitResistancesComponent& unitResistancesComponent, MetaGen::Shared::Unit::ResistanceTypeEnum resistanceType, f64 base, f64 current, f64 max);
    bool SetResistance(ECS::Components::UnitResistancesComponent& unitResistancesComponent, MetaGen::Shared::Unit::ResistanceTypeEnum resistanceType, f64 base, f64 current, f64 max);

    bool HasStat(const ECS::Components::UnitStatsComponent& unitStatsComponent, MetaGen::Shared::Unit::StatTypeEnum statType);
    ECS::UnitStat& GetStat(ECS::Components::UnitStatsComponent& unitStatsComponent, MetaGen::Shared::Unit::StatTypeEnum statType);
    bool AddStat(ECS::Components::UnitStatsComponent& unitStatsComponent, MetaGen::Shared::Unit::StatTypeEnum statType, f64 base, f64 current);
    bool SetStat(ECS::Components::UnitStatsComponent& unitStatsComponent, MetaGen::Shared::Unit::StatTypeEnum statType, f64 base, f64 current);

    void EnableGeometryGroup(entt::registry& registry, entt::entity entity, const ::ECS::Components::Model& model, u32 groupID);
    void DisableGeometryGroups(entt::registry& registry, entt::entity entity, const ::ECS::Components::Model& model, u32 startGroupID, u32 endGroupID = 0);
    void DisableAllGeometryGroups(entt::registry& registry, entt::entity entity, const ::ECS::Components::Model& model);
    void RefreshGeometryGroups(entt::registry& registry, entt::entity entity, ECS::Singletons::ClientDBSingleton& clientDBSingleton, ECS::Singletons::UnitCustomizationSingleton& unitCustomizationSingleton, const ::ECS::Components::Model& model);
    void RefreshSkinTexture(entt::registry& registry, entt::entity entity, ECS::Singletons::ClientDBSingleton& clientDBSingleton, ECS::Singletons::UnitCustomizationSingleton& unitCustomizationSingleton, const ::ECS::Components::DisplayInfo& displayInfo, ::ECS::Components::UnitCustomization& unitCustomization, const ::ECS::Components::Model& model);

}
