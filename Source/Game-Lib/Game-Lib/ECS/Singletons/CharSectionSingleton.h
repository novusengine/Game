#pragma once
#include <Base/Types.h>

#include <robinhood/robinhood.h>

namespace ECS::Singletons
{
    struct CharSectionSingleton
    {
    public:
        /*
            -- Key Structure --
            raceID         -- 4 bits
            sexID          -- 1 bit
            baseSection    -- 4 bits
            varationIndex  -- 5 bits
            colorIndex     -- 5 bits
            flags          -- 5 bits
        */

        robin_hood::unordered_map<u32, u32> keyToCharSectionID;
    };
}