#include "Panel.h"

#include "Game-Lib/Application/EnttRegistries.h"
#include "Game-Lib/ECS/Components/UI/Canvas.h"
#include "Game-Lib/ECS/Components/UI/PanelTemplate.h"
#include "Game-Lib/ECS/Components/UI/Widget.h"
#include "Game-Lib/ECS/Util/Transform2D.h"
#include "Game-Lib/ECS/Util/UIUtil.h"
#include "Game-Lib/Scripting/UI/Canvas.h"
#include "Game-Lib/Scripting/UI/Text.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Scripting/Zenith.h>

namespace Scripting::UI
{
    void Panel::Register(Zenith* zenith)
    {
        LuaMetaTable<Panel>::Register(zenith, "PanelMetaTable");

        LuaMetaTable<Panel>::Set(zenith, widgetCreationMethods);
        LuaMetaTable<Panel>::Set(zenith, widgetMethods);
        LuaMetaTable<Panel>::Set(zenith, widgetInputMethods);
        LuaMetaTable<Panel>::Set(zenith, panelMethods);
    }

    namespace PanelMethods
    {
        i32 GetSize(Zenith* zenith, Panel* panel)
        {
            entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
            const vec2& size = registry->get<ECS::Components::Transform2D>(panel->entity).GetSize();

            zenith->Push(size.x);
            zenith->Push(size.y);
            return 2;
        }

        i32 GetWidth(Zenith* zenith, Panel* panel)
        {
            entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
            const vec2& size = registry->get<ECS::Components::Transform2D>(panel->entity).GetSize();

            zenith->Push(size.x);
            return 1;
        }

        i32 GetHeight(Zenith* zenith, Panel* panel)
        {
            entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
            const vec2& size = registry->get<ECS::Components::Transform2D>(panel->entity).GetSize();

            zenith->Push(size.y);
            return 1;
        }

        i32 SetSize(Zenith* zenith, Panel* panel)
        {
            f32 x = zenith->CheckVal<f32>(2);
            f32 y = zenith->CheckVal<f32>(3);

            entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
            ECS::Transform2DSystem& ts = ECS::Transform2DSystem::Get(*registry);

            ts.SetSize(panel->entity, vec2(x, y));

            registry->emplace_or_replace<ECS::Components::UI::DirtyCanvasTag>(panel->canvasEntity);

            return 0;
        }

        i32 SetWidth(Zenith* zenith, Panel* panel)
        {
            f32 x = zenith->CheckVal<f32>(2);

            entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
            ECS::Transform2DSystem& ts = ECS::Transform2DSystem::Get(*registry);

            vec2 size = registry->get<ECS::Components::Transform2D>(panel->entity).GetSize();
            size.x = x;
            ts.SetSize(panel->entity, size);

            registry->emplace_or_replace<ECS::Components::UI::DirtyCanvasTag>(panel->canvasEntity);

            return 0;
        }

        i32 SetHeight(Zenith* zenith, Panel* panel)
        {
            f32 y = zenith->CheckVal<f32>(2);

            entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
            ECS::Transform2DSystem& ts = ECS::Transform2DSystem::Get(*registry);

            vec2 size = registry->get<ECS::Components::Transform2D>(panel->entity).GetSize();
            size.y = y;
            ts.SetSize(panel->entity, size);

            registry->emplace_or_replace<ECS::Components::UI::DirtyCanvasTag>(panel->canvasEntity);

            return 0;
        }

        i32 SetBackground(Zenith* zenith, Panel* panel)
        {
            entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
            auto& panelTemplate = registry->get<ECS::Components::UI::PanelTemplate>(panel->entity);
            registry->get_or_emplace<ECS::Components::UI::DirtyWidgetData>(panel->entity);

            if (zenith->IsString(2))
            {
                const char* texture = zenith->Get<const char*>(2);
                if (texture)
                {
                    panelTemplate.background = texture;
                    panelTemplate.backgroundRT = Renderer::TextureID::Invalid();
                    panelTemplate.backgroundRTEntity = entt::null;
                    panelTemplate.setFlags.background = true;
                    panelTemplate.setFlags.backgroundRT = false;
                }
            }
            else if (zenith->IsUserData(2))
            {
                Widget* widget = reinterpret_cast<Widget*>(zenith->ToUserData(2));
                if (widget->type == WidgetType::Canvas)
                {
                    auto& canvasComp = registry->get<ECS::Components::UI::Canvas>(widget->entity);

                    if (canvasComp.renderTexture == Renderer::TextureID::Invalid())
                    {
                        luaL_error(zenith->state, "Tried to SetBackground using a canvas that is not a RenderTarget Canvas");
                    }
                    
                    panelTemplate.backgroundRT = canvasComp.renderTexture;
                    panelTemplate.backgroundRTEntity = widget->entity;
                    panelTemplate.setFlags.background = false;
                    panelTemplate.setFlags.backgroundRT = true;
                }
                else
                {
                    luaL_error(zenith->state, "Expected parameter 2 to be either a string or a Canvas");
                }
            }
            else if (zenith->IsNil(2))
            {
                panelTemplate.background = "";
                panelTemplate.backgroundRT = Renderer::TextureID::Invalid();
                panelTemplate.backgroundRTEntity = entt::null;
                panelTemplate.setFlags.background = false;
                panelTemplate.setFlags.backgroundRT = false;
            }
            else
            {
                luaL_error(zenith->state, "Expected parameter 2 to be either a string or a Canvas");
            }

            registry->emplace_or_replace<ECS::Components::UI::DirtyCanvasTag>(panel->canvasEntity);

            return 0;
        }

        i32 SetForeground(Zenith* zenith, Panel* panel)
        {
            entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
            auto& panelTemplate = registry->get<ECS::Components::UI::PanelTemplate>(panel->entity);
            registry->get_or_emplace<ECS::Components::UI::DirtyWidgetData>(panel->entity);

            const char* texture = zenith->IsString(2) ? zenith->Get<const char*>(2) : nullptr;
            if (texture)
            {
                panelTemplate.foreground = texture;
                panelTemplate.setFlags.foreground = true;
            }
            else
            {
                panelTemplate.foreground = "";
                panelTemplate.setFlags.foreground = false;
            }

            registry->emplace_or_replace<ECS::Components::UI::DirtyCanvasTag>(panel->canvasEntity);

            return 0;
        }

        i32 SetTexCoords(Zenith* zenith, Panel* panel)
        {
            entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
            auto& panelTemplate = registry->get<ECS::Components::UI::PanelTemplate>(panel->entity);
            registry->get_or_emplace<ECS::Components::UI::DirtyWidgetData>(panel->entity);
            registry->get_or_emplace<ECS::Components::UI::DirtyWidgetTransform>(panel->entity);

            f32 minX = zenith->CheckVal<f32>(2);
            f32 minY = zenith->CheckVal<f32>(3);
            f32 maxX = zenith->CheckVal<f32>(4);
            f32 maxY = zenith->CheckVal<f32>(5);

            panelTemplate.setFlags.texCoords = true;
            panelTemplate.texCoords.min = vec2(minX, minY);
            panelTemplate.texCoords.max = vec2(maxX, maxY);

            registry->emplace_or_replace<ECS::Components::UI::DirtyCanvasTag>(panel->canvasEntity);

            return 0;
        }

        i32 SetColor(Zenith* zenith, Panel* panel)
        {
            entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
            auto& panelTemplate = registry->get<ECS::Components::UI::PanelTemplate>(panel->entity);
            registry->get_or_emplace<ECS::Components::UI::DirtyWidgetData>(panel->entity);

            vec3 colorVec = zenith->CheckVal<vec3>(2);
            f32 alpha = zenith->IsNumber(3) ? zenith->Get<f32>(3) : -1.0f;

            Color colorWithAlpha = Color(colorVec.r, colorVec.g, colorVec.b, panelTemplate.color.a);
            if (alpha >= 0.0f)
            {
                colorWithAlpha.a = alpha;
            }
            panelTemplate.color = colorWithAlpha;
            panelTemplate.setFlags.color = 1;

            registry->emplace_or_replace<ECS::Components::UI::DirtyCanvasTag>(panel->canvasEntity);

            return 0;
        }

        i32 SetAlpha(Zenith* zenith, Panel* panel)
        {
            entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
            auto& panelTemplate = registry->get<ECS::Components::UI::PanelTemplate>(panel->entity);
            registry->get_or_emplace<ECS::Components::UI::DirtyWidgetData>(panel->entity);

            f32 alpha = zenith->CheckVal<f32>(2);
            panelTemplate.color.a = alpha;
            panelTemplate.setFlags.color = 1;

            registry->emplace_or_replace<ECS::Components::UI::DirtyCanvasTag>(panel->canvasEntity);

            return 0;
        }
    }
}