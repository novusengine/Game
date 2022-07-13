#pragma once
#include <Base/Types.h>
#include <Base/Container/ConcurrentQueue.h>

#include <type_safe/strong_typedef.hpp>
#include <robinhood/robinhood.h>

#include <vector>
#include <shared_mutex>

class ModelRenderer;
class ModelLoader
{
public:
	STRONG_TYPEDEF(Handle, u32);

	struct LoadRequest
	{
		std::string path;
	};

private:
	struct LoadRequestInternal
	{
		Handle handle;
		std::string path;
	};
	
public:
	ModelLoader(ModelRenderer* modelRenderer);

	void Update(f32 deltaTime);

	bool IsHandleActive(ModelLoader::Handle handle);
	ModelLoader::Handle SendRequest(const LoadRequest& request);

private:
	void FreeHandle(ModelLoader::Handle handle);

private:
	//ModelRenderer* _modelRenderer;
	moodycamel::ConcurrentQueue<LoadRequestInternal> _requests;

	std::shared_mutex _handleMutex;
	u32 _maxHandleValue = 0;
	std::vector<u32> _freedHandles;
	robin_hood::unordered_set<u32> _activeHandles;
};