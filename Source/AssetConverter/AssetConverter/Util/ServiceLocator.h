#pragma once

struct Runtime;
class CascLoader;
class ServiceLocator
{
public:
	static Runtime* GetRuntime() { return _runtime; }
	static Runtime* SetRuntime(Runtime* runtime);

	static CascLoader* GetCascLoader() { return _cascLoader; }
	static CascLoader* SetCascLoader(CascLoader* cascLoader);

private:
	static Runtime* _runtime;
	static CascLoader* _cascLoader;
};