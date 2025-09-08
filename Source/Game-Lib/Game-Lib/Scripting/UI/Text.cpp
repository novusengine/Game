#include "Text.h"

#include "Game-Lib/ECS/Components/UI/Canvas.h"
#include "Game-Lib/ECS/Components/UI/Clipper.h"
#include "Game-Lib/ECS/Components/UI/Text.h"
#include "Game-Lib/ECS/Components/UI/TextTemplate.h"
#include "Game-Lib/ECS/Components/UI/Widget.h"
#include "Game-Lib/ECS/Util/UIUtil.h"
#include "Game-Lib/ECS/Util/Transform2D.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Scripting/Zenith.h>

#include <entt/entt.hpp>

namespace Scripting::UI
{
    void Text::Register(Zenith* zenith)
    {
        LuaMetaTable<Text>::Register(zenith, "TextMetaTable");
        LuaMetaTable<Text>::Set(zenith, widgetMethods);
        LuaMetaTable<Text>::Set(zenith, widgetInputMethods);
        LuaMetaTable<Text>::Set(zenith, textMethods);
    }

    namespace TextMethods
    {
        i32 GetText(Zenith* zenith, Text* text)
        {
            entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;

            ECS::Components::UI::Text& textComponent = registry->get<ECS::Components::UI::Text>(text->entity);
            zenith->Push(textComponent.text);

            return 1;
        }
        i32 SetText(Zenith* zenith, Text* text)
        {
            const char* rawText = zenith->CheckVal<const char*>(2);
            if (text == nullptr)
            {
                return 0;
            }

            entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;

            ECS::Components::UI::Text& textComponent = registry->get<ECS::Components::UI::Text>(text->entity);
            textComponent.rawText = rawText;
            ECS::Util::UI::RefreshText(registry, text->entity, rawText);

            registry->emplace_or_replace<ECS::Components::UI::DirtyCanvasTag>(text->canvasEntity);
            ECS::Util::UI::RefreshClipper(registry, text->entity);

            return 0;
        }

        i32 GetRawText(Zenith* zenith, Text* text)
        {
            entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;

            ECS::Components::UI::Text& textComponent = registry->get<ECS::Components::UI::Text>(text->entity);
            zenith->Push(textComponent.rawText);

            return 1;
        }

        i32 GetSize(Zenith* zenith, Text* text)
        {
            entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
            const vec2& size = registry->get<ECS::Components::Transform2D>(text->entity).GetSize();

            zenith->Push(size.x);
            zenith->Push(size.y);
            return 2;
        }

        i32 GetFontSize(Zenith* zenith, Text* text)
        {
            entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;

            ECS::Components::UI::TextTemplate& textTemplate = registry->get<ECS::Components::UI::TextTemplate>(text->entity);
            zenith->Push(textTemplate.size);

            return 1;
        }

        i32 SetFontSize(Zenith* zenith, Text* text)
        {
            f32 size = zenith->CheckVal<f32>(2);

            entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;

            auto& textTemplate = registry->get<ECS::Components::UI::TextTemplate>(text->entity);
            textTemplate.size = size;

            registry->emplace_or_replace<ECS::Components::UI::DirtyWidgetTransform>(text->entity);
            ECS::Util::UI::RefreshClipper(registry, text->entity);

            return 0;
        }

        i32 GetWidth(Zenith* zenith, Text* text)
        {
            entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
            const vec2& size = registry->get<ECS::Components::Transform2D>(text->entity).GetSize();

            zenith->Push(size.x);
            return 1;
        }

        i32 GetHeight(Zenith* zenith, Text* text)
        {
            entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
            const vec2& size = registry->get<ECS::Components::Transform2D>(text->entity).GetSize();

            zenith->Push(size.y);
            return 1;
        }

        i32 GetColor(Zenith* zenith, Text* text)
        {
            entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
            auto& textTemplate = registry->get<ECS::Components::UI::TextTemplate>(text->entity);
            zenith->Push(vec3(textTemplate.color.r, textTemplate.color.g, textTemplate.color.b));

            return 1;
        }

        i32 SetColor(Zenith* zenith, Text* text)
        {
            vec3 color = zenith->CheckVal<vec3>(2);

            entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
            auto& textTemplate = registry->get<ECS::Components::UI::TextTemplate>(text->entity);
            textTemplate.color = Color(color.r, color.g, color.b, 1.0f);
            textTemplate.setFlags.color = true;

            registry->get_or_emplace<ECS::Components::UI::DirtyWidgetData>(text->entity);
            registry->emplace_or_replace<ECS::Components::UI::DirtyCanvasTag>(text->canvasEntity);
            return 0;
        }

        i32 SetAlpha(Zenith* zenith, Text* text)
        {
            f32 alpha = zenith->CheckVal<f32>(2);

            entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
            auto& textTemplate = registry->get<ECS::Components::UI::TextTemplate>(text->entity);
            textTemplate.color.a = alpha;
            textTemplate.setFlags.color = true;

            registry->get_or_emplace<ECS::Components::UI::DirtyWidgetData>(text->entity);
            return 0;
        }

        i32 GetWrapWidth(Zenith* zenith, Text* text)
        {
            entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
            auto& textTemplate = registry->get<ECS::Components::UI::TextTemplate>(text->entity);
            zenith->Push(textTemplate.wrapWidth);

            return 1;
        }

        i32 SetWrapWidth(Zenith* zenith, Text* text)
        {
            f32 wrapWidth = zenith->CheckVal<f32>(2);
            wrapWidth = glm::max(0.0f, wrapWidth);

            entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;

            auto& textTemplate = registry->get<ECS::Components::UI::TextTemplate>(text->entity);
            textTemplate.wrapWidth = wrapWidth;
            textTemplate.setFlags.wrapWidth = wrapWidth >= 0;

            ECS::Components::UI::Text& textComponent = registry->get<ECS::Components::UI::Text>(text->entity);
            ECS::Util::UI::RefreshText(registry, text->entity, textComponent.rawText);

            registry->emplace_or_replace<ECS::Components::UI::DirtyCanvasTag>(text->canvasEntity);

            return 0;
        }

        i32 GetWrapIndent(Zenith* zenith, Text* text)
        {
            entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;

            auto& textTemplate = registry->get<ECS::Components::UI::TextTemplate>(text->entity);
            zenith->Push(static_cast<u32>(textTemplate.wrapIndent));
            return 1;
        }

        i32 SetWrapIndent(Zenith* zenith, Text* text)
        {
            u32 wrapIndent = zenith->CheckVal<u32>(2);
            wrapIndent = glm::max(0u, wrapIndent);

            entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;

            auto& textTemplate = registry->get<ECS::Components::UI::TextTemplate>(text->entity);
            textTemplate.wrapIndent = wrapIndent;
            textTemplate.setFlags.wrapIndent = wrapIndent >= 0;
            ECS::Components::UI::Text& textComponent = registry->get<ECS::Components::UI::Text>(text->entity);
            ECS::Util::UI::RefreshText(registry, text->entity, textComponent.rawText);

            registry->emplace_or_replace<ECS::Components::UI::DirtyCanvasTag>(text->canvasEntity);
            ECS::Util::UI::RefreshClipper(registry, text->entity);

            return 0;
        }
    }
}