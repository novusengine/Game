#include "EditorToolHandler.h"
#include "Game-Lib/Application/EnttRegistries.h"
#include "Game-Lib/ECS/Singletons/EditorSelection.h"
#include "Game-Lib/Scripting/Util/ZenithUtil.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <MetaGen/Game/Lua/Lua.h>

#include <Scripting/LuaManager.h>
#include <Scripting/Zenith.h>

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <lualib.h>

namespace Scripting::Editor
{
    void EditorToolHandler::Register(Zenith* zenith)
    {
        LuaManager* luaManager = ServiceLocator::GetLuaManager();
        const bool inDeveloperMode = luaManager && luaManager->IsDeveloperMode();
        const Scripting::LuaMethodFlags excludeFlags = inDeveloperMode
            ? Scripting::LuaMethodFlags::None
            : Scripting::LuaMethodFlags::DeveloperOnly;

        LuaMethodTable::Set(zenith, editorGlobalMethods, "Editor", excludeFlags);

        _onSelectionChangedRef = LUA_NOREF;
    }

    void EditorToolHandler::Clear(Zenith* zenith)
    {
        _onSelectionChangedRef = LUA_NOREF;
        _onGizmoChangedRef = LUA_NOREF;
    }

    static EditorToolHandler* GetSelf()
    {
        LuaManager* luaManager = ServiceLocator::GetLuaManager();
        if (!luaManager)
            return nullptr;
        return luaManager->GetLuaHandler<EditorToolHandler>(static_cast<LuaHandlerID>(MetaGen::Game::Lua::LuaHandlerTypeEnum::Editor));
    }

    static ECS::Singletons::EditorSelection* GetSelection()
    {
        EnttRegistries* registries = ServiceLocator::GetEnttRegistries();
        if (!registries || !registries->gameRegistry)
            return nullptr;
        auto& ctx = registries->gameRegistry->ctx();
        return &ctx.get<ECS::Singletons::EditorSelection>();
    }

    i32 EditorToolHandler::GetSelected(Zenith* zenith)
    {
        ECS::Singletons::EditorSelection* selection = GetSelection();
        if (!selection || selection->selectedEntity == entt::null)
        {
            zenith->Push();
            return 1;
        }

        zenith->Push(entt::to_integral(selection->selectedEntity));
        return 1;
    }

    i32 EditorToolHandler::SetSelected(Zenith* zenith)
    {
        ECS::Singletons::EditorSelection* selection = GetSelection();
        if (!selection)
            return 0;

        EnttRegistries* registries = ServiceLocator::GetEnttRegistries();
        entt::registry* registry = registries ? registries->gameRegistry : nullptr;

        entt::entity newSelection = entt::null;
        if (zenith->IsNumber(1) && registry)
        {
            entt::entity candidate = entt::entity(zenith->CheckVal<u32>(1));
            if (registry->valid(candidate))
                newSelection = candidate;
        }

        if (selection->selectedEntity == newSelection)
            return 0;

        selection->selectedEntity = newSelection;

        EditorToolHandler* self = GetSelf();
        if (self)
            self->OnSelectionChanged(zenith);

        return 0;
    }

    i32 EditorToolHandler::SetOnSelectionChanged(Zenith* zenith)
    {
        EditorToolHandler* self = GetSelf();
        if (!self)
            return 0;

        Scripting::Util::Zenith::Unref(zenith, self->_onSelectionChangedRef);
        self->_onSelectionChangedRef = LUA_NOREF;

        if (zenith->IsFunction(1))
        {
            self->_onSelectionChangedRef = zenith->GetRef(1);
        }
        return 0;
    }

    i32 EditorToolHandler::SetOnGizmoChanged(Zenith* zenith)
    {
        EditorToolHandler* self = GetSelf();
        if (!self)
            return 0;

        Scripting::Util::Zenith::Unref(zenith, self->_onGizmoChangedRef);
        self->_onGizmoChangedRef = LUA_NOREF;

        if (zenith->IsFunction(1))
        {
            self->_onGizmoChangedRef = zenith->GetRef(1);
        }
        return 0;
    }

    i32 EditorToolHandler::SetPickingEnabled(Zenith* zenith)
    {
        ECS::Singletons::EditorSelection* selection = GetSelection();
        if (selection)
            selection->pickingEnabled = zenith->CheckVal<bool>(1);
        return 0;
    }

    i32 EditorToolHandler::SetGizmoEnabled(Zenith* zenith)
    {
        ECS::Singletons::EditorSelection* selection = GetSelection();
        if (selection)
            selection->gizmoEnabled = zenith->CheckVal<bool>(1);
        return 0;
    }

    i32 EditorToolHandler::GetGizmoOperation(Zenith* zenith)
    {
        ECS::Singletons::EditorSelection* selection = GetSelection();
        zenith->Push(selection ? static_cast<u32>(selection->gizmoOperation) : 0u);
        return 1;
    }

    i32 EditorToolHandler::SetGizmoOperation(Zenith* zenith)
    {
        ECS::Singletons::EditorSelection* selection = GetSelection();
        if (selection)
        {
            u32 op = glm::min(zenith->CheckVal<u32>(1), static_cast<u32>(ECS::Singletons::GizmoOperation::Scale));
            selection->gizmoOperation = static_cast<ECS::Singletons::GizmoOperation>(op);
        }
        return 0;
    }

    i32 EditorToolHandler::GetGizmoMode(Zenith* zenith)
    {
        ECS::Singletons::EditorSelection* selection = GetSelection();
        zenith->Push(selection ? static_cast<u32>(selection->gizmoMode) : 0u);
        return 1;
    }

    i32 EditorToolHandler::SetGizmoMode(Zenith* zenith)
    {
        ECS::Singletons::EditorSelection* selection = GetSelection();
        if (selection)
        {
            u32 mode = glm::min(zenith->CheckVal<u32>(1), static_cast<u32>(ECS::Singletons::GizmoMode::World));
            selection->gizmoMode = static_cast<ECS::Singletons::GizmoMode>(mode);
        }
        return 0;
    }

    i32 EditorToolHandler::SetDrawOBB(Zenith* zenith)
    {
        ECS::Singletons::EditorSelection* selection = GetSelection();
        if (selection)
            selection->drawOBB = zenith->CheckVal<bool>(1);
        return 0;
    }

    i32 EditorToolHandler::SetDrawWorldAABB(Zenith* zenith)
    {
        ECS::Singletons::EditorSelection* selection = GetSelection();
        if (selection)
            selection->drawWorldAABB = zenith->CheckVal<bool>(1);
        return 0;
    }

    void EditorToolHandler::OnSelectionChanged(Zenith* zenith)
    {
        if (_onSelectionChangedRef == LUA_NOREF)
            return;

        zenith->GetRawI(LUA_REGISTRYINDEX, _onSelectionChangedRef);
        zenith->PCall(0);
    }

    void EditorToolHandler::OnGizmoChanged(Zenith* zenith)
    {
        if (_onGizmoChangedRef == LUA_NOREF)
            return;

        zenith->GetRawI(LUA_REGISTRYINDEX, _onGizmoChangedRef);
        zenith->PCall(0);
    }
}
