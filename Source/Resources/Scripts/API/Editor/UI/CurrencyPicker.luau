local ResourcePicker = require("@src/API/Editor/UI/ResourcePicker");

export type Item = ResourcePicker.Item;
export type OpenOptions = ResourcePicker.OpenOptions;
export type CurrencyPicker = ResourcePicker.ResourcePicker;

export type Options =
{
    name : string,
    title : string?,
    width : number?,
    height : number?,
    searchPlaceholder : string?,
}

export type CurrencyPickerAPI =
{
    New : (parent : (Canvas | Widget), layer : integer, options : Options) -> CurrencyPicker,
}

local currencyPickerAPI : CurrencyPickerAPI = { };

function currencyPickerAPI.New(parent : (Canvas | Widget), layer : integer, options : Options) : CurrencyPicker
    return ResourcePicker.New(parent, layer, {
        name = options.name,
        title = options.title or "Select Currency",
        width = options.width,
        height = options.height,
        searchPlaceholder = options.searchPlaceholder or "Search by name or currency ID...",
        footerText = "Click a currency to select it.",
    });
end

return currencyPickerAPI;
