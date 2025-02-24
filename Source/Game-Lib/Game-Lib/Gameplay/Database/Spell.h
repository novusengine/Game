#pragma once
#include "Shared.h"
#include <Base/Types.h>

namespace Database::Spell
{
    struct Spell
    {
    public:
        ClientDB::StringRef name;
        ClientDB::StringRef description;
        ClientDB::StringRef auraDescription;
    };
}