#pragma once
#include <Base/Types.h>
#include <Base/Memory/StackAllocator.h>

#include <robinhood/robinhood.h>
#include <entt/entt.hpp>
#include <map>

namespace ECS::Singletons
{
    struct InputSingleton
    {
    public:
        InputSingleton() {}

        // Input handling for scripts
        std::vector<i32> globalKeyboardEvents;

        robin_hood::unordered_map<u32, u32> eventIDToKeyboardEventIndex; // Maps event ID to index in globalKeyboardEvents
    };
}