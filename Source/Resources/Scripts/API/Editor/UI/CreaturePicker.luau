local ResourcePicker = require("@src/API/Editor/UI/ResourcePicker");

export type Item = ResourcePicker.Item;
export type OpenOptions = ResourcePicker.OpenOptions;
export type CreaturePicker = ResourcePicker.ResourcePicker;

export type Options =
{
    name : string,
    title : string?,
    width : number?,
    height : number?,
    searchPlaceholder : string?,
}

export type CreaturePickerAPI =
{
    New : (parent : (Canvas | Widget), layer : integer, options : Options) -> CreaturePicker,
}

local creaturePickerAPI : CreaturePickerAPI = { };

function creaturePickerAPI.New(parent : (Canvas | Widget), layer : integer, options : Options) : CreaturePicker
    return ResourcePicker.New(parent, layer, {
        name = options.name,
        title = options.title or "Select Creature",
        width = options.width,
        height = options.height,
        searchPlaceholder = options.searchPlaceholder or "Search by name or creature template ID...",
        footerText = "Click a creature to select it.",
    });
end

return creaturePickerAPI;
