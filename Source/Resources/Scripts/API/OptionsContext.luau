-- Category / Group / Section

-- OptionType
local optionType : OptionType = {
    InputBox = 0,
    Checkbox = 1,
    Slider = 2,
    ComboBox = 3,
}
OptionType = optionType;

-- OptionCategory
local optionCategoryType : OptionCategoryType = {
    Gameplay = "Gameplay",
    Keybindings = "Keybindings",
    Accessibility = "Accessibility",
    System = "System",
}
OptionCategoryType = optionCategoryType;

-- Option
type Callback =
{
    version : number,
    callback : { (self : Option, value : any) -> boolean },
}

type Option = 
{
    name : string,
    type : OptionType,

    defaultValue : any,
    value : any,

    settings : table,

    onValueChangedCallbacks : { Callback },
    activeCallbacks : { number },
    inactiveCallbacks : { number },

    RegisterOnValueChanged : (self : Option, callback : (self : Option, value : any) -> boolean) -> (),
    InvokeOnValueChanged : (self : Option, value : any) -> boolean,
}

-- OptionsSection
type OptionsSection =
{
    name : string,
    options : LookupTable<Option>,
}

-- OptionsGroup
type OptionsGroup =
{
    name : string,
    sections : LookupTable<OptionsSection>,
}

-- OptionsCategory
type OptionsCategory =
{
    name : string,
    groups : LookupTable<OptionsGroup>,
}

-- Context
type OptionsContext =
{
    categories : LookupTable<OptionsCategory>,
}

-- LookupTable
type LookupTable<T> = {
    nameToIndex : { [string]: number },
    list : { T },
}

-- Metatables
local OptionMetatable = {
    Get = function(self : Option) : any
        return self.value;
    end,
    GetDefault = function(self : Option) : any
        return self.defaultValue;
    end,
    Set = function(self : Option, value : any) : ()
        self.value = value;
    end,
    Reset = function(self : Option) : ()
        self.value = self.defaultValue;
    end,
    RegisterOnValueChanged = function(self : Option, callback : (self : Option, value : any) -> boolean) : ()
        -- Check if we have an inactiveCallback to reuse
        local index = nil;
        local version = 1;
        if (#self.inactiveCallbacks > 0) then
            index = self.inactiveCallbacks[#self.inactiveCallbacks];
            table.remove(self.inactiveCallbacks, #self.inactiveCallbacks);
            table.insert(self.activeCallbacks, index);

            local callbackTable = self.onValueChangedCallbacks[index];
            callbackTable.version += 1;
            version = callbackTable.version;
            callbackTable.callback = callback;
        else
            index = #self.onValueChangedCallbacks + 1;

            local callbackTable : Callback = {
                version = version,
                callback = callback,
            }

            table.insert(self.onValueChangedCallbacks, callbackTable);
            table.insert(self.activeCallbacks, index);
        end

        local cancelFunction = function()
            print("Cancel");
            for i, value in self.activeCallbacks do
                if (value == index) then
                    local callbackTable = self.onValueChangedCallbacks[index];
                    if (callbackTable.version == version) then
                        table.remove(self.activeCallbacks, i);
                        table.insert(self.inactiveCallbacks, index);
                        break;
                    end
                end
            end
        end

        return cancelFunction;
    end,
    InvokeOnValueChanged = function(self : Option, value : any) : ()
        local result = true;
        for i, index in self.activeCallbacks do
            local callbackTable = self.onValueChangedCallbacks[index];

            local callbackResult = callbackTable.callback(self, value);
            result = callbackResult and result;
        end

        return result;
    end,
}
OptionMetatable.__index = OptionMetatable

local LookupTableMetatable = {
    Get = function(self : LookupTable<any>, name : string) : number
        local index = self.nameToIndex[name];
        if (index == nil) then
            -- Create a new entry
            local value = { name = name };
            table.insert(self.list, value);
            index = #self.list;
            self.nameToIndex[name] = index;
        end

        return index;
    end,
    Has = function(self : LookupTable<any>, name : string) : boolean
        return self.nameToIndex[name] ~= nil;
    end,
}
LookupTableMetatable.__index = LookupTableMetatable;

local SectionMetatable = {
    GetOption = function(self : OptionsSection, name : string) : Option
        if (not self.options:Has(name)) then
            error(`Section {self.name} does not have option {name}`, 2);
        end

        -- Return existing object
        local index = self.options:Get(name);
        return self.options.list[index];
    end,
    RegisterOption = function(self : OptionsSection, name : string, defaultValue : any, type : OptionType, settings : table) : Option
        if (self.options:Has(name)) then
            error(`Section {self.name} already has option {name}`, 2);
        end
        
        local index = self.options:Get(name);
        local object = self.options.list[index];
        object.name = name;
        object.type = type;
        object.defaultValue = defaultValue;
        object.value = defaultValue;
        object.onValueChangedCallbacks = {};
        object.activeCallbacks = {};
        object.inactiveCallbacks = {};
        object.settings = settings or {};
        setmetatable(object, OptionMetatable);

        return object;
    end,
}
SectionMetatable.__index = SectionMetatable;
setmetatable(SectionMetatable, LookupTableMetatable);

local GroupMetatable = {
    Get = function(self : OptionsGroup, name : string) : OptionsSection
        if (not self.sections:Has(name)) then
            local index = self.sections:Get(name);
            local object = self.sections.list[index];
            object.options = {
                nameToIndex = {},
                list = {},
            };

            setmetatable(object, SectionMetatable);
            setmetatable(object.options, LookupTableMetatable);
            return object;
        end

        -- Return existing object
        local index = self.sections:Get(name);
        return self.sections.list[index];
    end,
}
GroupMetatable.__index = GroupMetatable;
setmetatable(GroupMetatable, LookupTableMetatable);

local CategoryMetatable = {
    Get = function(self : OptionsCategory, name : string) : OptionsGroup
        if (not self.groups:Has(name)) then
            local index = self.groups:Get(name);
            local object = self.groups.list[index];
            object.sections = {
                nameToIndex = {},
                list = {},
            };

            setmetatable(object, GroupMetatable);
            setmetatable(object.sections, LookupTableMetatable);
            return object;
        end

        -- Return existing object
        local index = self.groups:Get(name);
        return self.groups.list[index];
    end,
}
CategoryMetatable.__index = CategoryMetatable;
setmetatable(CategoryMetatable, LookupTableMetatable);

local ContextMetatable = {
    GetCategory = function(self : OptionsContext, optionCategory : OptionCategoryType) : OptionsCategory
        if (not self:Has(optionCategory)) then
            local index = LookupTableMetatable.Get(self.categories, optionCategory);
            local object = self.categories.list[index];
            object.groups = {
                nameToIndex = {},
                list = {},
            };

            setmetatable(object, CategoryMetatable);
            setmetatable(object.groups, LookupTableMetatable);
            return object;
        end

        -- Return existing object
        local index = LookupTableMetatable.Get(self.categories, optionCategory);
        return self.categories.list[index];
    end,
    GetCategories = function(self : OptionsContext) : { OptionsCategory }
        return self.categories.list;
    end,
    Has = function(self : OptionsContext, name : string) : boolean
        return self.categories.nameToIndex[name] ~= nil;
    end,
}
ContextMetatable.__index = ContextMetatable;

local optionsContext : OptionsContext = {
    categories = {
        nameToIndex = {},
        list = {},
    }
};

-- This might look ugly but it's needed to make the context global and not as a copy
__global_optionsContext = optionsContext;
setmetatable(__global_optionsContext, ContextMetatable);

__global_optionsContext:GetCategory(OptionCategoryType.Gameplay);
__global_optionsContext:GetCategory(OptionCategoryType.Keybindings);
__global_optionsContext:GetCategory(OptionCategoryType.Accessibility);
__global_optionsContext:GetCategory(OptionCategoryType.System);

return __global_optionsContext;

-- Usage:
--local optionCategory = OptionsContext:GetCategory(OptionCategoryType.Gameplay);
--local optionGroup = optionCategory:Get("Nameplates");
--local optionSection = optionGroup:Get("General");
--local enabledOption = optionSection:Get("Enabled"); -- ERROR
--local enabledOption = optionSection.RegisterOption("Enabled", true, OptionType.Checkbox););
--local isEnabled = enabledOption:Get();
