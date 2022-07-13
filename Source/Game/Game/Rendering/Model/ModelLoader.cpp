#include "ModelLoader.h"

ModelLoader::ModelLoader(ModelRenderer* modelRenderer) : /*_modelRenderer(modelRenderer),*/ _requests() 
{
	_freedHandles.reserve(64);
}

void ModelLoader::Update(f32 deltaTime)
{
	ModelLoader::LoadRequestInternal loadRequest;

	while (_requests.try_dequeue(loadRequest))
	{

	}
}

bool ModelLoader::IsHandleActive(ModelLoader::Handle handle)
{
	u32 value = static_cast<u32>(handle);

	_handleMutex.lock_shared();
	bool result = _activeHandles.contains(value);
	_handleMutex.unlock_shared();

	return result;
}

ModelLoader::Handle ModelLoader::SendRequest(const LoadRequest& request)
{
	Handle handle = Handle::Invalid();
	u32 value = std::numeric_limits<u32>().max();

	// Get Handle
	{
		_handleMutex.lock();

		if (_freedHandles.size() > 0)
		{
			value = _freedHandles.back();
			_freedHandles.pop_back();
		}
		else
		{
			value = _maxHandleValue++;
		}

		_activeHandles.insert(value);
		_handleMutex.unlock();
	}

	ModelLoader::LoadRequestInternal loadRequest;
	{
		handle = static_cast<Handle>(value);
		loadRequest.path = request.path;
	}

	_requests.enqueue(loadRequest);

	return handle;
}

void ModelLoader::FreeHandle(ModelLoader::Handle handle)
{
	u32 value = static_cast<u32>(handle);

	_handleMutex.lock();

	if (_activeHandles.contains(value))
	{
		_activeHandles.erase(value);
		_freedHandles.push_back(value);
	}

	_handleMutex.unlock();
}
