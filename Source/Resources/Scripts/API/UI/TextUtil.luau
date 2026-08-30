export type TextUtilAPI =
{
    ContainsIgnoreCase : (text : string, query : string) -> boolean,
    Truncate : (text : string, maximumCharacters : number) -> string,
    TruncateToWidth : (text : string, textTemplate : string, maximumWidth : number, fontSize : number?) -> string,
}

local textUtilAPI : TextUtilAPI = { };

function textUtilAPI.ContainsIgnoreCase(text : string, query : string) : boolean
    return string.find(string.lower(text), string.lower(query), 1, true) ~= nil;
end

function textUtilAPI.Truncate(text : string, maximumCharacters : number) : string
    local characterCount = utf8.len(text) or #text;
    if characterCount <= maximumCharacters then return text; end

    local suffix = "...";
    local prefixLength = math.max(0, maximumCharacters - #suffix);
    local endByte = utf8.offset(text, prefixLength + 1);
    local prefix = if endByte == nil then text else string.sub(text, 1, endByte - 1);
    return prefix .. suffix;
end

function textUtilAPI.TruncateToWidth(text : string, textTemplate : string, maximumWidth : number, fontSize : number?) : string
    local function Measure(value : string) : vec
        if fontSize == nil then return UI.CalculateTextSize(value, textTemplate); end
        return UI.CalculateTextSize(value, textTemplate, fontSize);
    end

    if Measure(text).x <= maximumWidth then return text; end

    local suffix = "...";
    if Measure(suffix).x > maximumWidth then return ""; end

    local characterCount = utf8.len(text) or #text;
    local low = 0;
    local high = characterCount;
    while low < high do
        local candidateLength = math.ceil((low + high) / 2);
        local endByte = utf8.offset(text, candidateLength + 1);
        local prefix = if endByte == nil then text else string.sub(text, 1, endByte - 1);
        if Measure(prefix .. suffix).x <= maximumWidth then
            low = candidateLength;
        else
            high = candidateLength - 1;
        end
    end

    local endByte = utf8.offset(text, low + 1);
    local prefix = if endByte == nil then text else string.sub(text, 1, endByte - 1);
    return prefix .. suffix;
end

return textUtilAPI;
