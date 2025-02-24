#pragma once
#include <Base/Types.h>

namespace ECS
{
    namespace Util::Database::Camera
    {
        bool Refresh();

        bool HasCameraSave(u32 cameraNameHash);
        u32 GetCameraSaveID(u32 cameraNameHash);

        bool RemoveCameraSave(u32 cameraNameHash);
        bool AddCameraSave(const std::string& cameraName);

        bool GenerateSaveLocation(const std::string& saveName, std::string& result);
        bool LoadSaveLocationFromBase64(const std::string& base64);
    }
}