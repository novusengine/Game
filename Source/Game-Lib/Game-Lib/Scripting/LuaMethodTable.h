#pragma once
#include <Base/Types.h>
#include <Base/Util/DebugHandler.h>

#include <lua.h>
#include <lualib.h>

namespace Scripting
{
    struct LuaMethod
    {
    public:
        const char* name;
        i32(*func)(lua_State*);
    };

    struct LuaNotMetaTable { };

    template <typename UserDataType>
    struct LuaMetaTable
    {
    public:
        static void Register(lua_State* state, const char* name)
        {
            if constexpr (std::is_same_v<UserDataType, LuaNotMetaTable>)
            {
                NC_ASSERT(false, "LuaMetaTable::Register should not be called when using LuaMethodTable")
            }

            NC_ASSERT(state, "Lua state is null");
            NC_ASSERT(name, "Name is null");

            // Ensure the table doesn't already exist
            if (!luaL_newmetatable(state, name))
            {
                lua_pop(state, 1);
                NC_LOG_ERROR("Lua Global Table already exists: {0}", name);
                return;
            }

            _name = name;

            // Create the meta table
            i32 table = lua_gettop(state);

            // Set ToString
            lua_pushcfunction(state, ToString, "ToString");
            lua_setfield(state, table, "__tostring");

            // Set __index index
            lua_pushvalue(state, table);
            lua_setfield(state, table, "__index");

            // Set __add index
            lua_pushcfunction(state, Add, "Add");
            lua_setfield(state, table, "__add");

            // Set __sub index
            lua_pushcfunction(state, Substract, "Substract");
            lua_setfield(state, table, "__sub");

            // Set __mul index
            lua_pushcfunction(state, Multiply, "Multiply");
            lua_setfield(state, table, "__mul");

            // Set __div index
            lua_pushcfunction(state, Divide, "Divide");
            lua_setfield(state, table, "__div");

            // Set __mod index
            lua_pushcfunction(state, Mod, "Mod");
            lua_setfield(state, table, "__mod");

            // Set __pow index
            lua_pushcfunction(state, Pow, "Pow");
            lua_setfield(state, table, "__pow");

            // Set __unm index
            lua_pushcfunction(state, UnaryMinus, "UnaryMinus");
            lua_setfield(state, table, "__unm");

            // Set __concat index
            lua_pushcfunction(state, Concat, "Concat");
            lua_setfield(state, table, "__concat");

            // Set __len index
            lua_pushcfunction(state, Length, "Length");
            lua_setfield(state, table, "__len");

            // Set __eq index
            lua_pushcfunction(state, Equal, "Equal");
            lua_setfield(state, table, "__eq");

            // Set __lt index
            lua_pushcfunction(state, Less, "Less");
            lua_setfield(state, table, "__lt");

            // Set __le index
            lua_pushcfunction(state, LessOrEqual, "LessOrEqual");
            lua_setfield(state, table, "__le");

            // Set __call index
            lua_pushcfunction(state, Call, "Call");
            lua_setfield(state, table, "__call");

            // Pop the table
            lua_pop(state, 1);
        }

        template <size_t N>
        static void Set(lua_State* state, const LuaMethod(&methodTable)[N], const char* globalName = nullptr)
        {
            constexpr bool isMethodTable = std::is_same_v<UserDataType, LuaNotMetaTable>;
            const char* name = nullptr;

            NC_ASSERT(state, "Lua state is null");
            NC_ASSERT(methodTable, "MethodTable is null");

            if constexpr (isMethodTable)
            {
                name = globalName;
            }
            else
            {
                NC_ASSERT(_name, "Name is null");
                NC_ASSERT(!globalName, "GlobalName cannot be set when calling SetMethodTable on a MetaTable, globalName is used when setting a global table, use Register to setup a MetaTable");
                name = _name;
            }

            bool isGlobal = false;

            // Get the correct table
            if (name)
            {
                lua_pushstring(state, name);
                lua_rawget(state, LUA_REGISTRYINDEX);
                bool globalIsTable = lua_istable(state, -1);

                if constexpr (isMethodTable)
                {
                    bool globalExists = lua_isnil(state, -1) == 0;
                    NC_ASSERT(!globalExists || globalIsTable, "Global key for name {0} does not exist or is not a table", name);

                    if (!globalExists)
                    {
                        // Pop the nil value
                        lua_pop(state, 1);

                        // Create the new table and set it in the global table
                        lua_newtable(state);
                        lua_setglobal(state, name);

                        // Move the table to the top of the stack
                        lua_getglobal(state, name);
                    }
                }
                else
                {
                    NC_ASSERT(globalIsTable, "Global key for name {0} does not exist or is not a table", name);
                }
            }
            else
            {
                isGlobal = true;
            }

            if (isGlobal)
            {
                for (size_t i = 0; i < N; ++i)
                {
                    lua_pushcfunction(state, methodTable[i].func, methodTable[i].name);
                    lua_setglobal(state, methodTable[i].name);
                }
            }
            else
            {
                i32 index = lua_gettop(state);

                for (size_t i = 0; i < N; ++i)
                {
                    lua_pushcfunction(state, methodTable[i].func, methodTable[i].name);
                    lua_setfield(state, index, methodTable[i].name);
                }

                lua_pop(state, 1);
            }
        }

    private:
        static i32 ToString(lua_State* state)
        {
            lua_pushfstring(state, "%s", _name);
            return 1;
        }

        static i32 ArithmeticError(lua_State* state) { luaL_error(state, "attempt to perform arithmetic on a %s value", _name);  return 1; }
        static i32 CompareError(lua_State* state) { luaL_error(state, "attempt to compare %s", _name);  return 1; }
        static i32 Add(lua_State* state) { return ArithmeticError(state); }
        static i32 Substract(lua_State* state) { return ArithmeticError(state); }
        static i32 Multiply(lua_State* state) { return ArithmeticError(state); }
        static i32 Divide(lua_State* state) { return ArithmeticError(state); }
        static i32 Mod(lua_State* state) { return ArithmeticError(state); }
        static i32 Pow(lua_State* state) { return ArithmeticError(state); }
        static i32 UnaryMinus(lua_State* state) { return ArithmeticError(state); }
        static i32 Concat(lua_State* state) { luaL_error(state, "attempt to concatenate a %s value", _name); return 1; }
        static i32 Length(lua_State* state) { luaL_error(state, "attempt to get length of a %s value", _name); return 1; }
        static i32 Equal(lua_State* state) { return CompareError(state); }
        static i32 Less(lua_State* state) { return CompareError(state); }
        static i32 LessOrEqual(lua_State* state) { return CompareError(state); }
        static i32 Call(lua_State* state) { luaL_error(state, "attempt to call a %s value", _name); return 1; }

    public:
        static const char* _name;
    };

    using LuaMethodTable = LuaMetaTable<LuaNotMetaTable>;

    template<typename UserDataType> const char* LuaMetaTable<UserDataType>::_name = nullptr;
}