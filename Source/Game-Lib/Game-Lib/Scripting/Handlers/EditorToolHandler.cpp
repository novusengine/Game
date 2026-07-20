#include "EditorToolHandler.h"
#include "Game-Lib/Application/EnttRegistries.h"
#include "Game-Lib/ECS/Singletons/EditorSelection.h"
#include "Game-Lib/Scripting/Util/ZenithUtil.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Base/CVarSystem/CVarSystemPrivate.h>

#include <MetaGen/Game/Lua/Lua.h>

#include <Scripting/LuaManager.h>
#include <Scripting/Zenith.h>

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <lualib.h>

#include <type_traits>

namespace Scripting::Editor
{
    namespace
    {
        template <typename T>
        void AddCVarTableFields(Zenith* zenith, const CVarStorage<T>& storage, CVarFlags flags)
        {
            if constexpr (std::is_same_v<T, i32>)
            {
                if ((flags & CVarFlags::EditCheckbox) == CVarFlags::EditCheckbox)
                {
                    zenith->AddTableField("value", storage.current != 0);
                    zenith->AddTableField("defaultValue", storage.initial != 0);
                }
                else
                {
                    zenith->AddTableField("value", storage.current);
                    zenith->AddTableField("defaultValue", storage.initial);
                }
            }
            else if constexpr (std::is_same_v<T, std::string>)
            {
                zenith->AddTableField("value", storage.current.c_str());
                zenith->AddTableField("defaultValue", storage.initial.c_str());
            }
            else if constexpr (std::is_same_v<T, ShowFlag>)
            {
                zenith->AddTableField("value", storage.current == ShowFlag::ENABLED);
                zenith->AddTableField("defaultValue", storage.initial == ShowFlag::ENABLED);
            }
            else if constexpr (std::is_same_v<T, vec4> || std::is_same_v<T, ivec4>)
            {
                auto addVectorField = [zenith](const char* name, const T& value)
                {
                    zenith->CreateTable();
                    zenith->AddTableField("x", value.x);
                    zenith->AddTableField("y", value.y);
                    zenith->AddTableField("z", value.z);
                    zenith->AddTableField("w", value.w);
                    zenith->SetTableKey(name);
                };
                addVectorField("value", storage.current);
                addVectorField("defaultValue", storage.initial);
            }
            else
            {
                zenith->AddTableField("value", storage.current);
                zenith->AddTableField("defaultValue", storage.initial);
            }
        }

        template <typename T>
        void AddCVars(Zenith* zenith, CVarArray<T>* cvars, const char* typeName, i32& tableIndex)
        {
            for (i32 i = 0; i < cvars->lastCVar; ++i)
            {
                const CVarStorage<T>& storage = cvars->cvars[i];
                const CVarParameter* parameter = storage.parameter;
                if (!parameter)
                    continue;

                const std::string qualifiedName = CVarSystem::GetQualifiedName(parameter->category, parameter->name.c_str());

                zenith->CreateTable();
                zenith->AddTableField("name", parameter->name.c_str());
                zenith->AddTableField("qualifiedName", qualifiedName.c_str());
                zenith->AddTableField("description", parameter->description.c_str());
                zenith->AddTableField("category", static_cast<u32>(parameter->category));
                zenith->AddTableField("flags", static_cast<u32>(parameter->flags));
                const char* displayTypeName = typeName;
                if constexpr (std::is_same_v<T, i32>)
                {
                    if ((parameter->flags & CVarFlags::EditCheckbox) == CVarFlags::EditCheckbox)
                        displayTypeName = "Boolean";
                }
                zenith->AddTableField("type", displayTypeName);
                AddCVarTableFields(zenith, storage, parameter->flags);
                zenith->SetTableKey(++tableIndex);
            }
        }

        CVarParameter* GetCVarParameter(Zenith* zenith)
        {
            if (!zenith->IsNumber(1) || !zenith->IsString(2))
                return nullptr;

            const CVarCategory category = static_cast<CVarCategory>(zenith->CheckVal<u32>(1));
            const char* name = zenith->Get<const char*>(2);
            return name ? CVarSystemImpl::Get()->GetCVar(category, name) : nullptr;
        }

        template <typename T>
        i32 ResetCVarValues(CVarArray<T>* cvars)
        {
            i32 resetCount = 0;
            for (i32 i = 0; i < cvars->lastCVar; ++i)
            {
                CVarStorage<T>& storage = cvars->cvars[i];
                if (!storage.parameter || storage.current == storage.initial)
                    continue;

                cvars->SetCurrent(storage.initial, i);
                ++resetCount;
            }
            return resetCount;
        }

    }

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
        Scripting::Util::Zenith::Unref(zenith, _onSelectionChangedRef);
        Scripting::Util::Zenith::Unref(zenith, _onGizmoChangedRef);
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

    i32 EditorToolHandler::GetCVars(Zenith* zenith)
    {
        CVarSystemImpl* cvarSystem = CVarSystemImpl::Get();
        zenith->CreateTable();

        i32 tableIndex = 0;
        AddCVars(zenith, cvarSystem->GetCVarArray<i32>(), "Integer", tableIndex);
        AddCVars(zenith, cvarSystem->GetCVarArray<f64>(), "Float", tableIndex);
        AddCVars(zenith, cvarSystem->GetCVarArray<std::string>(), "String", tableIndex);
        AddCVars(zenith, cvarSystem->GetCVarArray<vec4>(), "Float Vector", tableIndex);
        AddCVars(zenith, cvarSystem->GetCVarArray<ivec4>(), "Integer Vector", tableIndex);
        AddCVars(zenith, cvarSystem->GetCVarArray<ShowFlag>(), "Boolean", tableIndex);
        return 1;
    }

    i32 EditorToolHandler::SetCVar(Zenith* zenith)
    {
        CVarParameter* parameter = GetCVarParameter(zenith);
        if (!parameter || (parameter->flags & CVarFlags::EditReadOnly) == CVarFlags::EditReadOnly)
            return 0;

        CVarSystemImpl* cvarSystem = CVarSystemImpl::Get();
        switch (parameter->type)
        {
        case CVarType::INT:
            cvarSystem->GetCVarArray<i32>()->SetCurrent(
                zenith->IsBoolean(3) ? (zenith->ToBoolean(3) ? 1 : 0) : zenith->CheckVal<i32>(3),
                parameter->arrayIndex);
            break;
        case CVarType::FLOAT:
            cvarSystem->GetCVarArray<f64>()->SetCurrent(zenith->CheckVal<f64>(3), parameter->arrayIndex);
            break;
        case CVarType::STRING:
            cvarSystem->GetCVarArray<std::string>()->SetCurrent(zenith->CheckVal<const char*>(3), parameter->arrayIndex);
            break;
        case CVarType::FLOATVEC:
            cvarSystem->GetCVarArray<vec4>()->SetCurrent(vec4(
                zenith->CheckVal<f32>(3), zenith->CheckVal<f32>(4),
                zenith->CheckVal<f32>(5), zenith->CheckVal<f32>(6)), parameter->arrayIndex);
            break;
        case CVarType::INTVEC:
            cvarSystem->GetCVarArray<ivec4>()->SetCurrent(ivec4(
                zenith->CheckVal<i32>(3), zenith->CheckVal<i32>(4),
                zenith->CheckVal<i32>(5), zenith->CheckVal<i32>(6)), parameter->arrayIndex);
            break;
        case CVarType::SHOWFLAG:
            cvarSystem->GetCVarArray<ShowFlag>()->SetCurrent(zenith->CheckVal<bool>(3) ? ShowFlag::ENABLED : ShowFlag::DISABLED, parameter->arrayIndex);
            break;
        }

        return 0;
    }

    i32 EditorToolHandler::ResetCVar(Zenith* zenith)
    {
        CVarParameter* parameter = GetCVarParameter(zenith);
        if (!parameter || (parameter->flags & CVarFlags::EditReadOnly) == CVarFlags::EditReadOnly)
            return 0;

        CVarSystemImpl* cvarSystem = CVarSystemImpl::Get();
        switch (parameter->type)
        {
        case CVarType::INT:
        {
            CVarStorage<i32>* storage = cvarSystem->GetCVarArray<i32>()->GetCurrentStorage(parameter->arrayIndex);
            cvarSystem->GetCVarArray<i32>()->SetCurrent(storage->initial, parameter->arrayIndex);
            break;
        }
        case CVarType::FLOAT:
        {
            CVarStorage<f64>* storage = cvarSystem->GetCVarArray<f64>()->GetCurrentStorage(parameter->arrayIndex);
            cvarSystem->GetCVarArray<f64>()->SetCurrent(storage->initial, parameter->arrayIndex);
            break;
        }
        case CVarType::STRING:
        {
            CVarStorage<std::string>* storage = cvarSystem->GetCVarArray<std::string>()->GetCurrentStorage(parameter->arrayIndex);
            cvarSystem->GetCVarArray<std::string>()->SetCurrent(storage->initial, parameter->arrayIndex);
            break;
        }
        case CVarType::FLOATVEC:
        {
            CVarStorage<vec4>* storage = cvarSystem->GetCVarArray<vec4>()->GetCurrentStorage(parameter->arrayIndex);
            cvarSystem->GetCVarArray<vec4>()->SetCurrent(storage->initial, parameter->arrayIndex);
            break;
        }
        case CVarType::INTVEC:
        {
            CVarStorage<ivec4>* storage = cvarSystem->GetCVarArray<ivec4>()->GetCurrentStorage(parameter->arrayIndex);
            cvarSystem->GetCVarArray<ivec4>()->SetCurrent(storage->initial, parameter->arrayIndex);
            break;
        }
        case CVarType::SHOWFLAG:
        {
            CVarStorage<ShowFlag>* storage = cvarSystem->GetCVarArray<ShowFlag>()->GetCurrentStorage(parameter->arrayIndex);
            cvarSystem->GetCVarArray<ShowFlag>()->SetCurrent(storage->initial, parameter->arrayIndex);
            break;
        }
        }

        return 0;
    }

    i32 EditorToolHandler::ResetAllCVars(Zenith* zenith)
    {
        zenith->Push(CVarSystemImpl::Get()->RemoveUnregisteredCVars());
        return 1;
    }

    i32 EditorToolHandler::ResetAllCVarValues(Zenith* zenith)
    {
        CVarSystemImpl* cvarSystem = CVarSystemImpl::Get();
        i32 resetCount = 0;
        resetCount += ResetCVarValues(cvarSystem->GetCVarArray<i32>());
        resetCount += ResetCVarValues(cvarSystem->GetCVarArray<f64>());
        resetCount += ResetCVarValues(cvarSystem->GetCVarArray<std::string>());
        resetCount += ResetCVarValues(cvarSystem->GetCVarArray<vec4>());
        resetCount += ResetCVarValues(cvarSystem->GetCVarArray<ivec4>());
        resetCount += ResetCVarValues(cvarSystem->GetCVarArray<ShowFlag>());
        zenith->Push(resetCount);
        return 1;
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
