export type ThemeAPI =
{
    windowColor : vec,
    titleColor : vec,
    borderColor : vec,
    selectedColor : vec,
    windowAlpha : number,
    borderSize : number,
    textTemplate : string,
}
local themeAPI : ThemeAPI = {
    windowColor = vec.New(0.10, 0.10, 0.12),
    titleColor = vec.New(0.06, 0.06, 0.08),
    borderColor = vec.New(0.30, 0.30, 0.35),
    selectedColor = vec.New(0.30, 0.55, 0.85),
    windowAlpha = 0.96,
    borderSize = 1,
    textTemplate = "DevToolsText",
};

return themeAPI;
