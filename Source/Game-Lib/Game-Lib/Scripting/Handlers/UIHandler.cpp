#include "UIHandler.h"
#include "Game-Lib/Application/EnttRegistries.h"
#include "Game-Lib/ECS/Components/UI/Canvas.h"
#include "Game-Lib/ECS/Components/UI/EventInputInfo.h"
#include "Game-Lib/ECS/Components/UI/LayoutEventInfo.h"
#include "Game-Lib/ECS/Singletons/UISingleton.h"
#include "Game-Lib/ECS/Util/Transform2D.h"
#include "Game-Lib/ECS/Util/UIRefCleanup.h"
#include "Game-Lib/ECS/Util/UIUtil.h"
#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Rendering/Canvas/CanvasRenderer.h"
#include "Game-Lib/Scripting/UI/Box.h"
#include "Game-Lib/Scripting/UI/Canvas.h"
#include "Game-Lib/Scripting/UI/Panel.h"
#include "Game-Lib/Scripting/UI/Text.h"
#include "Game-Lib/Scripting/Util/ZenithUtil.h"
#include "Game-Lib/UI/Box.h"
#include "Game-Lib/Util/ServiceLocator.h"
#include "Game-Lib/Util/AssetPath.h"

#include <Base/CVarSystem/CVarSystem.h>
#include <Base/Util/StringUtils.h>

#include <Filesystem/PactStorage.h>

#include <MetaGen/Game/Lua/Lua.h>

#include <Scripting/LuaManager.h>
#include <Scripting/Zenith.h>

#include <entt/entt.hpp>
#include <lualib.h>
#include <xxhash/xxhash64.h>

#include <algorithm>
#include <cstring>

namespace
{
    AutoCVar_Int CVAR_SmoothUnitFrameBars(
        CVarCategory::Client,
        "smoothUnitFrameBars",
        "Smoothly interpolates unit frame health and resource bar fills",
        1,
        CVarFlags::EditCheckbox);
}

namespace Scripting::UI
{
    namespace
    {
        UIHandler* GetUIHandler()
        {
            LuaManager* luaManager = ServiceLocator::GetLuaManager();
            if (!luaManager)
                return nullptr;

            return luaManager->GetLuaHandler<UIHandler>(static_cast<LuaHandlerID>(MetaGen::Game::Lua::LuaHandlerTypeEnum::UI));
        }

        InputActionHandle GetAction(const char* name)
        {
            if (!name)
                return {};

            return ServiceLocator::GetInputActionSystem()->GetAction(XXHash64::hash(name, std::strlen(name), 0));
        }

        InputActionContextHandle GetActionContext(const char* name)
        {
            if (!name)
                return {};

            return ServiceLocator::GetInputActionSystem()->GetContext(XXHash64::hash(name, std::strlen(name), 0));
        }

        bool ReadUnsignedField(Zenith* zenith, i32 tableIndex, const char* fieldName, u32& value, bool required = false)
        {
            if (!zenith->GetTableField(fieldName, tableIndex))
                return !required;

            const bool valid = zenith->IsNumber(-1);
            if (valid)
                value = zenith->Get<u32>(-1);

            zenith->Pop();
            return valid;
        }

        bool ReadIntegerField(Zenith* zenith, i32 tableIndex, const char* fieldName, i32& value)
        {
            if (!zenith->GetTableField(fieldName, tableIndex))
                return true;

            const bool valid = zenith->IsNumber(-1);
            if (valid)
                value = zenith->Get<i32>(-1);

            zenith->Pop();
            return valid;
        }

        bool ReadBooleanField(Zenith* zenith, i32 tableIndex, const char* fieldName, bool& value)
        {
            if (!zenith->GetTableField(fieldName, tableIndex))
                return true;

            const bool valid = zenith->IsBoolean(-1);
            if (valid)
                value = zenith->ToBoolean(-1);

            zenith->Pop();
            return valid;
        }

        std::optional<InputBinding> ReadBinding(Zenith* zenith, i32 tableIndex)
        {
            if (!zenith->IsTable(tableIndex))
                return std::nullopt;

            u32 device = 0;
            u32 code = 0;
            u32 modifiers = static_cast<u32>(InputModifier::None);
            u32 modifierMatch = static_cast<u32>(ModifierMatch::Exact);
            if (!ReadUnsignedField(zenith, tableIndex, "device", device, true)
                || !ReadUnsignedField(zenith, tableIndex, "code", code, true)
                || !ReadUnsignedField(zenith, tableIndex, "modifiers", modifiers)
                || !ReadUnsignedField(zenith, tableIndex, "modifierMatch", modifierMatch))
            {
                return std::nullopt;
            }

            return InputActionSystem::TryCreateBinding(device, code, modifiers, modifierMatch);
        }

        bool ReadInputActionOptions(Zenith* zenith, i32 tableIndex, std::string& contextName, InputActionDesc& desc)
        {
            if (zenith->GetTop() < tableIndex || zenith->IsNil(tableIndex))
                return true;

            if (!zenith->IsTable(tableIndex))
                return false;

            if (zenith->GetTableField("context", tableIndex))
            {
                if (!zenith->IsString(-1))
                {
                    zenith->Pop();
                    return false;
                }

                contextName = zenith->Get<const char*>(-1);
                zenith->Pop();
            }

            u32 defaultReply = static_cast<u32>(desc.defaultReply);
            if (!ReadUnsignedField(zenith, tableIndex, "defaultReply", defaultReply)
                || defaultReply > static_cast<u32>(InputReply::Consumed)
                || !ReadBooleanField(zenith, tableIndex, "rebindable", desc.rebindable)
                || !ReadIntegerField(zenith, tableIndex, "categorySortOrder", desc.categorySortOrder)
                || !ReadIntegerField(zenith, tableIndex, "sortOrder", desc.sortOrder))
            {
                return false;
            }

            desc.defaultReply = static_cast<InputReply>(defaultReply);

            if (zenith->GetTableField("secondaryBinding", tableIndex))
            {
                desc.defaultBindings[1] = ReadBinding(zenith, -1);
                zenith->Pop();

                if (!desc.defaultBindings[1])
                    return false;
            }

            return true;
        }

        void PushBinding(Zenith* zenith, const InputBinding& binding)
        {
            zenith->CreateTable();
            zenith->AddTableField("device", static_cast<u32>(binding.control.device));
            zenith->AddTableField("code", static_cast<u32>(binding.control.code));
            zenith->AddTableField("modifiers", static_cast<u32>(binding.modifiers));
            zenith->AddTableField("modifierMatch", static_cast<u32>(binding.modifierMatch));
        }

        void AddBindingsField(Zenith* zenith, const char* fieldName, const std::array<std::optional<InputBinding>, InputActionDesc::MAX_BINDINGS>& bindings)
        {
            zenith->CreateTable();

            for (u32 bindingSlot = 0; bindingSlot < bindings.size(); bindingSlot++)
            {
                if (!bindings[bindingSlot])
                    continue;

                PushBinding(zenith, *bindings[bindingSlot]);
                zenith->SetTableKey(static_cast<i32>(bindingSlot + 1));
            }
            zenith->SetTableKey(fieldName);
        }

        void PushBindingChangeResult(Zenith* zenith, const InputBindingChangeResult& result)
        {
            InputActionSystem* inputActions = ServiceLocator::GetInputActionSystem();

            zenith->CreateTable();
            zenith->AddTableField("status", static_cast<u32>(result.status));
            zenith->AddTableField("succeeded", result.Succeeded());
            zenith->AddTableField("requestedPolicy", static_cast<u32>(result.requestedPolicy));
            zenith->AddTableField("appliedPolicy", static_cast<u32>(result.appliedPolicy));

            zenith->CreateTable();
            i32 conflictIndex = 1;
            for (const InputBindingConflict& conflict : result.conflicts)
            {
                const InputActionInfo* info = inputActions->GetActionInfo(conflict.action);
                zenith->CreateTable();
                zenith->AddTableField("action", info ? info->name.c_str() : "");
                zenith->AddTableField("slot", conflict.bindingSlot + 1);
                zenith->AddTableField("kind", static_cast<u32>(conflict.kind));
                zenith->SetTableKey(conflictIndex++);
            }
            zenith->SetTableKey("conflicts");

            zenith->CreateTable();
            i32 mutationIndex = 1;
            for (const InputBindingMutation& mutation : result.mutations)
            {
                const InputActionInfo* info = inputActions->GetActionInfo(mutation.action);
                zenith->CreateTable();
                zenith->AddTableField("action", info ? info->name.c_str() : "");
                zenith->AddTableField("slot", mutation.bindingSlot + 1);

                if (mutation.previousBinding)
                {
                    PushBinding(zenith, *mutation.previousBinding);
                    zenith->SetTableKey("previousBinding");
                }

                if (mutation.binding)
                {
                    PushBinding(zenith, *mutation.binding);
                    zenith->SetTableKey("binding");
                }

                zenith->SetTableKey(mutationIndex++);
            }
            zenith->SetTableKey("mutations");
        }
    }

    void UIHandler::Register(Zenith* zenith)
    {
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
        registry->ctx().emplace<ECS::Singletons::UISingleton>();

        // Release Lua-registry refs (SetOnMouseUp, RegisterLayoutRefresh, etc.)
        // when their owning entities are destroyed; otherwise the registry slot
        // leaks for the lifetime of the Lua state.
        registry->on_destroy<ECS::Components::UI::EventInputInfo>().connect<&ECS::Util::UIRefCleanup::ReleaseEventInputInfoRefs>();
        registry->on_destroy<ECS::Components::UI::LayoutEventInfo>().connect<&ECS::Util::UIRefCleanup::ReleaseLayoutEventInfoRefs>();

        // UI
        LuaMethodTable::Set(zenith, uiGlobalMethods, "UI");
        LuaMethodTable::Set(zenith, inputGlobalMethods, "Input");

        // Widgets
        Widget::Register(zenith);
        Canvas::Register(zenith);
        Panel::Register(zenith);
        Text::Register(zenith);

        // Utils
        Box::Register(zenith);

        CreateInputConstants(zenith);

        // Setup Cursor Canvas
        {
            auto& uiSingleton = registry->ctx().get<ECS::Singletons::UISingleton>();
            uiSingleton.cursorCanvasEntity = ECS::Util::UI::GetOrEmplaceCanvas(uiSingleton.cursorCanvas, registry, "CursorCanvas", vec2(0, 0), ivec2(48, 48), false);
        }
    }

    void UIHandler::Clear(Zenith* zenith)
    {
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
        auto& ctx = registry->ctx();
        auto& transformSystem = ECS::Transform2DSystem::Get(*registry);

        registry->view<ECS::Components::UI::Widget>().each([&transformSystem](entt::entity entity, ECS::Components::UI::Widget& widget)
        {
            if (widget.scriptWidget != nullptr)
            {
                delete widget.scriptWidget;
                widget.scriptWidget = nullptr;
            }

            if (transformSystem.HasParent(entity))
            {
                transformSystem.ClearParent(entity);
            }
        });

        registry->clear();

        transformSystem.ClearQueue();
        ServiceLocator::GetGameRenderer()->GetCanvasRenderer()->Clear();

        if (ctx.contains<ECS::Singletons::UISingleton>())
        {
            ECS::Singletons::UISingleton& uiSingleton = ctx.get<ECS::Singletons::UISingleton>();
            uiSingleton.panelTemplates.clear();
            uiSingleton.textTemplates.clear();

            uiSingleton.nameHashToCanvasEntity.clear();
            uiSingleton.templateHashToTextTemplateIndex.clear();
            uiSingleton.templateHashToPanelTemplateIndex.clear();
            uiSingleton.lastClickPosition = vec2(0, 0);
            uiSingleton.clickedEntity = entt::null;
            uiSingleton.hoveredEntity = entt::null;
            uiSingleton.focusedEntity = entt::null;
            uiSingleton.cursorCanvasEntity = entt::null;
            uiSingleton.allHoveredEntities.clear();
            uiSingleton.scriptWidgets.clear();
        }

        InputActionSystem* inputActions = ServiceLocator::GetInputActionSystem();
        for (ScriptInputConnection& scriptConnection : _inputConnections)
        {
            inputActions->Disconnect(scriptConnection.connection);
            Scripting::Util::Zenith::Unref(zenith, scriptConnection.callbackRef);
        }
        _inputConnections.clear();

        if (_bindingCaptureCallbackRef >= 0)
        {
            inputActions->CancelBindingCapture(false);
            Scripting::Util::Zenith::Unref(zenith, _bindingCaptureCallbackRef);
            _bindingCaptureCallbackRef = -1;
        }
    }

    // UI
    i32 UIHandler::RegisterPanelTemplate(Zenith* zenith)
    {
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
        ECS::Singletons::UISingleton& uiSingleton = registry->ctx().get<ECS::Singletons::UISingleton>();

        u32 panelTemplateIndex = static_cast<u32>(uiSingleton.panelTemplates.size());
        auto& panelTemplate = uiSingleton.panelTemplates.emplace_back();

        const char* templateName = zenith->CheckVal<const char*>(1);

        if (zenith->GetTableField("background", 2))
        {
            panelTemplate.background = zenith->CheckVal<const char*>(-1);
            panelTemplate.setFlags.background = 1;
            zenith->Pop();
        }

        if (zenith->GetTableField("foreground", 2))
        {
            panelTemplate.foreground = zenith->CheckVal<const char*>(-1);
            panelTemplate.setFlags.foreground = 1;
            zenith->Pop();
        }

        panelTemplate.color = Color::White;
        if (zenith->GetTableField("color", 2))
        {
            vec3 color = zenith->CheckVal<vec3>(-1);
            panelTemplate.color = Color(color.x, color.y, color.z);
            panelTemplate.setFlags.color = 1;
            zenith->Pop();
        }

        if (zenith->GetTableField("alpha", 2))
        {
            f32 alpha = zenith->CheckVal<f32>(-1);
            panelTemplate.color.a = alpha;
            panelTemplate.setFlags.color = 1;
            zenith->Pop();
        }

        if (zenith->GetTableField("cornerRadius", 2))
        {
            panelTemplate.cornerRadius = zenith->CheckVal<f32>(-1);
            panelTemplate.setFlags.cornerRadius = 1;
            zenith->Pop();
        }

        if (zenith->GetTableField("texCoords", 2))
        {
            ::UI::Box* box = zenith->GetUserData<::UI::Box>(-1);
            zenith->Pop();

            if (box)
            {
                panelTemplate.texCoords = *box;
            }
            else
            {
                panelTemplate.texCoords.min = vec2(0.0f, 0.0f);
                panelTemplate.texCoords.max = vec2(1.0f, 1.0f);
            }
            panelTemplate.setFlags.texCoords = 1;
        }

        if (zenith->GetTableField("nineSliceInsets", 2))
        {
            ::UI::Box* box = zenith->GetUserData<::UI::Box>(-1);
            zenith->Pop();

            if (box)
            {
                panelTemplate.nineSliceInsets = vec4(box->min, box->max);
            }
            else
            {
                panelTemplate.nineSliceInsets = vec4(0.0f);
            }
            panelTemplate.setFlags.nineSliceInsets = 1;
        }

        // Event Templates
        if (zenith->GetTableField("onClickTemplate", 2))
        {
            panelTemplate.onClickTemplate = zenith->CheckVal<const char*>(-1);
            zenith->Pop();
        }

        if (zenith->GetTableField("onHoverTemplate", 2))
        {
            panelTemplate.onHoverTemplate = zenith->CheckVal<const char*>(-1);
            zenith->Pop();
        }

        if (zenith->GetTableField("onUninteractableTemplate", 2))
        {
            panelTemplate.onUninteractableTemplate = zenith->CheckVal<const char*>(-1);
            zenith->Pop();
        }

        // Event Callbacks
        if (zenith->GetTableField("onMouseDown", 2))
        {
            if (zenith->IsFunction(-1))
            {
                panelTemplate.onMouseDownEvent = zenith->GetRef(-1);
            }
            zenith->Pop();
        }
        if (zenith->GetTableField("onMouseUp", 2))
        {
            if (zenith->IsFunction(-1))
            {
                panelTemplate.onMouseUpEvent = zenith->GetRef(-1);
            }
            zenith->Pop();
        }
        if (zenith->GetTableField("onMouseHeld", 2))
        {
            if (zenith->IsFunction(-1))
            {
                panelTemplate.onMouseHeldEvent = zenith->GetRef(-1);
            }
            zenith->Pop();
        }

        if (zenith->GetTableField("onHoverBegin", 2))
        {
            if (zenith->IsFunction(-1))
            {
                panelTemplate.onHoverBeginEvent = zenith->GetRef(-1);
            }
            zenith->Pop();
        }
        if (zenith->GetTableField("onHoverEnd", 2))
        {
            if (zenith->IsFunction(-1))
            {
                panelTemplate.onHoverEndEvent = zenith->GetRef(-1);
            }
            zenith->Pop();
        }
        if (zenith->GetTableField("onHoverHeld", 2))
        {
            if (zenith->IsFunction(-1))
            {
                panelTemplate.onHoverHeldEvent = zenith->GetRef(-1);
            }
            zenith->Pop();
        }

        if (zenith->GetTableField("onFocusBegin", 2))
        {
            if (zenith->IsFunction(-1))
            {
                panelTemplate.onFocusBeginEvent = zenith->GetRef(-1);
            }
            zenith->Pop();
        }
        if (zenith->GetTableField("onFocusEnd", 2))
        {
            if (zenith->IsFunction(-1))
            {
                panelTemplate.onFocusEndEvent = zenith->GetRef(-1);
            }
            zenith->Pop();
        }
        if (zenith->GetTableField("onFocusHeld", 2))
        {
            if (zenith->IsFunction(-1))
            {
                panelTemplate.onFocusHeldEvent = zenith->GetRef(-1);
            }
            zenith->Pop();
        }

        u32 templateNameHash = StringUtils::fnv1a_32(templateName, strlen(templateName));
        uiSingleton.templateHashToPanelTemplateIndex[templateNameHash] = panelTemplateIndex;

        return 0;
    }

    i32 UIHandler::RegisterTextTemplate(Zenith* zenith)
    {
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
        ECS::Singletons::UISingleton& uiSingleton = registry->ctx().get<ECS::Singletons::UISingleton>();

        u32 textTemplateIndex = static_cast<u32>(uiSingleton.textTemplates.size());
        auto& textTemplate = uiSingleton.textTemplates.emplace_back();

        const char* templateName = zenith->CheckVal<const char*>(1);

        const char* font = nullptr;
        if (zenith->GetTableField("font", 2))
        {
            textTemplate.font = zenith->CheckVal<const char*>(-1);
            textTemplate.setFlags.font = 1;
            zenith->Pop();
        }

        f32 size = 0.0f;
        if (zenith->GetTableField("size", 2))
        {
            textTemplate.size = zenith->CheckVal<f32>(-1);
            textTemplate.setFlags.size = 1;
            zenith->Pop();
        }

        textTemplate.color = Color::White;
        if (zenith->GetTableField("color", 2))
        {
            vec3 color = zenith->CheckVal<vec3>(-1);
            textTemplate.color = Color(color.x, color.y, color.z);
            textTemplate.setFlags.color = 1;
            zenith->Pop();
        }

        if (zenith->GetTableField("borderSize", 2))
        {
            textTemplate.borderSize = zenith->CheckVal<f32>(-1);
            textTemplate.setFlags.borderSize = 1;
            zenith->Pop();
        }

        textTemplate.borderColor = Color::White;
        if (zenith->GetTableField("borderColor", 2))
        {
            vec3 color = zenith->CheckVal<vec3>(-1);
            textTemplate.borderColor = Color(color.x, color.y, color.z);
            textTemplate.setFlags.borderColor = 1;
            zenith->Pop();
        }

        textTemplate.wrapWidth = 0.0f;
        if (zenith->GetTableField("wrapWidth", 2))
        {
            f32 wrapWidth = zenith->CheckVal<f32>(-1);
            wrapWidth = glm::max(0.0f, wrapWidth);

            textTemplate.wrapWidth = wrapWidth;
            textTemplate.setFlags.wrapWidth = wrapWidth > 0.0f;
            zenith->Pop();
        }

        textTemplate.wrapIndent = 0;
        if (zenith->GetTableField("wrapIndent", 2))
        {
            i32 wrapIndent = zenith->CheckVal<i32>(-1);
            wrapIndent = glm::max(0, wrapIndent);

            textTemplate.wrapIndent = static_cast<u8>(wrapIndent);
            textTemplate.setFlags.wrapIndent = wrapIndent > 0;
            zenith->Pop();
        }

        // Event Templates
        if (zenith->GetTableField("onClickTemplate", 2))
        {
            textTemplate.onClickTemplate = zenith->CheckVal<const char*>(-1);
            zenith->Pop();
        }

        if (zenith->GetTableField("onHoverTemplate", 2))
        {
            textTemplate.onHoverTemplate = zenith->CheckVal<const char*>(-1);
            zenith->Pop();
        }

        // Event Callbacks
        if (zenith->GetTableField("onMouseDown", 2))
        {
            if (zenith->IsFunction(-1))
            {
                textTemplate.onMouseDownEvent = zenith->GetRef(-1);
            }
            zenith->Pop();
        }
        if (zenith->GetTableField("onMouseUp", 2))
        {
            if (zenith->IsFunction(-1))
            {
                textTemplate.onMouseUpEvent = zenith->GetRef(-1);
            }
            zenith->Pop();
        }
        if (zenith->GetTableField("onMouseHeld", 2))
        {
            if (zenith->IsFunction(-1))
            {
                textTemplate.onMouseHeldEvent = zenith->GetRef(-1);
            }
            zenith->Pop();
        }

        if (zenith->GetTableField("onHoverBegin", 2))
        {
            if (zenith->IsFunction(-1))
            {
                textTemplate.onHoverBeginEvent = zenith->GetRef(-1);
            }
            zenith->Pop();
        }
        if (zenith->GetTableField("onHoverEnd", 2))
        {
            if (zenith->IsFunction(-1))
            {
                textTemplate.onHoverEndEvent = zenith->GetRef(-1);
            }
            zenith->Pop();
        }
        if (zenith->GetTableField("onHoverHeld", 2))
        {
            if (zenith->IsFunction(-1))
            {
                textTemplate.onHoverHeldEvent = zenith->GetRef(-1);
            }
            zenith->Pop();
        }

        if (zenith->GetTableField("onFocusBegin", 2))
        {
            if (zenith->IsFunction(-1))
            {
                textTemplate.onFocusBeginEvent = zenith->GetRef(-1);
            }
            zenith->Pop();
        }
        if (zenith->GetTableField("onFocusEnd", 2))
        {
            if (zenith->IsFunction(-1))
            {
                textTemplate.onFocusEndEvent = zenith->GetRef(-1);
            }
            zenith->Pop();
        }
        if (zenith->GetTableField("onFocusHeld", 2))
        {
            if (zenith->IsFunction(-1))
            {
                textTemplate.onFocusHeldEvent = zenith->GetRef(-1);
            }
            zenith->Pop();
        }

        u32 templateNameHash = StringUtils::fnv1a_32(templateName, strlen(templateName));
        uiSingleton.templateHashToTextTemplateIndex[templateNameHash] = textTemplateIndex;

        return 0;
    }

    i32 UIHandler::RegisterSendMessageToChatCallback(Zenith* zenith)
    {
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
        ECS::Singletons::UISingleton& uiSingleton = registry->ctx().get<ECS::Singletons::UISingleton>();

        if (zenith->IsNil(1))
        {
            // Unregister chat callback
            uiSingleton.sendMessageToChatCallback = -1;
        }

        if (!zenith->IsFunction(1))
            return 0;

        i32 eventID = zenith->GetRef(1);
        uiSingleton.sendMessageToChatCallback = eventID;

        return 0;
    }

    i32 UIHandler::GetCanvas(Zenith* zenith)
    {
        const char* canvasIdentifier = zenith->CheckVal<const char*>(1);
        if (canvasIdentifier == nullptr)
        {
            zenith->Push();
            return 1;
        }

        i32 posX = zenith->CheckVal<i32>(2);
        i32 posY = zenith->CheckVal<i32>(3);

        i32 sizeX = zenith->CheckVal<i32>(4);
        i32 sizeY = zenith->CheckVal<i32>(5);

        bool isRenderTexture = zenith->IsBoolean(6) ? zenith->ToBoolean(6) : false;

        Widget* widget = nullptr;

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
        entt::entity entity = ECS::Util::UI::GetOrEmplaceCanvas(widget, registry, canvasIdentifier, vec2(posX, posY), ivec2(sizeX, sizeY), isRenderTexture);

        Widget* pushWidget = zenith->PushUserData<Widget>([](void* x)
        {
            // Very sad canvas is gone now :(
        });
        memcpy(pushWidget, widget, sizeof(Widget));
        luaL_getmetatable(zenith->state, widget->metaTableName.c_str());
        lua_setmetatable(zenith->state, -2);

        return 1;
    }

    i32 UIHandler::GetMousePos(Zenith* zenith)
    {
        Renderer::Renderer* renderer = ServiceLocator::GetGameRenderer()->GetRenderer();
        InputSystem* inputSystem = ServiceLocator::GetInputSystem();

        const vec2& renderSize = renderer->GetRenderSize();
        auto mousePos = inputSystem->GetMousePosition();

        mousePos.y = renderSize.y - mousePos.y; // Flipped because UI is bottom-left origin
        mousePos = mousePos / renderSize;
        mousePos *= vec2(Renderer::Settings::UI_REFERENCE_WIDTH, Renderer::Settings::UI_REFERENCE_HEIGHT);

        zenith->Push(mousePos.x);
        zenith->Push(mousePos.y);

        return 2;
    }

    i32 UIHandler::GetTextureSize(Zenith* zenith)
    {
        Renderer::Renderer* renderer = ServiceLocator::GetGameRenderer()->GetRenderer();

        const char* texturePath = zenith->CheckVal<const char*>(1);
        if (texturePath == nullptr)
        {
            zenith->Push();
            zenith->Push();
        }
        else
        {
            auto* pactStorage = ServiceLocator::GetPactStorage();
            u64 textureNameHash = ::Util::AssetPath::Hash(texturePath);

            PACT::PactFileHandle fileHandle;
            if (pactStorage->ReadFile(textureNameHash, fileHandle) == PACT::PactReadResult::Success)
            {
                Renderer::DataTextureDesc textureDesc;
                textureDesc.hash = textureNameHash;
                textureDesc.data = reinterpret_cast<const u8*>(fileHandle.GetData());
                textureDesc.size = fileHandle.GetSize();

                Renderer::TextureID textureID = renderer->LoadDataTexture(textureDesc);

                Renderer::TextureBaseDesc baseDesc = renderer->GetDesc(textureID);
                zenith->Push(baseDesc.width);
                zenith->Push(baseDesc.height);
            }
            else
            {
                zenith->Push();
                zenith->Push();
            }

        }

        return 2;
    }

    i32 UIHandler::IsSmoothUnitFrameBarsEnabled(Zenith* zenith)
    {
        zenith->Push(CVAR_SmoothUnitFrameBars.Get() != 0);
        return 1;
    }

    i32 UIHandler::PixelsToTexCoord(Zenith* zenith)
    {
        i32 posX = zenith->CheckVal<i32>(1);
        i32 posY = zenith->CheckVal<i32>(2);

        i32 sizeX = zenith->CheckVal<i32>(3);
        i32 sizeY = zenith->CheckVal<i32>(4);

        sizeX = Math::Max(sizeX - 1, 1);
        sizeY = Math::Max(sizeY - 1, 1);

        vec2 texCoord = vec2(static_cast<f32>(posX) / static_cast<f32>(sizeX), static_cast<f32>(posY) / static_cast<f32>(sizeY));

        zenith->Push(texCoord.x);
        zenith->Push(texCoord.y);

        return 2;
    }
    i32 UIHandler::CalculateTextSize(Zenith* zenith)
    {
        const char* text = zenith->CheckVal<const char*>(1);
        if (text == nullptr)
        {
            luaL_error(zenith->state, "Expected text as parameter 1");
        }

        const char* templateName = zenith->CheckVal<const char*>(2);
        if (templateName == nullptr)
        {
            luaL_error(zenith->state, "Expected text template name as parameter 2");
        }

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
        ECS::Singletons::UISingleton& uiSingleton = registry->ctx().get<ECS::Singletons::UISingleton>();

        u32 templateNameHash = StringUtils::fnv1a_32(templateName, strlen(templateName));
        u32 textTemplateIndex = uiSingleton.templateHashToTextTemplateIndex[templateNameHash];

        const auto& textTemplate = uiSingleton.textTemplates[textTemplateIndex];

        Renderer::Renderer* renderer = ServiceLocator::GetGameRenderer()->GetRenderer();
        Renderer::Font* font = Renderer::Font::GetFont(renderer, textTemplate.font);

        std::string textStr = text;
        ECS::Util::UI::ReplaceTextNewLines(textStr);

        if (textTemplate.setFlags.wrapWidth)
        {
            textStr = ECS::Util::UI::GenWrapText(textStr, font, textTemplate.size, textTemplate.borderSize, textTemplate.wrapWidth, textTemplate.wrapIndent);
        }

        vec2 textSize = font->CalculateTextSize(textStr, textTemplate.size, textTemplate.borderSize);

        zenith->Push(textSize.x);
        zenith->Push(textSize.y);

        return 2;
    }

    i32 UIHandler::WrapText(Zenith* zenith)
    {
        const char* text = zenith->CheckVal<const char*>(1);
        if (text == nullptr)
        {
            luaL_error(zenith->state, "Expected text as parameter 1");
        }

        const char* templateName = zenith->CheckVal<const char*>(2);
        if (templateName == nullptr)
        {
            luaL_error(zenith->state, "Expected text template name as parameter 2");
        }

        f32 wrapWidth = zenith->CheckVal<f32>(3);

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
        ECS::Singletons::UISingleton& uiSingleton = registry->ctx().get<ECS::Singletons::UISingleton>();

        u32 templateNameHash = StringUtils::fnv1a_32(templateName, strlen(templateName));
        u32 textTemplateIndex = uiSingleton.templateHashToTextTemplateIndex[templateNameHash];

        const auto& textTemplate = uiSingleton.textTemplates[textTemplateIndex];
        if (wrapWidth == -1.0f)
        {
            wrapWidth = textTemplate.wrapWidth;
        }

        Renderer::Renderer* renderer = ServiceLocator::GetGameRenderer()->GetRenderer();
        Renderer::Font* font = Renderer::Font::GetFont(renderer, textTemplate.font);

        std::string textStr = text;
        if (wrapWidth == 0)
        {
            zenith->Push(text);
        }
        else
        {
            textStr = ECS::Util::UI::GenWrapText(text, font, textTemplate.size, textTemplate.borderSize, wrapWidth, textTemplate.wrapIndent);
            zenith->Push(textStr.c_str());
        }

        vec2 textSize = font->CalculateTextSize(textStr, textTemplate.size, textTemplate.borderSize);
        zenith->Push(textSize.x);
        zenith->Push(textSize.y);

        return 3;
    }

    i32 UIHandler::FocusWidget(Zenith* zenith)
    {
        Widget* widget = zenith->GetUserData<Widget>(1);
        if (widget == nullptr)
        {
            luaL_error(zenith->state, "Expected widget as parameter 1");
        }

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
        ECS::Util::UI::FocusWidgetEntity(registry, widget->entity);

        registry->emplace_or_replace<ECS::Components::UI::DirtyCanvasTag>(widget->canvasEntity);

        return 0;
    }

    i32 UIHandler::UnfocusWidget(Zenith* zenith)
    {
        Widget* widget = zenith->GetUserData<Widget>(1);
        if (widget == nullptr)
        {
            luaL_error(zenith->state, "Expected widget as parameter 1");
        }

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
        ECS::Singletons::UISingleton& uiSingleton = registry->ctx().get<ECS::Singletons::UISingleton>();

        if (widget->entity == uiSingleton.focusedEntity)
        {
            ECS::Util::UI::FocusWidgetEntity(registry, entt::null);
        }

        registry->emplace_or_replace<ECS::Components::UI::DirtyCanvasTag>(widget->canvasEntity);

        return 0;
    }

    i32 UIHandler::IsFocusedWidget(Zenith* zenith)
    {
        Widget* widget = zenith->GetUserData<Widget>(1);
        if (widget == nullptr)
        {
            luaL_error(zenith->state, "Expected widget as parameter 1");
        }

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
        ECS::Singletons::UISingleton& uiSingleton = registry->ctx().get<ECS::Singletons::UISingleton>();

        bool isFocusedWidget = widget->entity == uiSingleton.focusedEntity;
        zenith->Push(isFocusedWidget);

        return 1;
    }

    i32 UIHandler::WasJustFocusedWidget(Zenith* zenith)
    {
        Widget* widget = zenith->GetUserData<Widget>(1);
        if (widget == nullptr)
        {
            luaL_error(zenith->state, "Expected widget as parameter 1");
        }

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
        ECS::Singletons::UISingleton& uiSingleton = registry->ctx().get<ECS::Singletons::UISingleton>();

        bool wasJustFocusedWidget = widget->entity == uiSingleton.justFocusedEntity;
        zenith->Push(wasJustFocusedWidget);

        return 1;
    }

    i32 UIHandler::GetFocusedWidget(Zenith* zenith)
    {
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
        ECS::Singletons::UISingleton& uiSingleton = registry->ctx().get<ECS::Singletons::UISingleton>();

        if (uiSingleton.focusedEntity == entt::null)
        {
            return 0;
        }

        auto& widgetComp = registry->get<ECS::Components::UI::Widget>(uiSingleton.focusedEntity);
        zenith->PushLightUserData(widgetComp.scriptWidget);

        luaL_getmetatable(zenith->state, widgetComp.scriptWidget->metaTableName.c_str());
        lua_setmetatable(zenith->state, -2);

        return 1;
    }

    i32 UIHandler::IsHoveredWidget(Zenith* zenith)
    {
        Widget* widget = zenith->GetUserData<Widget>(1);
        if (widget == nullptr)
        {
            luaL_error(zenith->state, "Expected widget as parameter 1");
        }

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
        ECS::Singletons::UISingleton& uiSingleton = registry->ctx().get<ECS::Singletons::UISingleton>();

        bool isHoveredWidget = widget->entity == uiSingleton.hoveredEntity;
        zenith->Push(isHoveredWidget);

        return 1;
    }

    i32 UIHandler::DestroyWidget(Zenith* zenith)
    {
        Widget* widget = zenith->GetUserData<Widget>(1);
        if (widget == nullptr)
        {
            luaL_error(zenith->state, "Expected widget as parameter 1");
        }

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;

        auto& widgetComp = registry->get<ECS::Components::UI::Widget>(widget->entity);
        if (widgetComp.type != ECS::Components::UI::WidgetType::Panel && widgetComp.type != ECS::Components::UI::WidgetType::Text && widgetComp.type != ECS::Components::UI::WidgetType::Widget)
        {
            luaL_error(zenith->state, "Expected a Panel, Text or Widget for DestroyWidget");
        }

        registry->emplace_or_replace<ECS::Components::UI::DirtyCanvasTag>(widget->canvasEntity);

        if (!ECS::Util::UI::DestroyWidget(registry, widget->entity))
        {
            luaL_error(zenith->state, "Failed to destroy widget");
        }

        return 0;
    }

    i32 UIHandler::RegisterInputAction(Zenith* zenith)
    {
        InputActionDesc desc;
        desc.name = zenith->CheckVal<const char*>(1);
        desc.displayName = zenith->CheckVal<const char*>(2);
        desc.category = zenith->CheckVal<const char*>(3);
        desc.defaultBindings[0] = ReadBinding(zenith, 4);
        desc.defaultReply = InputReply::Consumed;

        std::string contextName = "Global";
        if (!desc.defaultBindings[0] || !ReadInputActionOptions(zenith, 5, contextName, desc))
        {
            zenith->Push(false);
            return 1;
        }

        InputActionSystem* inputActions = ServiceLocator::GetInputActionSystem();
        const InputActionContextHandle context = GetActionContext(contextName.c_str());
        if (!context.IsValid())
        {
            zenith->Push(false);
            return 1;
        }

        InputActionHandle action = GetAction(desc.name.c_str());
        if (action.IsValid())
        {
            const InputActionInfo* info = inputActions->GetActionInfo(action);
            const std::string& expectedDisplayName = desc.displayName.empty() ? desc.name : desc.displayName;
            const bool matchesExisting = info
                && info->name == desc.name
                && info->displayName == expectedDisplayName
                && info->category == desc.category
                && info->defaultBindings == desc.defaultBindings
                && info->context == context
                && info->defaultReply == desc.defaultReply
                && info->categorySortOrder == desc.categorySortOrder
                && info->sortOrder == desc.sortOrder
                && info->rebindable == desc.rebindable;
            zenith->Push(matchesExisting);
            return 1;
        }

        action = inputActions->RegisterAction(context, desc);
        zenith->Push(action.IsValid());
        return 1;
    }

    i32 UIHandler::ConnectInputAction(Zenith* zenith)
    {
        UIHandler* self = GetUIHandler();
        InputActionHandle action = GetAction(zenith->CheckVal<const char*>(1));
        if (!self || !action.IsValid() || !zenith->IsFunction(2))
        {
            zenith->Push(0u);
            return 1;
        }

        const i32 callbackRef = zenith->GetRef(2);
        InputActionConnection connection = ServiceLocator::GetInputActionSystem()->Connect(action, [zenith, callbackRef](const InputActionEvent& event)
        {
            if (!zenith || !zenith->state)
                return InputReply::Handled;

            lua_checkstack(zenith->state, 6);
            zenith->GetRawI(LUA_REGISTRYINDEX, callbackRef);
            zenith->Push(static_cast<u32>(event.phase));
            zenith->Push(static_cast<u32>(event.control.device));
            zenith->Push(static_cast<u32>(event.control.code));
            zenith->Push(static_cast<u32>(event.modifiers));
            zenith->Push(event.value);

            if (!zenith->PCall(5, 1))
                return InputReply::Handled;

            InputReply reply = InputReply::Handled;
            if (zenith->IsNumber(-1))
            {
                const u32 result = zenith->Get<u32>(-1);
                if (result <= static_cast<u32>(InputReply::Consumed))
                    reply = static_cast<InputReply>(result);
            }
            zenith->Pop();
            return reply;
        });

        if (!connection.IsValid())
        {
            Scripting::Util::Zenith::Unref(zenith, callbackRef);
            zenith->Push(0u);
            return 1;
        }

        u32 connectionID = self->_nextInputConnectionID++;
        if (self->_nextInputConnectionID == 0)
            self->_nextInputConnectionID = 1;

        self->_inputConnections.push_back({ connection, callbackRef, connectionID });
        zenith->Push(connectionID);
        return 1;
    }

    i32 UIHandler::DisconnectInputAction(Zenith* zenith)
    {
        UIHandler* self = GetUIHandler();
        const u32 connectionID = zenith->CheckVal<u32>(1);
        if (!self)
        {
            zenith->Push(false);
            return 1;
        }

        auto connection = std::find_if(self->_inputConnections.begin(), self->_inputConnections.end(), [connectionID](const ScriptInputConnection& candidate)
        {
            return candidate.id == connectionID;
        });

        if (connection == self->_inputConnections.end())
        {
            zenith->Push(false);
            return 1;
        }

        ServiceLocator::GetInputActionSystem()->Disconnect(connection->connection);
        Scripting::Util::Zenith::Unref(zenith, connection->callbackRef);
        self->_inputConnections.erase(connection);
        zenith->Push(true);
        return 1;
    }

    i32 UIHandler::SetInputBinding(Zenith* zenith)
    {
        InputActionHandle action = GetAction(zenith->CheckVal<const char*>(1));
        const u32 bindingSlot = zenith->CheckVal<u32>(2);
        std::optional<InputBinding> binding = ReadBinding(zenith, 3);

        InputBindingChangeResult result;
        if (action.IsValid() && bindingSlot > 0 && binding)
        {
            const InputBindingConflictPolicy policy = zenith->GetTop() >= 4 ? static_cast<InputBindingConflictPolicy>(zenith->CheckVal<u32>(4)) : InputBindingConflictPolicy::Reject;
            result = ServiceLocator::GetInputActionSystem()->SetBinding(action, bindingSlot - 1, binding, policy);
        }

        PushBindingChangeResult(zenith, result);
        return 1;
    }

    i32 UIHandler::ClearInputBinding(Zenith* zenith)
    {
        InputActionHandle action = GetAction(zenith->CheckVal<const char*>(1));
        const u32 bindingSlot = zenith->CheckVal<u32>(2);

        InputBindingChangeResult result;
        if (action.IsValid() && bindingSlot > 0)
        {
            const InputBindingConflictPolicy policy = zenith->GetTop() >= 3 ? static_cast<InputBindingConflictPolicy>(zenith->CheckVal<u32>(3)) : InputBindingConflictPolicy::Reject;
            result = ServiceLocator::GetInputActionSystem()->SetBinding(action, bindingSlot - 1, std::nullopt, policy);
        }

        PushBindingChangeResult(zenith, result);
        return 1;
    }

    i32 UIHandler::ResetInputBinding(Zenith* zenith)
    {
        InputActionHandle action = GetAction(zenith->CheckVal<const char*>(1));
        const u32 bindingSlot = zenith->CheckVal<u32>(2);

        InputBindingChangeResult result;
        if (action.IsValid() && bindingSlot > 0)
        {
            const InputBindingConflictPolicy policy = zenith->GetTop() >= 3 ? static_cast<InputBindingConflictPolicy>(zenith->CheckVal<u32>(3)) : InputBindingConflictPolicy::Reject;
            result = ServiceLocator::GetInputActionSystem()->ResetBinding(action, bindingSlot - 1, policy);
        }

        PushBindingChangeResult(zenith, result);
        return 1;
    }

    i32 UIHandler::GetInputBinding(Zenith* zenith)
    {
        InputActionHandle action = GetAction(zenith->CheckVal<const char*>(1));
        const u32 bindingSlot = zenith->CheckVal<u32>(2);

        const InputActionInfo* info = ServiceLocator::GetInputActionSystem()->GetActionInfo(action);
        if (!info || bindingSlot == 0 || bindingSlot > info->bindings.size() || !info->bindings[bindingSlot - 1])
        {
            zenith->Push();
            return 1;
        }

        PushBinding(zenith, *info->bindings[bindingSlot - 1]);
        return 1;
    }

    i32 UIHandler::GetInputActions(Zenith* zenith)
    {
        InputActionSystem* inputActions = ServiceLocator::GetInputActionSystem();
        zenith->CreateTable();

        i32 actionIndex = 1;
        for (InputActionHandle action : inputActions->GetActions())
        {
            const InputActionInfo* info = inputActions->GetActionInfo(action);
            const InputActionContextInfo* contextInfo = info ? inputActions->GetContextInfo(info->context) : nullptr;
            if (!info || !contextInfo)
                continue;

            zenith->CreateTable();
            zenith->AddTableField("name", info->name.c_str());
            zenith->AddTableField("displayName", info->displayName.c_str());
            zenith->AddTableField("category", info->category.c_str());
            zenith->AddTableField("context", contextInfo->name.c_str());
            zenith->AddTableField("defaultReply", static_cast<u32>(info->defaultReply));
            zenith->AddTableField("categorySortOrder", info->categorySortOrder);
            zenith->AddTableField("sortOrder", info->sortOrder);
            zenith->AddTableField("rebindable", info->rebindable);
            AddBindingsField(zenith, "defaultBindings", info->defaultBindings);
            AddBindingsField(zenith, "bindings", info->bindings);
            zenith->SetTableKey(actionIndex++);
        }

        return 1;
    }

    i32 UIHandler::IsInputActionDown(Zenith* zenith)
    {
        const InputActionHandle action = GetAction(zenith->CheckVal<const char*>(1));
        zenith->Push(ServiceLocator::GetInputActionSystem()->IsDown(action));
        return 1;
    }

    i32 UIHandler::WasInputActionPressed(Zenith* zenith)
    {
        const InputActionHandle action = GetAction(zenith->CheckVal<const char*>(1));
        zenith->Push(ServiceLocator::GetInputActionSystem()->WasPressed(action));
        return 1;
    }

    i32 UIHandler::WasInputActionReleased(Zenith* zenith)
    {
        const InputActionHandle action = GetAction(zenith->CheckVal<const char*>(1));
        zenith->Push(ServiceLocator::GetInputActionSystem()->WasReleased(action));
        return 1;
    }

    i32 UIHandler::FindInputBindingConflicts(Zenith* zenith)
    {
        InputActionHandle action = GetAction(zenith->CheckVal<const char*>(1));
        const u32 bindingSlot = zenith->CheckVal<u32>(2);
        std::optional<InputBinding> binding = ReadBinding(zenith, 3);

        InputBindingChangeResult result;
        if (action.IsValid() && bindingSlot > 0 && binding)
        {
            result.action = action;
            result.bindingSlot = bindingSlot - 1;
            result.conflicts = ServiceLocator::GetInputActionSystem()->FindBindingConflicts(action, bindingSlot - 1, *binding);
            result.status = result.conflicts.empty() ? InputBindingChangeStatus::Applied : InputBindingChangeStatus::AppliedWithConflicts;
        }

        PushBindingChangeResult(zenith, result);
        return 1;
    }

    i32 UIHandler::BeginInputBindingCapture(Zenith* zenith)
    {
        UIHandler* self = GetUIHandler();
        if (!self || !zenith->IsFunction(1) || self->_bindingCaptureCallbackRef >= 0)
        {
            zenith->Push(false);
            return 1;
        }

        const i32 callbackRef = zenith->GetRef(1);
        const bool started = ServiceLocator::GetInputActionSystem()->BeginBindingCapture([zenith, callbackRef](std::optional<InputBinding> binding)
        {
            UIHandler* currentHandler = GetUIHandler();
            if (!currentHandler || currentHandler->_bindingCaptureCallbackRef != callbackRef || !zenith || !zenith->state)
                return;

            currentHandler->_bindingCaptureCallbackRef = -1;
            lua_checkstack(zenith->state, 2);
            zenith->GetRawI(LUA_REGISTRYINDEX, callbackRef);
            if (binding)
            {
                PushBinding(zenith, *binding);
            }
            else
            {
                zenith->Push();
            }

            zenith->PCall(1, 0);
            Scripting::Util::Zenith::Unref(zenith, callbackRef);
        });

        if (!started)
        {
            Scripting::Util::Zenith::Unref(zenith, callbackRef);
            zenith->Push(false);
            return 1;
        }

        self->_bindingCaptureCallbackRef = callbackRef;
        zenith->Push(true);
        return 1;
    }

    i32 UIHandler::CancelInputBindingCapture(Zenith* zenith)
    {
        zenith->Push(ServiceLocator::GetInputActionSystem()->CancelBindingCapture());
        return 1;
    }

    i32 UIHandler::IsInputBindingCaptureActive(Zenith* zenith)
    {
        zenith->Push(ServiceLocator::GetInputActionSystem()->IsBindingCaptureActive());
        return 1;
    }

    i32 UIHandler::SaveInputBindings(Zenith* zenith)
    {
        zenith->Push(ServiceLocator::GetInputActionSystem()->SaveBindings());
        return 1;
    }

    i32 UIHandler::CreateInputContext(Zenith* zenith)
    {
        const char* name = zenith->CheckVal<const char*>(1);
        const i32 priority = zenith->CheckVal<i32>(2);
        const i32 sortOrder = zenith->GetTop() >= 3 ? zenith->CheckVal<i32>(3) : 0;

        InputActionSystem* inputActions = ServiceLocator::GetInputActionSystem();
        InputActionContextHandle context = GetActionContext(name);
        if (context.IsValid())
        {
            const InputActionContextInfo* info = inputActions->GetContextInfo(context);
            zenith->Push(info && info->priority == priority && info->sortOrder == sortOrder);
            return 1;
        }

        context = inputActions->CreateContext(name, priority, sortOrder);
        zenith->Push(context.IsValid());
        return 1;
    }

    i32 UIHandler::SetInputContextActive(Zenith* zenith)
    {
        InputActionContextHandle context = GetActionContext(zenith->CheckVal<const char*>(1));
        const bool result = context.IsValid() && ServiceLocator::GetInputActionSystem()->SetContextActive(context, zenith->CheckVal<bool>(2));
        zenith->Push(result);
        return 1;
    }

    i32 UIHandler::IsInputContextActive(Zenith* zenith)
    {
        const InputActionContextHandle context = GetActionContext(zenith->CheckVal<const char*>(1));
        zenith->Push(ServiceLocator::GetInputActionSystem()->IsContextActive(context));
        return 1;
    }

    i32 UIHandler::GetInputContexts(Zenith* zenith)
    {
        InputActionSystem* inputActions = ServiceLocator::GetInputActionSystem();
        zenith->CreateTable();

        i32 contextIndex = 1;
        for (InputActionContextHandle context : inputActions->GetContexts())
        {
            const InputActionContextInfo* info = inputActions->GetContextInfo(context);
            if (!info)
                continue;

            zenith->CreateTable();
            zenith->AddTableField("name", info->name.c_str());
            zenith->AddTableField("priority", info->priority);
            zenith->AddTableField("sortOrder", info->sortOrder);
            zenith->AddTableField("active", inputActions->IsContextActive(context));
            zenith->SetTableKey(contextIndex++);
        }

        return 1;
    }

    i32 UIHandler::IsSoftwareCursorEnabled(Zenith* zenith)
    {
        zenith->Push(ServiceLocator::GetInputSystem()->GetCursorMode() == CursorMode::Software);
        return 1;
    }

    i32 UIHandler::GetCursorState(Zenith* zenith)
    {
        GameRenderer* gameRenderer = ServiceLocator::GetGameRenderer();
        const u32 knownRevision = zenith->GetTop() > 0 ? zenith->CheckVal<u32>(1) : 0;
        if (knownRevision == gameRenderer->GetCursorRevision())
        {
            zenith->Push();
            return 1;
        }

        zenith->Push(gameRenderer->GetCursorRevision());
        zenith->Push(gameRenderer->GetCursorTexturePath());
        return 2;
    }

    void UIHandler::CallUIInputEvent(Zenith* zenith, i32 eventRef, UIInputEvent inputEvent, Widget* widget)
    {
        lua_checkstack(zenith->state, 3);
        zenith->GetRawI(LUA_REGISTRYINDEX, eventRef);
        zenith->Push(static_cast<u32>(inputEvent));
        zenith->PushLightUserData(widget);

        luaL_getmetatable(zenith->state, widget->metaTableName.c_str());
        lua_setmetatable(zenith->state, -2);

        zenith->PCall(2);
    }

    void UIHandler::CallUIInputEvent(Zenith* zenith, i32 eventRef, UIInputEvent inputEvent, Widget* widget, i32 value)
    {
        lua_checkstack(zenith->state, 4);
        zenith->GetRawI(LUA_REGISTRYINDEX, eventRef);
        zenith->Push(static_cast<u32>(inputEvent));
        zenith->PushLightUserData(widget);

        luaL_getmetatable(zenith->state, widget->metaTableName.c_str());
        lua_setmetatable(zenith->state, -2);

        zenith->Push(value);
        zenith->PCall(3);
    }

    void UIHandler::CallUIInputEvent(Zenith* zenith, i32 eventRef, UIInputEvent inputEvent, Widget* widget, i32 value1, const vec2& value2)
    {
        lua_checkstack(zenith->state, 6);
        zenith->GetRawI(LUA_REGISTRYINDEX, eventRef);
        zenith->Push(static_cast<u32>(inputEvent));
        zenith->PushLightUserData(widget);

        luaL_getmetatable(zenith->state, widget->metaTableName.c_str());
        lua_setmetatable(zenith->state, -2);

        zenith->Push(value1);
        zenith->Push(value2.x);
        zenith->Push(value2.y);
        zenith->PCall(5);
    }

    void UIHandler::CallUIInputEvent(Zenith* zenith, i32 eventRef, UIInputEvent inputEvent, Widget* widget, f32 value)
    {
        lua_checkstack(zenith->state, 4);
        zenith->GetRawI(LUA_REGISTRYINDEX, eventRef);
        zenith->Push(static_cast<u32>(inputEvent));
        zenith->PushLightUserData(widget);

        luaL_getmetatable(zenith->state, widget->metaTableName.c_str());
        lua_setmetatable(zenith->state, -2);

        zenith->Push(value);
        zenith->PCall(3);
    }

    void UIHandler::CallUIInputEvent(Zenith* zenith, i32 eventRef, UIInputEvent inputEvent, Widget* widget, const vec2& value)
    {
        lua_checkstack(zenith->state, 5);
        zenith->GetRawI(LUA_REGISTRYINDEX, eventRef);
        zenith->Push(static_cast<u32>(inputEvent));
        zenith->PushLightUserData(widget);

        luaL_getmetatable(zenith->state, widget->metaTableName.c_str());
        lua_setmetatable(zenith->state, -2);

        zenith->Push(value.x);
        zenith->Push(value.y);
        zenith->PCall(4);
    }

    bool UIHandler::CallKeyboardInputEvent(Zenith* zenith, i32 eventRef, Widget* widget, i32 key, i32 actionMask, i32 modifierMask)
    {
        lua_checkstack(zenith->state, 7);
        zenith->GetRawI(LUA_REGISTRYINDEX, eventRef);

        zenith->PushLightUserData(widget);
        luaL_getmetatable(zenith->state, widget->metaTableName.c_str());
        lua_setmetatable(zenith->state, -2);

        zenith->Push(static_cast<i32>(UIKeyboardEvent::Key));
        zenith->Push(key);
        zenith->Push(actionMask);
        zenith->Push(modifierMask);

        zenith->PCall(5, 1);
        bool result = zenith->CheckVal<bool>(-1);
        zenith->Pop();

        return result; // Return if we should consume the event or not
    }

    bool UIHandler::CallKeyboardInputEvent(Zenith* zenith, i32 eventRef, i32 key, i32 actionMask, i32 modifierMask)
    {
        lua_checkstack(zenith->state, 6);
        zenith->GetRawI(LUA_REGISTRYINDEX, eventRef);

        zenith->Push(static_cast<i32>(UIKeyboardEvent::Key));
        zenith->Push(key);
        zenith->Push(actionMask);
        zenith->Push(modifierMask);

        zenith->PCall(4, 1);
        bool result = zenith->CheckVal<bool>(-1);
        zenith->Pop();

        return result; // Return if we should consume the event or not
    }

    bool UIHandler::CallKeyboardUnicodeEvent(Zenith* zenith, i32 eventRef, Widget* widget, u32 unicode)
    {
        lua_checkstack(zenith->state, 5);
        zenith->GetRawI(LUA_REGISTRYINDEX, eventRef);

        zenith->PushLightUserData(widget);
        luaL_getmetatable(zenith->state, widget->metaTableName.c_str());
        lua_setmetatable(zenith->state, -2);

        zenith->Push(static_cast<i32>(UIKeyboardEvent::Unicode));
        zenith->Push(unicode);

        zenith->PCall(3, 1);
        bool result = zenith->CheckVal<bool>(1);
        zenith->Pop();

        return result; // Return if widget should consume the event or not
    }

    void UIHandler::CallSendMessageToChat(Zenith* zenith, i32 eventRef, const std::string& channel, const std::string& playerName, const std::string& text, bool isOutgoing)
    {
        lua_checkstack(zenith->state, 5);
        zenith->GetRawI(LUA_REGISTRYINDEX, eventRef);

        zenith->Push(channel);
        zenith->Push(playerName);
        zenith->Push(text);
        zenith->Push(isOutgoing);

        zenith->PCall(4);
    }

    void UIHandler::CreateInputConstants(Zenith* zenith)
    {
        {
            zenith->CreateTable("UIInputEvent");

            zenith->AddTableField("MouseDown", static_cast<u32>(UIInputEvent::MouseDown));
            zenith->AddTableField("MouseUp", static_cast<u32>(UIInputEvent::MouseUp));
            zenith->AddTableField("MouseHeld", static_cast<u32>(UIInputEvent::MouseHeld));

            zenith->AddTableField("HoverBegin", static_cast<u32>(UIInputEvent::HoverBegin));
            zenith->AddTableField("HoverEnd", static_cast<u32>(UIInputEvent::HoverEnd));
            zenith->AddTableField("HoverHeld", static_cast<u32>(UIInputEvent::HoverHeld));

            zenith->AddTableField("FocusBegin", static_cast<u32>(UIInputEvent::FocusBegin));
            zenith->AddTableField("FocusEnd", static_cast<u32>(UIInputEvent::FocusEnd));
            zenith->AddTableField("FocusHeld", static_cast<u32>(UIInputEvent::FocusHeld));

            zenith->Pop();
        }

        {
            zenith->CreateTable("UIKeyboardEvent");

            zenith->AddTableField("Key", static_cast<u32>(UIKeyboardEvent::Key));
            zenith->AddTableField("Unicode", static_cast<u32>(UIKeyboardEvent::Unicode));

            zenith->Pop();
        }

        {
            zenith->CreateTable("InputPhase");

            zenith->AddTableField("Pressed", static_cast<u32>(InputPhase::Pressed));
            zenith->AddTableField("Repeated", static_cast<u32>(InputPhase::Repeated));
            zenith->AddTableField("Released", static_cast<u32>(InputPhase::Released));
            zenith->AddTableField("Canceled", static_cast<u32>(InputPhase::Canceled));
            zenith->AddTableField("Triggered", static_cast<u32>(InputPhase::Triggered));

            zenith->Pop();
        }

        {
            zenith->CreateTable("InputReply");

            zenith->AddTableField("Ignored", static_cast<u32>(InputReply::Ignored));
            zenith->AddTableField("Handled", static_cast<u32>(InputReply::Handled));
            zenith->AddTableField("Consumed", static_cast<u32>(InputReply::Consumed));

            zenith->Pop();
        }

        {
            zenith->CreateTable("GameInputPriority");

            zenith->AddTableField("ImGui", GameInputPriority::ImGui);
            zenith->AddTableField("Modal", GameInputPriority::Modal);
            zenith->AddTableField("UI", GameInputPriority::UI);
            zenith->AddTableField("Editor", GameInputPriority::Editor);
            zenith->AddTableField("Camera", GameInputPriority::Camera);
            zenith->AddTableField("Global", GameInputPriority::Global);
            zenith->AddTableField("Gameplay", GameInputPriority::Gameplay);
            zenith->AddTableField("Debug", GameInputPriority::Debug);

            zenith->Pop();
        }

        {
            zenith->CreateTable("InputModifier");

            zenith->AddTableField("None", static_cast<u32>(InputModifier::None));
            zenith->AddTableField("Shift", static_cast<u32>(InputModifier::Shift));
            zenith->AddTableField("Control", static_cast<u32>(InputModifier::Control));
            zenith->AddTableField("Alt", static_cast<u32>(InputModifier::Alt));
            zenith->AddTableField("Super", static_cast<u32>(InputModifier::Super));

            zenith->Pop();
        }

        {
            zenith->CreateTable("InputDevice");

            zenith->AddTableField("Keyboard", static_cast<u32>(InputDevice::Keyboard));
            zenith->AddTableField("Mouse", static_cast<u32>(InputDevice::Mouse));
            zenith->AddTableField("MouseWheel", static_cast<u32>(InputDevice::MouseWheel));

            zenith->Pop();
        }

        {
            zenith->CreateTable("MouseWheelDirection");

            zenith->AddTableField("Up", static_cast<u32>(MouseWheelDirection::Up));
            zenith->AddTableField("Down", static_cast<u32>(MouseWheelDirection::Down));
            zenith->AddTableField("Left", static_cast<u32>(MouseWheelDirection::Left));
            zenith->AddTableField("Right", static_cast<u32>(MouseWheelDirection::Right));

            zenith->Pop();
        }

        {
            zenith->CreateTable("InputModifierMatch");

            zenith->AddTableField("Exact", static_cast<u32>(ModifierMatch::Exact));
            zenith->AddTableField("AtLeast", static_cast<u32>(ModifierMatch::AtLeast));
            zenith->AddTableField("Any", static_cast<u32>(ModifierMatch::Any));

            zenith->Pop();
        }

        {
            zenith->CreateTable("InputBindingConflictPolicy");

            zenith->AddTableField("Reject", static_cast<u32>(InputBindingConflictPolicy::Reject));
            zenith->AddTableField("Replace", static_cast<u32>(InputBindingConflictPolicy::Replace));
            zenith->AddTableField("Swap", static_cast<u32>(InputBindingConflictPolicy::Swap));
            zenith->AddTableField("Allow", static_cast<u32>(InputBindingConflictPolicy::Allow));

            zenith->Pop();
        }

        {
            zenith->CreateTable("InputBindingConflictKind");

            zenith->AddTableField("Direct", static_cast<u32>(InputBindingConflictKind::Direct));
            zenith->AddTableField("ShadowsRequested", static_cast<u32>(InputBindingConflictKind::ShadowsRequested));
            zenith->AddTableField("ShadowedByRequested", static_cast<u32>(InputBindingConflictKind::ShadowedByRequested));

            zenith->Pop();
        }

        {
            zenith->CreateTable("InputBindingChangeStatus");

            zenith->AddTableField("Applied", static_cast<u32>(InputBindingChangeStatus::Applied));
            zenith->AddTableField("AppliedWithConflicts", static_cast<u32>(InputBindingChangeStatus::AppliedWithConflicts));
            zenith->AddTableField("Queued", static_cast<u32>(InputBindingChangeStatus::Queued));
            zenith->AddTableField("RejectedConflict", static_cast<u32>(InputBindingChangeStatus::RejectedConflict));
            zenith->AddTableField("InvalidAction", static_cast<u32>(InputBindingChangeStatus::InvalidAction));
            zenith->AddTableField("InvalidSlot", static_cast<u32>(InputBindingChangeStatus::InvalidSlot));
            zenith->AddTableField("InvalidBinding", static_cast<u32>(InputBindingChangeStatus::InvalidBinding));
            zenith->AddTableField("InvalidPolicy", static_cast<u32>(InputBindingChangeStatus::InvalidPolicy));
            zenith->AddTableField("NotRebindable", static_cast<u32>(InputBindingChangeStatus::NotRebindable));

            zenith->Pop();
        }
    }

}
