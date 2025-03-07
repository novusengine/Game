#pragma once
#include <Base/Types.h>
#include <FileFormat/Novus/ClientDB/Definitions.h>

namespace Database::Shared
{
    struct CameraSave
    {
    public:
        ClientDB::StringRef name;
        ClientDB::StringRef code;
    };

    struct Cursor
    {
    public:
        ClientDB::StringRef name;
        ClientDB::StringRef texture;
    };

    struct Icon
    {
    public:
        ClientDB::StringRef texture;
    };
}