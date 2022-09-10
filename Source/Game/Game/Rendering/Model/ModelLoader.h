#pragma once
#include <Base/Types.h>
#include <Base/Container/ConcurrentQueue.h>
#include <Base/Container/SafeUnorderedMap.h>

#include <type_safe/strong_typedef.hpp>

#include <vector>
#include <shared_mutex>

class ModelRenderer;
class ModelLoader
{
public:
	STRONG_TYPEDEF(Handle, u32);

	struct LoadDesc
	{
		vec3 position = vec3(0.0f, 0.0f, 0.0f);
		quat rotation = quat(vec3(0.0f, 0.0f, 0.0f));
		vec3 scale	  = vec3(1.0f, 1.0f, 1.0f);
		std::string path = "";
	};

	struct LoadHandle
	{
		Handle handle = Handle::Invalid();
		u32 instanceID = std::numeric_limits<u32>().max();
	};

private:
	struct LoadRequestInternal
	{
		u32 modelHash = std::numeric_limits<u32>().max();
		std::string path = "";
	};

	struct ModelLoadStatus
	{
		enum class Status
		{
			InProgress,
			Completed,
			Failed
		};

		Status status = Status::InProgress;
		Handle activeHandle = Handle::Invalid(); // Only valid when Status is equal to InProgress
		u32 modelDataID = 0;
	};
	
public:
	ModelLoader(ModelRenderer* modelRenderer);

	void Update(f32 deltaTime);

	bool IsHandleActive(ModelLoader::Handle handle);
	ModelLoader::LoadHandle AddInstance(const LoadDesc& loadDesc);

private:
	void FreeHandle(ModelLoader::Handle handle);

private:
	ModelRenderer* _modelRenderer = nullptr;
	moodycamel::ConcurrentQueue<LoadRequestInternal> _requests;

	SafeUnorderedMap<u32, ModelLoadStatus> _modelHashToModelLoadStatus;
	SafeUnorderedMap<u32, std::vector<u32>> _modelHashToInstances;

	std::shared_mutex _handleMutex;
	u32 _maxHandleValue = 0;
	std::vector<u32> _freedHandles;
	robin_hood::unordered_set<u32> _activeHandles;
};