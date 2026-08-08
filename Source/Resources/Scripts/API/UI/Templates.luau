export type TemplatesAPI = { RegisterTemplates : () -> () }
local templatesAPI : TemplatesAPI = { }

--[[local function UIInputEventHandler(eventID, widget, ...)
    if (eventID == UIInputEvent.MouseDown) then
        print("Event: MouseDown");
    end
    if (eventID == UIInputEvent.MouseUp) then
        --print("Event: MouseUp");
    end

    if (eventID == UIInputEvent.HoverBegin) then
        --print("Event: HoverBegin");
    end
    if (eventID == UIInputEvent.HoverEnd) then
        --print("Event: HoverEnd");
    end

    if (eventID == UIInputEvent.MouseDown or eventID == UIInputEvent.MouseUp or eventID == UIInputEvent.MouseHeld) then
        --local mouseButton = ...
    end
  end]]

function templatesAPI.RegisterTemplates()
    UI.RegisterTextTemplate("DevToolsText",
    {
        font = "Data/Fonts/OpenSans-Regular.ttf",
        size = 8.0,
        borderSize = 1.0,
        borderColor = vec.New(0.0, 0.0, 0.0),
    });

    -- Debug panels
    UI.RegisterPanelTemplate("DebugRed",
    {
        cornerRadius = 0.0,
        color = vec.New(1.0, 0.0, 0.0),
    });

    UI.RegisterPanelTemplate("DebugGreen",
    {
        cornerRadius = 0.0,
        color = vec.New(0.0, 1.0, 0.0),
    });

    UI.RegisterPanelTemplate("DebugBlue",
    {
        cornerRadius = 0.0,
        color = vec.New(0.0, 0.0, 1.0),
    });

    UI.RegisterPanelTemplate("DebugDarkGrey",
    {
        cornerRadius = 0.0,
        color = vec.New(0.15, 0.15, 0.15),
    });

    -- Dialog Box
    UI.RegisterPanelTemplate("DialogBox",
    {
        color = vec.New(0.055, 0.065, 0.078),
        alpha = 0.98,
        cornerRadius = 3.0,
        borderColor = vec.New(0.20, 0.23, 0.28),
        borderSize = 1.0,
    });

    UI.RegisterPanelTemplate("TexturePanel",
    {
        color = vec.New(1.0, 1.0, 1.0),
        alpha = 1.0,
        cornerRadius = 0.0,
    });

    -- Dialog Box Header Text
    UI.RegisterTextTemplate("YellowText",
    {
        font = "Data/Fonts/OpenSans-Regular.ttf", -- Obviously we want a different font
        size = 24.0,
        color = vec.New(255.0/255.0, 239.0/255.0, 0.0/255.0),
        borderSize = 0.3,
        borderColor = vec.New(0.0, 0.0, 0.0),
    });

    -- Button
    UI.RegisterPanelTemplate("DefaultButtonPanel",
    {
        color = vec.New(0.095, 0.105, 0.12),
        alpha = 1.0,
        cornerRadius = 2.0,
        borderColor = vec.New(0.22, 0.25, 0.30),
        borderSize = 1.0,

        -- These get applied additively on this template
        onClickTemplate = "DefaultButtonDownPanel",
        onHoverTemplate = "DefaultButtonHoveredPanel",
        onUninteractableTemplate = "DefaultButtonDisabledPanel",
    });

    UI.RegisterPanelTemplate("DefaultButtonDownPanel",
    {
        color = vec.New(0.09, 0.15, 0.23),
        alpha = 1.0,
    });

    UI.RegisterPanelTemplate("DefaultButtonHoveredPanel",
    {
        color = vec.New(0.13, 0.20, 0.29),
        alpha = 1.0,
    });

    UI.RegisterPanelTemplate("DefaultButtonDisabledPanel",
    {
        color = vec.New(0.30, 0.30, 0.35),
        alpha = 0.08,
    });

    UI.RegisterTextTemplate("DefaultDebugText",
    {
        font = "Data/Fonts/OpenSans-Regular.ttf", -- Obviously we want a different font
        size = 12.0,
        borderSize = 1.0,
        borderColor = vec.New(0.0, 0.0, 0.0),
    });

    UI.RegisterTextTemplate("DefaultButtonText",
    {
        font = "Data/Fonts/OpenSans-Regular.ttf", -- Obviously we want a different font
        size = 16.0,
        borderSize = 1.0,
        borderColor = vec.New(0.0, 0.0, 0.0),
    });

    UI.RegisterTextTemplate("SmallYellowButtonText",
    {
        font = "Data/Fonts/OpenSans-Regular.ttf",
        size = 18.0,
        color = vec.New(255.0/255.0, 199.0/255.0, 0.0/255.0),
        borderSize = 0.5,
        borderColor = vec.New(0.2, 0.2, 0.2),
    });
    -- Slider
    UI.RegisterPanelTemplate("DefaultSliderBackground",
    {
        color = vec.New(0.055, 0.065, 0.078),
        alpha = 1.0,
        cornerRadius = 2.0,
        borderColor = vec.New(0.20, 0.23, 0.28),
        borderSize = 1.0,
    });

    UI.RegisterPanelTemplate("DefaultSliderFill",
    {
        color = vec.New(0.20, 0.42, 0.68),
        alpha = 1.0,
        cornerRadius = 2.0,
    });

    -- Checkbox
    UI.RegisterPanelTemplate("DefaultCheckboxBackground",
    {
        background = "texture/ui/icons/32x32/blue/check_square_grey.png",
        color = vec.New(0.095, 0.105, 0.12),

        onHoverTemplate = "DefaultCheckboxHoveredPanel",
    });

    UI.RegisterPanelTemplate("DefaultCheckboxHoveredPanel",
    {
        color = vec.New(0.13, 0.20, 0.29),
    });

    UI.RegisterPanelTemplate("DefaultCheckboxFill",
    {
        background = "texture/ui/icons/50x50/white/checkmark.png",
        color = vec.New(0.38, 0.56, 0.78),
        alpha = 0.9,

        onUninteractableTemplate = "DefaultCheckboxDisabledFill",
    });

    UI.RegisterPanelTemplate("DefaultCheckboxDisabledFill",
    {
        background = "texture/ui/icons/50x50/white/checkmark.png",
        color = vec.New(0.38, 0.56, 0.78),
        alpha = 0.4,
    });

    -- Menu Panel

    --local inheritedMenuButtonTemplate = UI.CopyButtonTemplate("MenuButton")
    --menuButtonTemplate.buttonPanelTemplate = "OtherButtonPanel"
    --UI.RegisterButtonTemplate("InheritedMenuButton", inheritedMenuButtonTemplate);
end

return templatesAPI
