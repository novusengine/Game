#include "ServiceLocator.h"

Runtime* ServiceLocator::_runtime = nullptr;
CascLoader* ServiceLocator::_cascLoader = nullptr;

Runtime* ServiceLocator::SetRuntime(Runtime* runtime)
{
	_runtime = runtime;
	return _runtime;
}

CascLoader* ServiceLocator::SetCascLoader(CascLoader* cascLoader)
{
	_cascLoader = cascLoader;
	return _cascLoader;
}