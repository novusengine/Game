local ResourcePicker = require("@src/API/Editor/UI/ResourcePicker");

export type Item = ResourcePicker.Item;
export type OpenOptions = ResourcePicker.OpenOptions;
export type FactionPicker = ResourcePicker.ResourcePicker;

export type Options =
{
    name : string,
    title : string?,
    width : number?,
    height : number?,
    searchPlaceholder : string?,
}

export type FactionPickerAPI =
{
    New : (parent : (Canvas | Widget), layer : integer, options : Options) -> FactionPicker,
}

local factionPickerAPI : FactionPickerAPI = { };

function factionPickerAPI.New(parent : (Canvas | Widget), layer : integer, options : Options) : FactionPicker
    return ResourcePicker.New(parent, layer, {
        name = options.name,
        title = options.title or "Select Faction",
        width = options.width,
        height = options.height,
        searchPlaceholder = options.searchPlaceholder or "Search by name or faction ID...",
        footerText = "Click a faction to select it.",
    });
end

return factionPickerAPI;
