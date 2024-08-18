#include "UIUtil.h"
#include "Game/Application/EnttRegistries.h"
#include "Game/ECS/Components/Name.h"
#include "Game/ECS/Components/UI/Canvas.h"
#include "Game/ECS/Components/UI/Panel.h"
#include "Game/ECS/Components/UI/Text.h"
#include "Game/ECS/Components/UI/Widget.h"
#include "Game/ECS/Singletons/UISingleton.h"
#include "Game/ECS/Util/Transform2D.h"
#include "Game/Rendering/GameRenderer.h"
#include "Game/Util/ServiceLocator.h"

#include <Renderer/Font.h>

#include <Base/Util/StringUtils.h>

#include <entt/entt.hpp>

namespace ECS::Util
{
	namespace UI
	{
		entt::entity GetOrEmplaceCanvas(entt::registry* registry, const char* name, vec2 pos, ivec2 size)
		{
			ECS::Singletons::UISingleton& uiSingleton = registry->ctx().get<ECS::Singletons::UISingleton>();

			u32 nameHash = StringUtils::fnv1a_32(name, strlen(name));

			if (uiSingleton.nameHashToCanvasEntity.contains(nameHash))
			{
				entt::entity entity = uiSingleton.nameHashToCanvasEntity[nameHash];
				return entity;
			}

			return CreateCanvas(registry, name, pos, size);
		}

		entt::entity CreateCanvas(entt::registry* registry, const char* name, vec2 pos, ivec2 size, entt::entity parent)
		{
			auto& ctx = registry->ctx();
			auto& transform2DSystem = Transform2DSystem::Get(*registry);

			entt::entity entity = registry->create();

			// Transform
			auto& transformComp = registry->emplace<ECS::Components::Transform2D>(entity);

			if (parent != entt::null)
			{
				transform2DSystem.ParentEntityTo(parent, entity);
			}
			else
			{
				registry->emplace<ECS::Components::UI::WidgetRoot>(entity);
			}
			transform2DSystem.SetLocalPosition(entity, pos);
			transform2DSystem.SetSize(entity, size);

			// Name
			auto& nameComp = registry->emplace<ECS::Components::Name>(entity);
			nameComp.fullName = name;
			nameComp.name = name;
			nameComp.nameHash = StringUtils::fnv1a_32(name, strlen(name));

			// Widget
			auto& widgetComp = registry->emplace<ECS::Components::UI::Widget>(entity);
			widgetComp.type = ECS::Components::UI::WidgetType::Canvas;

			// Canvas
			//auto& canvasComp = registry->emplace<ECS::Components::UI::Canvas>(entity);

			return entity;
		}

		entt::entity CreatePanel(entt::registry* registry, vec2 pos, ivec2 size, u32 layer, const char* templateName, entt::entity parent)
		{
			auto& ctx = registry->ctx();
			auto& transform2DSystem = Transform2DSystem::Get(*registry);
			auto& uiSingleton = registry->ctx().get<ECS::Singletons::UISingleton>();

			entt::entity entity = registry->create();

			// Transform
			auto& transformComp = registry->emplace<ECS::Components::Transform2D>(entity);

			transform2DSystem.ParentEntityTo(parent, entity);
			transform2DSystem.SetLocalPosition(entity, pos);
			transform2DSystem.SetSize(entity, size);

			// Widget
			auto& widgetComp = registry->emplace<ECS::Components::UI::Widget>(entity);
			widgetComp.type = ECS::Components::UI::WidgetType::Panel;

			registry->emplace<ECS::Components::UI::DirtyWidget>(entity);

			// Panel
			auto& panelComp = registry->emplace<ECS::Components::UI::Panel>(entity);
			panelComp.layer = layer;

			u32 templateNameHash = StringUtils::fnv1a_32(templateName, strlen(templateName));
			if (!uiSingleton.templateHashToPanelTemplateIndex.contains(templateNameHash))
			{
				NC_LOG_ERROR("UI: Tried to create panel with template name '{}' but no template with that name has been registered", templateName);
				return entity;
			}

			u32 templateIndex = uiSingleton.templateHashToPanelTemplateIndex[templateNameHash];
			panelComp.templateIndex = templateIndex;
			return entity;
		}

		entt::entity CreateText(entt::registry* registry, const char* text, vec2 pos, u32 layer, const char* templateName, entt::entity parent)
		{
			Renderer::Renderer* renderer = ServiceLocator::GetGameRenderer()->GetRenderer();

			auto& ctx = registry->ctx();
			auto& transform2DSystem = Transform2DSystem::Get(*registry);
			auto& uiSingleton = registry->ctx().get<ECS::Singletons::UISingleton>();

			entt::entity entity = registry->create();

			// Transform
			auto& transformComp = registry->emplace<ECS::Components::Transform2D>(entity);

			transform2DSystem.ParentEntityTo(parent, entity);
			transform2DSystem.SetLocalPosition(entity, pos);

			// Widget
			auto& widgetComp = registry->emplace<ECS::Components::UI::Widget>(entity);
			widgetComp.type = ECS::Components::UI::WidgetType::Text;

			registry->emplace<ECS::Components::UI::DirtyWidget>(entity);

			// Text
			auto& textComp = registry->emplace<ECS::Components::UI::Text>(entity);
			textComp.text = text;
			textComp.layer = layer;

			u32 templateNameHash = StringUtils::fnv1a_32(templateName, strlen(templateName));
			if (!uiSingleton.templateHashToTextTemplateIndex.contains(templateNameHash))
			{
				NC_LOG_ERROR("UI: Tried to create text with template name '{}' but no template with that name has been registered", templateName);
				return entity;
			}

			u32 templateIndex = uiSingleton.templateHashToTextTemplateIndex[templateNameHash];
			textComp.templateIndex = templateIndex;

			const std::string& fontPath = uiSingleton.textTemplates[templateIndex].font;
			f32 fontSize = uiSingleton.textTemplates[templateIndex].size;
			Renderer::Font* font = Renderer::Font::GetFont(renderer, fontPath, fontSize);

			vec2 textSize = font->CalculateTextSize(text);
			transform2DSystem.SetSize(entity, textSize);

			return entity;
		}
	}
}