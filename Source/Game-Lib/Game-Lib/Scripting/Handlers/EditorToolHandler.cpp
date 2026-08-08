#include "EditorToolHandler.h"
#include "Game-Lib/Application/EnttRegistries.h"
#include "Game-Lib/ECS/Singletons/Database/ClientDBSingleton.h"
#include "Game-Lib/ECS/Singletons/EditorSelection.h"
#include "Game-Lib/ECS/Util/Database/SpellUtil.h"
#include "Game-Lib/Editor/SpellEditorBackend.h"
#include "Game-Lib/Editor/TerrainEditSession.h"
#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Scripting/Util/ZenithUtil.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Base/CVarSystem/CVarSystemPrivate.h>

#include <MetaGen/Game/Lua/Lua.h>
#include <MetaGen/Shared/ClientDB/ClientDB.h>
#include <MetaGen/Shared/Spell/Spell.h>

#include <Scripting/LuaManager.h>
#include <Scripting/Zenith.h>

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <lualib.h>

#include <algorithm>
#include <string>
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
            if (!zenith->IsInteger(1) || !zenith->IsString(2))
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

        ::Editor::SpellEditorBackend* GetSpellEditorBackend()
        {
            EnttRegistries* registries = ServiceLocator::GetEnttRegistries();
            if (!registries || !registries->dbRegistry)
                return nullptr;

            entt::registry::context& context = registries->dbRegistry->ctx();
            return context.contains<::Editor::SpellEditorBackend>()
                ? &context.get<::Editor::SpellEditorBackend>()
                : &context.emplace<::Editor::SpellEditorBackend>();
        }

        ::Editor::TerrainEditSession* GetTerrainEditSession()
        {
            GameRenderer* gameRenderer = ServiceLocator::GetGameRenderer();
            return gameRenderer ? gameRenderer->GetTerrainEditSession() : nullptr;
        }

        const char* GetDataStateName(::Editor::SpellEditorDataState state)
        {
            switch (state)
            {
                case ::Editor::SpellEditorDataState::Unavailable: return "Unavailable";
                case ::Editor::SpellEditorDataState::Loading: return "Loading";
                case ::Editor::SpellEditorDataState::Ready: return "Ready";
                case ::Editor::SpellEditorDataState::Failed: return "Failed";
                default: return "Unavailable";
            }
        }

        const char* GetDraftStateName(::Editor::SpellEditorDraftState state)
        {
            switch (state)
            {
                case ::Editor::SpellEditorDraftState::None: return "None";
                case ::Editor::SpellEditorDraftState::Clean: return "Clean";
                case ::Editor::SpellEditorDraftState::Dirty: return "Dirty";
                case ::Editor::SpellEditorDraftState::Synchronizing: return "Synchronizing";
                case ::Editor::SpellEditorDraftState::MutationFailed: return "MutationFailed";
                case ::Editor::SpellEditorDraftState::Refreshing: return "Refreshing";
                default: return "None";
            }
        }

        template <typename Meta>
        void PushEnumOptions(Zenith* zenith, const char* key, bool omitTerminal = true)
        {
            zenith->CreateTable();
            i32 optionIndex = 0;
            for (const auto& [name, value] : Meta::ENUM_FIELD_LIST)
            {
                if (omitTerminal && name == "Count")
                    continue;

                zenith->CreateTable();
                zenith->AddTableField("name", name.data());
                zenith->AddTableField("value", value);
                zenith->SetTableKey(++optionIndex);
            }
            zenith->SetTableKey(key);
        }

        void PushDiagnostics(Zenith* zenith, const std::vector<::Editor::SpellEditorDiagnostic>& diagnostics)
        {
            zenith->CreateTable();
            i32 index = 0;
            for (const ::Editor::SpellEditorDiagnostic& diagnostic : diagnostics)
            {
                zenith->CreateTable();
                zenith->AddTableField("path", diagnostic.path.c_str());
                zenith->AddTableField("message", diagnostic.message.c_str());
                zenith->SetTableKey(++index);
            }
        }

        void PushSpellEditorDraft(Zenith* zenith, ::Editor::SpellEditorBackend& backend)
        {
            const ::Editor::SpellEditorDraft* draft = backend.GetDraft();
            if (!draft)
            {
                zenith->Push();
                return;
            }

            zenith->CreateTable();
            zenith->AddTableField("spellID", draft->spellID);
            zenith->AddTableField("isCreate", draft->isCreate);
            zenith->AddTableField("name", draft->name.c_str());
            zenith->AddTableField("description", draft->description.c_str());
            zenith->AddTableField("auraDescription", draft->auraDescription.c_str());
            zenith->AddTableField("iconID", draft->iconID);
            zenith->AddTableField("castTime", draft->castTime);
            zenith->AddTableField("cooldown", draft->cooldown);
            zenith->AddTableField("targetSelector", draft->targetSelector);
            zenith->AddTableField("targetShape", draft->targetShape);
            zenith->AddTableField("targetRelation", draft->targetRelation);
            zenith->AddTableField("targetRecipientMask", draft->targetRecipientMask);
            zenith->AddTableField("rangePolicy", draft->rangePolicy);
            zenith->AddTableField("minimumRange", draft->minimumRange);
            zenith->AddTableField("maximumRange", draft->maximumRange);
            zenith->AddTableField("targetRadius", draft->targetRadius);
            zenith->AddTableField("maximumTargets", draft->maximumTargets);
            zenith->AddTableField("state", GetDraftStateName(backend.GetDraftState()));
            zenith->AddTableField("pendingRequestID", backend.GetPendingRequestID());
            zenith->AddTableField("serverDiagnostic", backend.GetServerDiagnostic().c_str());

            if (draft->aura)
            {
                const ::Editor::SpellEditorAuraDraft& aura = *draft->aura;
                zenith->CreateTable();
                zenith->AddTableField("duration", aura.duration);
                zenith->AddTableField("stacksPerApplication", aura.stacksPerApplication);
                zenith->AddTableField("maximumStacks", aura.maximumStacks);
                zenith->AddTableField("applicationPolicy", aura.applicationPolicy);
                zenith->AddTableField("disposition", aura.disposition);
                zenith->AddTableField("dispelType", aura.dispelType);
                zenith->AddTableField("lifecycleFlags", aura.lifecycleFlags);
                zenith->SetTableKey("aura");
            }

            zenith->CreateTable();
            i32 effectIndex = 0;
            for (const ::Editor::SpellEditorEffectDraft& effect : draft->effects)
            {
                const auto* descriptor = MetaGen::Shared::Spell::GetSpellEffectDescriptor(
                    static_cast<MetaGen::Shared::Spell::SpellEffectTypeEnum>(effect.type));
                zenith->CreateTable();
                zenith->AddTableField("id", effect.id);
                zenith->AddTableField("priority", effect.priority);
                zenith->AddTableField("type", effect.type);
                zenith->AddTableField("typeName", descriptor ? descriptor->name.data() : "Unknown");
                zenith->AddTableField("executionOrder", effectIndex + 1);
                if (descriptor)
                {
                    zenith->AddTableField("owner", static_cast<u8>(descriptor->owner));
                    zenith->AddTableField("periodic", descriptor->periodic);
                    zenith->AddTableField("targetMode", MetaGen::Shared::Spell::GetSpellEffectTargetModeName(descriptor->target.mode).data());
                    zenith->AddTableField("targetKind", MetaGen::Shared::Spell::GetSpellEffectTargetKindName(descriptor->target.kind).data());
                    zenith->AddTableField("targetState", MetaGen::Shared::Spell::GetSpellEffectTargetStateName(descriptor->target.state).data());
                }
                zenith->CreateTable();
                for (i32 parameterIndex = 0; parameterIndex < static_cast<i32>(effect.parameters.size()); ++parameterIndex)
                    zenith->AddTableField(parameterIndex + 1, effect.parameters[parameterIndex]);
                zenith->SetTableKey("parameters");
                zenith->SetTableKey(++effectIndex);
            }
            zenith->SetTableKey("effects");

            zenith->CreateTable();
            i32 constraintIndex = 0;
            for (const ::Editor::SpellEditorConstraintDraft& constraint : draft->constraints)
            {
                zenith->CreateTable();
                zenith->AddTableField("id", constraint.id);
                zenith->AddTableField("groupID", constraint.groupID);
                zenith->AddTableField("scope", constraint.scope);
                zenith->AddTableField("maximumApplications", constraint.maximumApplications);
                zenith->AddTableField("overflowBehavior", constraint.overflowBehavior);
                zenith->AddTableField("overrideMask", constraint.overrideMask);
                zenith->SetTableKey(++constraintIndex);
            }
            zenith->SetTableKey("constraints");

            zenith->CreateTable();
            i32 procLinkIndex = 0;
            for (const ::Editor::SpellEditorProcLinkDraft& procLink : draft->procLinks)
            {
                zenith->CreateTable();
                zenith->AddTableField("id", procLink.id);
                zenith->AddTableField("procDataID", procLink.procDataID);
                zenith->AddTableField("effectMask", procLink.effectMask);
                zenith->CreateTable();
                for (i32 index = 0; index < static_cast<i32>(draft->effects.size()); ++index)
                {
                    if ((procLink.effectMask & (u64{ 1 } << index)) != 0)
                        zenith->AddTableField(index + 1, draft->effects[index].id);
                }
                zenith->SetTableKey("selectedEffectIDs");
                zenith->SetTableKey(++procLinkIndex);
            }
            zenith->SetTableKey("procLinks");
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
        LuaMethodTable::Set(zenith, spellEditorGlobalMethods, "SpellEditor", excludeFlags);
        LuaMethodTable::Set(zenith, terrainEditorGlobalMethods, "TerrainEditor", excludeFlags);

        _onSelectionChangedRef = LUA_NOREF;
    }

    void EditorToolHandler::Update(Zenith*, f32)
    {
        ::Editor::SpellEditorBackend* backend = GetSpellEditorBackend();
        if (backend)
            backend->Update();
    }

    void EditorToolHandler::Clear(Zenith* zenith)
    {
        ::Editor::TerrainEditSession* terrainEditSession = GetTerrainEditSession();
        if (terrainEditSession)
            terrainEditSession->SetEnabled(false);

        Scripting::Util::Zenith::Unref(zenith, _onSelectionChangedRef);
        Scripting::Util::Zenith::Unref(zenith, _onGizmoChangedRef);
        _onSelectionChangedRef = LUA_NOREF;
        _onGizmoChangedRef = LUA_NOREF;
    }

    i32 EditorToolHandler::GetTerrainEditorState(Zenith* zenith)
    {
        ::Editor::TerrainEditSession* session = GetTerrainEditSession();
        if (!session)
        {
            zenith->Push();
            return 1;
        }

        const ::Editor::TerrainEditSession::State state = session->GetState();
        zenith->CreateTable();
        zenith->AddTableField("available", state.available);
        zenith->AddTableField("enabled", state.enabled);
        zenith->AddTableField("strokeActive", state.strokeActive);
        zenith->AddTableField("cursorHit", state.cursorHit);
        zenith->AddTableField("canUndo", state.canUndo);
        zenith->AddTableField("canRedo", state.canRedo);
        zenith->AddTableField("dirtyChunkCount", state.dirtyChunkCount);
        zenith->AddTableField("blockedPaintCellCount", state.blockedPaintCellCount);
        zenith->AddTableField("cursorPosition", state.cursorPosition);
        return 1;
    }

    i32 EditorToolHandler::SetTerrainEditorEnabled(Zenith* zenith)
    {
        ::Editor::TerrainEditSession* session = GetTerrainEditSession();
        zenith->Push(session && session->SetEnabled(zenith->CheckVal<bool>(1)));
        return 1;
    }

    i32 EditorToolHandler::SetTerrainEditorPreviewRadius(Zenith* zenith)
    {
        ::Editor::TerrainEditSession* session = GetTerrainEditSession();
        if (!session)
        {
            zenith->Push(false);
            return 1;
        }

        session->SetPreviewRadius(zenith->CheckVal<f32>(1));
        zenith->Push(true);
        return 1;
    }

    i32 EditorToolHandler::BeginTerrainEditorStroke(Zenith* zenith)
    {
        ::Editor::TerrainEditSession* session = GetTerrainEditSession();
        const char* name = zenith->CheckVal<const char*>(1);
        zenith->Push(session && name && session->BeginStroke(name));
        return 1;
    }

    i32 EditorToolHandler::ApplyTerrainEditorSample(Zenith* zenith)
    {
        ::Editor::TerrainEditSession* session = GetTerrainEditSession();
        if (!session || !zenith->IsInteger(1))
        {
            zenith->Push(false);
            return 1;
        }

        const i32 operation = zenith->CheckVal<i32>(1);
        if (operation < 0 || operation > static_cast<i32>(::Editor::TerrainSculptOperation::Smooth))
        {
            zenith->Push(false);
            return 1;
        }

        zenith->Push(session->ApplyStrokeSample(
            static_cast<::Editor::TerrainSculptOperation>(operation),
            zenith->CheckVal<vec3>(2),
            zenith->CheckVal<f32>(3),
            zenith->CheckVal<f32>(4),
            zenith->CheckVal<f32>(5),
            zenith->CheckVal<f32>(6),
            zenith->CheckVal<f32>(7)));
        return 1;
    }

    i32 EditorToolHandler::SetTerrainEditorPaintTexture(Zenith* zenith)
    {
        ::Editor::TerrainEditSession* session = GetTerrainEditSession();
        const char* virtualPath = zenith->CheckVal<const char*>(1);
        zenith->Push(session && virtualPath && session->SetPaintTexture(virtualPath));
        return 1;
    }

    i32 EditorToolHandler::ApplyTerrainEditorPaintSample(Zenith* zenith)
    {
        ::Editor::TerrainEditSession* session = GetTerrainEditSession();
        zenith->Push(session && session->ApplyPaintSample(
            zenith->CheckVal<vec3>(1),
            zenith->CheckVal<f32>(2),
            zenith->CheckVal<f32>(3),
            zenith->CheckVal<f32>(4),
            zenith->CheckVal<f32>(5),
            zenith->CheckVal<f32>(6)));
        return 1;
    }

    i32 EditorToolHandler::GetTerrainEditorCursorTextureLayers(Zenith* zenith)
    {
        ::Editor::TerrainEditSession* session = GetTerrainEditSession();
        if (!session)
        {
            zenith->CreateTable();
            return 1;
        }

        std::vector<::Editor::TerrainEditSession::TextureLayerState> layers;
        session->GetCursorTextureLayers(layers);
        zenith->CreateTable();
        i32 layerIndex = 0;
        for (const ::Editor::TerrainEditSession::TextureLayerState& layer : layers)
        {
            zenith->CreateTable();
            zenith->AddTableField("path", layer.path.c_str());
            zenith->AddTableField("textureHash", layer.textureHash);
            zenith->AddTableField("layerIndex", layer.layerIndex);
            zenith->AddTableField("averageWeight", layer.averageWeight);
            zenith->SetTableKey(++layerIndex);
        }
        return 1;
    }

    i32 EditorToolHandler::CommitTerrainEditorStroke(Zenith* zenith)
    {
        ::Editor::TerrainEditSession* session = GetTerrainEditSession();
        zenith->Push(session && session->CommitStroke());
        return 1;
    }

    i32 EditorToolHandler::CancelTerrainEditorStroke(Zenith* zenith)
    {
        ::Editor::TerrainEditSession* session = GetTerrainEditSession();
        zenith->Push(session && session->CancelStroke());
        return 1;
    }

    i32 EditorToolHandler::UndoTerrainEditor(Zenith* zenith)
    {
        ::Editor::TerrainEditSession* session = GetTerrainEditSession();
        zenith->Push(session && session->Undo());
        return 1;
    }

    i32 EditorToolHandler::RedoTerrainEditor(Zenith* zenith)
    {
        ::Editor::TerrainEditSession* session = GetTerrainEditSession();
        zenith->Push(session && session->Redo());
        return 1;
    }

    i32 EditorToolHandler::SaveTerrainEditor(Zenith* zenith)
    {
        ::Editor::TerrainEditSession* session = GetTerrainEditSession();
        zenith->Push(session && session->Save());
        return 1;
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
        if (zenith->IsInteger(1) && registry)
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

    i32 EditorToolHandler::GetSpellEditorSnapshot(Zenith* zenith)
    {
        ::Editor::SpellEditorBackend* backend = GetSpellEditorBackend();
        EnttRegistries* registries = ServiceLocator::GetEnttRegistries();
        if (!backend || !registries || !registries->dbRegistry)
            return 0;

        backend->Update();
        entt::registry::context& context = registries->dbRegistry->ctx();
        ::Editor::SpellEditorData* data = context.contains<::Editor::SpellEditorData>()
            ? &context.get<::Editor::SpellEditorData>()
            : nullptr;

        zenith->CreateTable();
        zenith->AddTableField("dataState", data ? GetDataStateName(data->state) : "Unavailable");
        zenith->AddTableField("draftState", GetDraftStateName(backend->GetDraftState()));
        zenith->AddTableField("pendingRequestID", backend->GetPendingRequestID());
        zenith->AddTableField("serverDiagnostic", backend->GetServerDiagnostic().c_str());
        zenith->AddTableField("mutationSucceeded", backend->ConsumeMutationSuccess());

        zenith->CreateTable();
        if (data && data->state == ::Editor::SpellEditorDataState::Ready)
        {
            using Artifact = MetaGen::Shared::Spell::SpellEditorArtifactEnum;
            ::ClientDB::Data* spells = data->GetStorage(Artifact::Spell);
            ::ClientDB::Data* auras = data->GetStorage(Artifact::SpellAura);
            ::ClientDB::Data* effects = data->GetStorage(Artifact::SpellEffects);
            i32 spellIndex = 0;
            spells->Each([&](u32 spellID, const MetaGen::Shared::ClientDB::SpellRecord& spell)
            {
                const std::vector<u32>* effectIDs = ECSUtil::Spell::GetSpellEffectList(data->spellIndex, spellID);
                zenith->CreateTable();
                zenith->AddTableField("id", spellID);
                zenith->AddTableField("name", spells->GetString(spell.name).c_str());
                zenith->AddTableField("description", spells->GetString(spell.description).c_str());
                zenith->AddTableField("iconID", spell.iconID);
                zenith->AddTableField("hasAura", auras->Has(spellID));
                zenith->AddTableField("effectCount", effectIDs ? static_cast<u32>(effectIDs->size()) : 0u);
                u32 referencedAuraID = 0;
                if (effectIDs)
                {
                    for (u32 effectID : *effectIDs)
                    {
                        if (!effects->Has(effectID))
                            continue;
                        const auto& effect = effects->Get<MetaGen::Shared::ClientDB::SpellEffectsRecord>(effectID);
                        if (static_cast<MetaGen::Shared::Spell::SpellEffectTypeEnum>(effect.effectType) ==
                            MetaGen::Shared::Spell::SpellEffectTypeEnum::ApplyAura && effect.parameters[0] > 0)
                        {
                            referencedAuraID = static_cast<u32>(effect.parameters[0]);
                            break;
                        }
                    }
                }
                zenith->AddTableField("referencedAuraID", referencedAuraID);
                zenith->SetTableKey(++spellIndex);
                return true;
            });
        }
        zenith->SetTableKey("spells");

        zenith->CreateTable();
        if (data && data->state == ::Editor::SpellEditorDataState::Ready)
        {
            ::ClientDB::Data* groups = data->GetStorage(MetaGen::Shared::Spell::SpellEditorArtifactEnum::SpellAuraConstraintGroup);
            i32 groupIndex = 0;
            groups->Each([&](u32 groupID, const MetaGen::Shared::ClientDB::SpellAuraConstraintGroupRecord& group)
            {
                if (groupID == 0)
                    return true;

                zenith->CreateTable();
                zenith->AddTableField("id", groupID);
                zenith->AddTableField("name", groups->GetString(group.name).c_str());
                zenith->AddTableField("defaultScope", group.defaultScope);
                zenith->AddTableField("defaultMaximumApplications", group.defaultMaximumApplications);
                zenith->AddTableField("defaultOverflowBehavior", group.defaultOverflowBehavior);
                zenith->SetTableKey(++groupIndex);
                return true;
            });
        }
        zenith->SetTableKey("constraintGroups");

        zenith->CreateTable();
        if (data && data->state == ::Editor::SpellEditorDataState::Ready)
        {
            ::ClientDB::Data* constraints = data->GetStorage(MetaGen::Shared::Spell::SpellEditorArtifactEnum::SpellAuraConstraint);
            i32 constraintIndex = 0;
            constraints->Each([&](u32 constraintID, const MetaGen::Shared::ClientDB::SpellAuraConstraintRecord& constraint)
            {
                zenith->CreateTable();
                zenith->AddTableField("id", constraintID);
                zenith->AddTableField("spellID", constraint.spellID);
                zenith->AddTableField("groupID", constraint.groupID);
                zenith->AddTableField("scope", constraint.scope);
                zenith->AddTableField("maximumApplications", constraint.maximumApplications);
                zenith->AddTableField("overflowBehavior", constraint.overflowBehavior);
                zenith->AddTableField("overrideMask", constraint.overrideMask);
                zenith->SetTableKey(++constraintIndex);
                return true;
            });
        }
        zenith->SetTableKey("constraints");

        zenith->CreateTable();
        if (data && data->state == ::Editor::SpellEditorDataState::Ready)
        {
            ::ClientDB::Data* procData = data->GetStorage(MetaGen::Shared::Spell::SpellEditorArtifactEnum::SpellProcData);
            i32 procDataIndex = 0;
            procData->Each([&](u32 procDataID, const MetaGen::Shared::ClientDB::SpellProcDataRecord& value)
            {
                zenith->CreateTable();
                zenith->AddTableField("id", procDataID);
                zenith->AddTableField("ownerSpellID", value.ownerSpellID);
                zenith->AddTableField("name", procData->GetString(value.name).c_str());
                zenith->AddTableField("phaseMask", value.phaseMask);
                zenith->AddTableField("typeMask", value.typeMask);
                zenith->AddTableField("hitMask", value.hitMask);
                zenith->AddTableField("flags", value.flags);
                zenith->AddTableField("procsPerMinute", value.procsPerMinute);
                zenith->AddTableField("chanceToProc", value.chanceToProc);
                zenith->AddTableField("internalCooldownMS", value.internalCooldownMS);
                zenith->AddTableField("charges", value.charges);
                zenith->SetTableKey(++procDataIndex);
                return true;
            });
        }
        zenith->SetTableKey("procDataDefinitions");

        zenith->CreateTable();
        if (data && data->state == ::Editor::SpellEditorDataState::Ready)
        {
            ::ClientDB::Data* procLinks = data->GetStorage(MetaGen::Shared::Spell::SpellEditorArtifactEnum::SpellProcLink);
            i32 procLinkIndex = 0;
            procLinks->Each([&](u32 procLinkID, const MetaGen::Shared::ClientDB::SpellProcLinkRecord& value)
            {
                zenith->CreateTable();
                zenith->AddTableField("id", procLinkID);
                zenith->AddTableField("spellID", value.spellID);
                zenith->AddTableField("procDataID", value.procDataID);
                zenith->AddTableField("effectMask", value.effectMask);
                zenith->SetTableKey(++procLinkIndex);
                return true;
            });
        }
        zenith->SetTableKey("procLinks");

        zenith->CreateTable();
        if (data && data->state == ::Editor::SpellEditorDataState::Ready)
        {
            ::ClientDB::Data* effects = data->GetStorage(MetaGen::Shared::Spell::SpellEditorArtifactEnum::SpellEffects);
            i32 referenceIndex = 0;
            effects->Each([&](u32 effectID, const MetaGen::Shared::ClientDB::SpellEffectsRecord& effect)
            {
                if (static_cast<MetaGen::Shared::Spell::SpellEffectTypeEnum>(effect.effectType) !=
                    MetaGen::Shared::Spell::SpellEffectTypeEnum::ApplyAura || effect.parameters[0] <= 0)
                {
                    return true;
                }
                zenith->CreateTable();
                zenith->AddTableField("effectID", effectID);
                zenith->AddTableField("sourceSpellID", effect.spellID);
                zenith->AddTableField("targetSpellID", static_cast<u32>(effect.parameters[0]));
                zenith->SetTableKey(++referenceIndex);
                return true;
            });
        }
        zenith->SetTableKey("auraReferences");
        return 1;
    }

    i32 EditorToolHandler::GetSpellEditorDraft(Zenith* zenith)
    {
        ::Editor::SpellEditorBackend* backend = GetSpellEditorBackend();
        if (!backend)
            return 0;
        backend->Update();
        PushSpellEditorDraft(zenith, *backend);
        return 1;
    }

    i32 EditorToolHandler::GetSpellEditorCatalog(Zenith* zenith)
    {
        using namespace MetaGen::Shared::Spell;
        zenith->CreateTable();

        zenith->CreateTable();
        i32 effectIndex = 0;
        for (const SpellEffectDescriptor& descriptor : SPELL_EFFECT_CATALOG)
        {
            if (descriptor.owner == SpellEffectOwner::None)
                continue;
            zenith->CreateTable();
            zenith->AddTableField("value", static_cast<u8>(descriptor.type));
            zenith->AddTableField("name", descriptor.name.data());
            zenith->AddTableField("owner", static_cast<u8>(descriptor.owner));
            zenith->AddTableField("periodic", descriptor.periodic);
            zenith->AddTableField("targetMode", GetSpellEffectTargetModeName(descriptor.target.mode).data());
            zenith->AddTableField("targetKind", GetSpellEffectTargetKindName(descriptor.target.kind).data());
            zenith->AddTableField("targetState", GetSpellEffectTargetStateName(descriptor.target.state).data());
            zenith->AddTableField("handler", descriptor.name.data());
            zenith->CreateTable();
            for (u32 parameterIndex = 0; parameterIndex < descriptor.parameterCount; ++parameterIndex)
            {
                const SpellEffectParameterDescriptor& parameter = descriptor.parameters[parameterIndex];
                zenith->CreateTable();
                zenith->AddTableField("index", parameterIndex + 1);
                zenith->AddTableField("name", parameter.name.data());
                zenith->AddTableField("kind", static_cast<u8>(parameter.kind));
                zenith->AddTableField("semanticType", parameter.semanticType.data());
                zenith->AddTableField("defaultValue", parameter.defaultValue);
                zenith->AddTableField("minimumValue", parameter.minimumValue);
                zenith->AddTableField("maximumValue", parameter.maximumValue);
                zenith->CreateTable();
                i32 optionIndex = 0;
                for (const SpellEffectParameterOption& option : parameter.options)
                {
                    zenith->CreateTable();
                    zenith->AddTableField("name", option.name.data());
                    zenith->AddTableField("value", option.value);
                    zenith->SetTableKey(++optionIndex);
                }
                zenith->SetTableKey("options");
                zenith->SetTableKey(static_cast<i32>(parameterIndex + 1));
            }
            zenith->SetTableKey("parameters");
            zenith->SetTableKey(++effectIndex);
        }
        zenith->SetTableKey("effects");

        PushEnumOptions<SpellTargetSelectorEnumMeta>(zenith, "targetSelectors");
        PushEnumOptions<SpellTargetShapeEnumMeta>(zenith, "targetShapes");
        PushEnumOptions<SpellTargetRelationEnumMeta>(zenith, "targetRelations");
        PushEnumOptions<SpellTargetRecipientMaskEnumMeta>(zenith, "targetRecipientMasks", false);
        PushEnumOptions<SpellRangePolicyEnumMeta>(zenith, "rangePolicies");
        PushEnumOptions<AuraApplicationPolicyEnumMeta>(zenith, "auraApplicationPolicies");
        PushEnumOptions<AuraDispositionEnumMeta>(zenith, "auraDispositions");
        PushEnumOptions<AuraDispelTypeEnumMeta>(zenith, "auraDispelTypes");
        PushEnumOptions<AuraLifecycleFlagsEnumMeta>(zenith, "auraLifecycleFlags", false);
        PushEnumOptions<AuraConstraintScopeEnumMeta>(zenith, "constraintScopes");
        PushEnumOptions<AuraConstraintOverflowEnumMeta>(zenith, "constraintOverflowBehaviors");
        PushEnumOptions<AuraConstraintOverrideFlagsEnumMeta>(zenith, "constraintOverrideFlags", false);
        PushEnumOptions<SpellProcPhaseMaskEnumMeta>(zenith, "procPhaseMask");
        PushEnumOptions<SpellProcTypeMaskEnumMeta>(zenith, "procTypeMask");
        PushEnumOptions<SpellProcHitMaskEnumMeta>(zenith, "procHitMask");
        PushEnumOptions<SpellProcFlagEnumMeta>(zenith, "procFlags");
        return 1;
    }

    i32 EditorToolHandler::SearchSpellEditorIcons(Zenith* zenith)
    {
        const char* rawFilter = zenith->CheckVal<const char*>(1);
        const u32 offset = zenith->CheckVal<u32>(2);
        const u32 limit = std::clamp(zenith->CheckVal<u32>(3), 1u, 200u);

        zenith->CreateTable();
        zenith->CreateTable();

        EnttRegistries* registries = ServiceLocator::GetEnttRegistries();
        u32 matchCount = 0;
        i32 resultIndex = 0;
        if (registries && registries->dbRegistry)
        {
            entt::registry::context& context = registries->dbRegistry->ctx();
            if (context.contains<ECS::Singletons::ClientDBSingleton>())
            {
                auto& clientDB = context.get<ECS::Singletons::ClientDBSingleton>();
                if (clientDB.Has(ClientDBHash::Icon))
                {
                    std::string filter = rawFilter ? rawFilter : "";
                    StringUtils::ToLower(filter);
                    ::ClientDB::Data* icons = clientDB.Get(ClientDBHash::Icon);
                    icons->Each([&](u32 iconID, const MetaGen::Shared::ClientDB::IconRecord& icon)
                    {
                        const std::string& texture = icons->GetString(icon.texture);
                        std::string searchableTexture = texture;
                        StringUtils::ToLower(searchableTexture);
                        const std::string iconIDText = std::to_string(iconID);
                        const bool matches = filter.empty() || searchableTexture.find(filter) != std::string::npos || iconIDText.find(filter) != std::string::npos;
                        if (!matches)
                            return true;

                        if (matchCount >= offset && resultIndex < static_cast<i32>(limit))
                        {
                            zenith->CreateTable();
                            zenith->AddTableField("id", iconID);
                            zenith->AddTableField("texture", texture.c_str());
                            zenith->SetTableKey(++resultIndex);
                        }
                        ++matchCount;
                        return true;
                    });
                }
            }
        }

        zenith->SetTableKey("icons");
        zenith->AddTableField("total", matchCount);
        zenith->AddTableField("offset", offset);
        return 1;
    }

    i32 EditorToolHandler::RequestSpellEditorSnapshot(Zenith* zenith)
    {
        ::Editor::SpellEditorBackend* backend = GetSpellEditorBackend();
        zenith->Push(backend && backend->RequestSnapshot());
        return 1;
    }

    i32 EditorToolHandler::OpenSpellEditorDraft(Zenith* zenith)
    {
        ::Editor::SpellEditorBackend* backend = GetSpellEditorBackend();
        zenith->Push(backend && backend->OpenDraft(zenith->CheckVal<u32>(1)));
        return 1;
    }

    i32 EditorToolHandler::CreateSpellEditorDraft(Zenith* zenith)
    {
        ::Editor::SpellEditorBackend* backend = GetSpellEditorBackend();
        zenith->Push(backend && backend->CreateDraft());
        return 1;
    }

    i32 EditorToolHandler::DuplicateSpellEditorDraft(Zenith* zenith)
    {
        ::Editor::SpellEditorBackend* backend = GetSpellEditorBackend();
        zenith->Push(backend && backend->DuplicateDraft(zenith->CheckVal<u32>(1)));
        return 1;
    }

    i32 EditorToolHandler::DiscardSpellEditorDraft(Zenith* zenith)
    {
        ::Editor::SpellEditorBackend* backend = GetSpellEditorBackend();
        if (backend)
            backend->DiscardDraft();
        return 0;
    }

    i32 EditorToolHandler::SetSpellEditorField(Zenith* zenith)
    {
        ::Editor::SpellEditorBackend* backend = GetSpellEditorBackend();
        const char* field = zenith->CheckVal<const char*>(1);
        bool result = false;
        if (backend && field)
        {
            const i32 valueType = lua_type(zenith->state, 2);
            if (valueType == LUA_TSTRING)
                result = backend->SetSpellString(field, zenith->CheckVal<const char*>(2));
            else if (valueType == LUA_TINTEGER)
                result = backend->SetSpellNumber(field, static_cast<f64>(zenith->CheckVal<i64>(2)));
            else if (valueType == LUA_TNUMBER)
                result = backend->SetSpellNumber(field, zenith->CheckVal<f64>(2));
        }
        zenith->Push(result);
        return 1;
    }

    i32 EditorToolHandler::SetSpellEditorAuraEnabled(Zenith* zenith)
    {
        ::Editor::SpellEditorBackend* backend = GetSpellEditorBackend();
        zenith->Push(backend && backend->SetAuraEnabled(zenith->CheckVal<bool>(1)));
        return 1;
    }

    i32 EditorToolHandler::SetSpellEditorAuraField(Zenith* zenith)
    {
        ::Editor::SpellEditorBackend* backend = GetSpellEditorBackend();
        const char* field = zenith->CheckVal<const char*>(1);
        bool result = false;
        if (backend && field)
        {
            const f64 value = zenith->IsInteger(2)
                ? static_cast<f64>(zenith->CheckVal<i64>(2))
                : zenith->CheckVal<f64>(2);
            result = backend->SetAuraNumber(field, value);
        }

        zenith->Push(result);
        return 1;
    }

    i32 EditorToolHandler::AddSpellEditorEffect(Zenith* zenith)
    {
        ::Editor::SpellEditorBackend* backend = GetSpellEditorBackend();
        zenith->Push(backend ? backend->AddEffect(zenith->CheckVal<u8>(1)) : 0u);
        return 1;
    }

    i32 EditorToolHandler::RemoveSpellEditorEffect(Zenith* zenith)
    {
        ::Editor::SpellEditorBackend* backend = GetSpellEditorBackend();
        zenith->Push(backend && backend->RemoveEffect(zenith->CheckVal<u32>(1)));
        return 1;
    }

    i32 EditorToolHandler::MoveSpellEditorEffect(Zenith* zenith)
    {
        ::Editor::SpellEditorBackend* backend = GetSpellEditorBackend();
        zenith->Push(backend && backend->MoveEffect(zenith->CheckVal<u32>(1), zenith->CheckVal<i32>(2)));
        return 1;
    }

    i32 EditorToolHandler::SetSpellEditorEffectField(Zenith* zenith)
    {
        ::Editor::SpellEditorBackend* backend = GetSpellEditorBackend();
        const char* field = zenith->CheckVal<const char*>(2);
        zenith->Push(backend && field && backend->SetEffectNumber(
            zenith->CheckVal<u32>(1), field, zenith->CheckVal<i64>(3)));
        return 1;
    }

    i32 EditorToolHandler::SetSpellEditorEffectParameter(Zenith* zenith)
    {
        ::Editor::SpellEditorBackend* backend = GetSpellEditorBackend();
        const u8 parameterIndex = zenith->CheckVal<u8>(2);
        zenith->Push(backend && parameterIndex > 0 && backend->SetEffectParameter(
            zenith->CheckVal<u32>(1), parameterIndex - 1, zenith->CheckVal<i32>(3)));
        return 1;
    }

    i32 EditorToolHandler::AddSpellEditorConstraint(Zenith* zenith)
    {
        ::Editor::SpellEditorBackend* backend = GetSpellEditorBackend();
        zenith->Push(backend ? backend->AddConstraint(zenith->CheckVal<u32>(1)) : 0u);
        return 1;
    }

    i32 EditorToolHandler::RemoveSpellEditorConstraint(Zenith* zenith)
    {
        ::Editor::SpellEditorBackend* backend = GetSpellEditorBackend();
        zenith->Push(backend && backend->RemoveConstraint(zenith->CheckVal<u32>(1)));
        return 1;
    }

    i32 EditorToolHandler::CreateSpellEditorConstraintGroup(Zenith* zenith)
    {
        ::Editor::SpellEditorBackend* backend = GetSpellEditorBackend();
        const char* name = zenith->CheckVal<const char*>(1);
        zenith->Push(backend && name ? backend->CreateConstraintGroup(
            name, zenith->CheckVal<u8>(2), zenith->CheckVal<u16>(3), zenith->CheckVal<u8>(4)) : 0u);
        return 1;
    }

    i32 EditorToolHandler::UpdateSpellEditorConstraintGroup(Zenith* zenith)
    {
        ::Editor::SpellEditorBackend* backend = GetSpellEditorBackend();
        const char* name = zenith->CheckVal<const char*>(2);
        zenith->Push(backend && name && backend->UpdateConstraintGroup(
            zenith->CheckVal<u32>(1), name, zenith->CheckVal<u8>(3),
            zenith->CheckVal<u16>(4), zenith->CheckVal<u8>(5)));
        return 1;
    }

    i32 EditorToolHandler::DeleteSpellEditorConstraintGroup(Zenith* zenith)
    {
        ::Editor::SpellEditorBackend* backend = GetSpellEditorBackend();
        zenith->Push(backend && backend->DeleteConstraintGroup(zenith->CheckVal<u32>(1)));
        return 1;
    }

    i32 EditorToolHandler::SetSpellEditorConstraintField(Zenith* zenith)
    {
        ::Editor::SpellEditorBackend* backend = GetSpellEditorBackend();
        const char* field = zenith->CheckVal<const char*>(2);
        zenith->Push(backend && field && backend->SetConstraintNumber(
            zenith->CheckVal<u32>(1), field, zenith->CheckVal<u64>(3)));
        return 1;
    }

    i32 EditorToolHandler::ResetSpellEditorConstraintField(Zenith* zenith)
    {
        ::Editor::SpellEditorBackend* backend = GetSpellEditorBackend();
        const char* field = zenith->CheckVal<const char*>(2);
        zenith->Push(backend && field && backend->ResetConstraintField(zenith->CheckVal<u32>(1), field));
        return 1;
    }

    i32 EditorToolHandler::CreateSpellEditorProcData(Zenith* zenith)
    {
        ::Editor::SpellEditorBackend* backend = GetSpellEditorBackend();
        const char* name = zenith->CheckVal<const char*>(2);
        zenith->Push(backend ? backend->CreateProcData(
            zenith->CheckVal<u32>(1), name ? name : "",
            zenith->CheckVal<u32>(3), zenith->CheckVal<u64>(4),
            zenith->CheckVal<u64>(5), zenith->CheckVal<u64>(6),
            zenith->CheckVal<f32>(7), zenith->CheckVal<f32>(8),
            zenith->CheckVal<u32>(9), zenith->CheckVal<i32>(10)) : 0u);
        return 1;
    }

    i32 EditorToolHandler::UpdateSpellEditorProcData(Zenith* zenith)
    {
        ::Editor::SpellEditorBackend* backend = GetSpellEditorBackend();
        const char* name = zenith->CheckVal<const char*>(3);
        zenith->Push(backend && backend->UpdateProcData(
            zenith->CheckVal<u32>(1), zenith->CheckVal<u32>(2), name ? name : "",
            zenith->CheckVal<u32>(4), zenith->CheckVal<u64>(5),
            zenith->CheckVal<u64>(6), zenith->CheckVal<u64>(7),
            zenith->CheckVal<f32>(8), zenith->CheckVal<f32>(9),
            zenith->CheckVal<u32>(10), zenith->CheckVal<i32>(11)));
        return 1;
    }

    i32 EditorToolHandler::DeleteSpellEditorProcData(Zenith* zenith)
    {
        ::Editor::SpellEditorBackend* backend = GetSpellEditorBackend();
        zenith->Push(backend && backend->DeleteProcData(zenith->CheckVal<u32>(1)));
        return 1;
    }

    i32 EditorToolHandler::AddSpellEditorProcLink(Zenith* zenith)
    {
        ::Editor::SpellEditorBackend* backend = GetSpellEditorBackend();
        zenith->Push(backend ? backend->AddProcLink(zenith->CheckVal<u32>(1)) : 0u);
        return 1;
    }

    i32 EditorToolHandler::RemoveSpellEditorProcLink(Zenith* zenith)
    {
        ::Editor::SpellEditorBackend* backend = GetSpellEditorBackend();
        zenith->Push(backend && backend->RemoveProcLink(zenith->CheckVal<u32>(1)));
        return 1;
    }

    i32 EditorToolHandler::SetSpellEditorProcLinkData(Zenith* zenith)
    {
        ::Editor::SpellEditorBackend* backend = GetSpellEditorBackend();
        zenith->Push(backend && backend->SetProcLinkProcData(
            zenith->CheckVal<u32>(1), zenith->CheckVal<u32>(2)));
        return 1;
    }

    i32 EditorToolHandler::SetSpellEditorProcLinkEffect(Zenith* zenith)
    {
        ::Editor::SpellEditorBackend* backend = GetSpellEditorBackend();
        zenith->Push(backend && backend->SetProcLinkEffectSelected(
            zenith->CheckVal<u32>(1), zenith->CheckVal<u32>(2), zenith->CheckVal<bool>(3)));
        return 1;
    }

    i32 EditorToolHandler::ValidateSpellEditorDraft(Zenith* zenith)
    {
        ::Editor::SpellEditorBackend* backend = GetSpellEditorBackend();
        if (!backend)
            return 0;
        PushDiagnostics(zenith, backend->Validate());
        return 1;
    }

    i32 EditorToolHandler::SubmitSpellEditorDraft(Zenith* zenith)
    {
        ::Editor::SpellEditorBackend* backend = GetSpellEditorBackend();
        zenith->Push(backend && backend->Submit());
        return 1;
    }

    i32 EditorToolHandler::DeleteSpellEditorSpell(Zenith* zenith)
    {
        ::Editor::SpellEditorBackend* backend = GetSpellEditorBackend();
        zenith->Push(backend && backend->DeleteSpell(zenith->CheckVal<u32>(1)));
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
