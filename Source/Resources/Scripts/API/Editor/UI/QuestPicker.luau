local ResourcePicker = require("@src/API/Editor/UI/ResourcePicker");

export type Item = ResourcePicker.Item;
export type OpenOptions = ResourcePicker.OpenOptions;
export type QuestPicker = ResourcePicker.ResourcePicker;

export type Options =
{
    name : string,
    title : string?,
    width : number?,
    height : number?,
    searchPlaceholder : string?,
}

export type QuestPickerAPI =
{
    New : (parent : (Canvas | Widget), layer : integer, options : Options) -> QuestPicker,
}

local questPickerAPI : QuestPickerAPI = { };

function questPickerAPI.New(parent : (Canvas | Widget), layer : integer, options : Options) : QuestPicker
    return ResourcePicker.New(parent, layer, {
        name = options.name,
        title = options.title or "Select Quest",
        width = options.width,
        height = options.height,
        searchPlaceholder = options.searchPlaceholder or "Search by name or quest ID...",
        footerText = "Click a quest to select it.",
    });
end

return questPickerAPI;
