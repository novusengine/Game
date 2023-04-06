#pragma once
#include "BaseEditor.h"
#include <Base/Math/Geometry.h>
#include <Renderer/Descriptors/TextureDesc.h>
#include <entt/entt.hpp>

#include <Input/InputManager.h>

class Window;
class DebugRenderer;

struct NChunk;
struct NCell;

namespace Editor
{
	enum QueryObjectType
	{
		None = 0,
		Terrain,
		ModelOpaque,
		ModelTransparent
	};

	struct CModelAnimationEntry
	{
		u16 id;
		i16 variationID;
		std::string name;
	};

	class Inspector : public BaseEditor
	{
	public:
		struct SelectedTerrainData
		{
			Geometry::AABoundingBox boundingBox;

			vec2 adtCoords;
			vec2 chunkCoords;
			vec2 chunkWorldPos;
			vec2 cellCoords;

			u32 chunkId;
			u32 cellId;

			bool selectedChunk;
			NChunk* chunk;
			NCell* cell;

			//NDBC::AreaTable* zone;
			//NDBC::AreaTable* area;

			bool drawWireframe = false;
		};

		struct SelectedModelData
		{
			Geometry::AABoundingBox boundingBox;

			u32 instanceID;
			entt::entity entityID;
			u32 modelID;
			bool isOpaque;

			u32 numRenderBatches;
			i32 selectedRenderBatch;
			bool drawWireframe = false;
			bool wireframeEntireObject = true;

			u32 selectedAnimationEntry = 0;
			std::vector<CModelAnimationEntry> animationEntries;
		};

		struct TextureInspectorStatus
		{
			bool isOpen = false;
			bool r = true;
			bool g = true;
			bool b = true;
			bool a = true;
		};

	public:
		Inspector();

		virtual const char* GetName() override { return "Inspector"; }

		virtual void DrawImGui() override;

		void ClearSelection();
		void DirtySelection();

		void SelectTerrain(u32 chunkID, u32 cellID, bool selectChunk);
		void SelectModel(u32 instanceID);

		QueryObjectType GetSelectedObjectType() { return _selectedObjectType; }

		const SelectedTerrainData& GetSelectedTerrainData() { return _selectedTerrainData; }
		const SelectedModelData& GetSelectedModelData() { return _selectedModelData; }

		bool OnMouseClickLeft(i32 key, KeybindAction action, KeybindModifier modifier);

	private:
		void TerrainSelectionDrawImGui();
		void ModelSelectionDrawImGui();

		// Returns if the data was changed
		bool DrawTransformInspector(mat4x4& instanceMatrix, bool& finishedAction, mat4x4& outPreEditMatrix);
		void DrawTextureInspector(u32 textureIndex, Renderer::TextureID textureHash);

	private:
		u32 _activeToken = 0;
		u32 _queriedToken = 0;
		bool _selectedObjectDataInitialized = false;

		QueryObjectType _selectedObjectType = QueryObjectType::None;
		SelectedTerrainData _selectedTerrainData;
		SelectedModelData _selectedModelData;

		std::vector<TextureInspectorStatus> _textureStatus;
	};
}