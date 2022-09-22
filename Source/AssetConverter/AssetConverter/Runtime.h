#pragma once
#include <json/json.hpp>

#include <filesystem>
namespace fs = std::filesystem;

struct Runtime
{
public:
	struct Paths
	{
		fs::path executable;
		fs::path data;
		fs::path clientDB;
		fs::path texture;
		fs::path map;
		fs::path mapObject;
		fs::path complexModel;
	};

public:
	bool isInDebugMode = false;
	Paths paths = { };
	nlohmann::ordered_json json = { };
};