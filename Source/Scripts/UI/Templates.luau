local Module = { }

local function UIInputEventHandler(eventID, widget, ...)
    if (eventID == UIInputEvent.MouseDown) then
        print("Event: MouseDown");
    end
    if (eventID == UIInputEvent.MouseUp) then
        print("Event: MouseUp");
    end

    if (eventID == UIInputEvent.HoverBegin) then
        print("Event: HoverBegin");
    end
    if (eventID == UIInputEvent.HoverEnd) then
        print("Event: HoverEnd");
    end

    if (eventID == UIInputEvent.MouseDown or eventID == UIInputEvent.MouseUp or eventID == UIInputEvent.MouseHeld) then
        --local mouseButton = ...
    end
  end

function Module.RegisterTemplates()
    local menuPanelTemplate = {
        background = "Data/Texture/interface/dialogframe/ui-dialogbox-background.dds",
        color = vec3.new(1.0, 1.0, 1.0), -- TODO: Color class
        cornerRadius = 0.0,
    };
    UI.RegisterPanelTemplate("MenuPanel", menuPanelTemplate);

    -- Header
    local headerMinX, headerMinY = UI.PixelsToTexCoord(60, 2, 256, 64);
    local headerMaxX, headerMaxY = UI.PixelsToTexCoord(193, 37, 256, 64);

    local menuPanelHeaderTemplate = {
        background = "Data/Texture/interface/dialogframe/ui-dialogbox-header.dds",
        color = vec3.new(1.0, 1.0, 1.0), -- TODO: Color class
        cornerRadius = 0.0,
        texCoords = Box.new(headerMinX, headerMinY, headerMaxX, headerMaxY),
    };
    UI.RegisterPanelTemplate("MenuPanelHeader", menuPanelHeaderTemplate);
    
    local menuPanelHeaderTextTemplate = {
        font = "Data/Fonts/Ubuntu-Regular.ttf", -- Obviously we want a different font
        size = 24.0,
        color = vec3.new(255.0/255.0, 239.0/255.0, 0.0/255.0),
        borderSize = 0.30,
        borderFade = 0.5,
        borderColor = vec3.new(0.0, 0.0, 0.0),
    };
    UI.RegisterTextTemplate("MenuPanelHeaderText", menuPanelHeaderTextTemplate);

    local buttonMinX, buttonMinY = UI.PixelsToTexCoord(1, 1, 128, 32);
    local buttonMaxX, buttonMaxY = UI.PixelsToTexCoord(78, 21, 128, 32);

    -- Button
    local menuButtonPanelTemplate = {
        background = "Data/Texture/interface/buttons/ui-panel-button-up.dds",
        color = vec3.new(1.0, 1.0, 1.0), -- TODO: Color class
        cornerRadius = 0.0,
        texCoords = Box.new(buttonMinX, buttonMinY, buttonMaxX, buttonMaxY),

        -- These get applied additively on this template
        onClickTemplate = "MenuButtonDownPanel",
        onHoverTemplate = "MenuButtonHoveredPanel",
        onUninteractableTemplate = "MenuButtonDisabledPanel",

        onMouseDown = UIInputEventHandler,
        onMouseUp = UIInputEventHandler,
        --onMouseHeld = UIInputEventHandler,

        onHoverBegin = UIInputEventHandler,
        onHoverEnd = UIInputEventHandler,
        --onHover = UIInputEventHandler,
    };
    UI.RegisterPanelTemplate("MenuButtonPanel", menuButtonPanelTemplate);

    local menuButtonDownPanelTemplate = {
        background = "Data/Texture/interface/buttons/ui-panel-button-down.dds",
    };
    UI.RegisterPanelTemplate("MenuButtonDownPanel", menuButtonDownPanelTemplate);

    local menuButtonDisabledPanelTemplate = {
        background = "Data/Texture/interface/buttons/ui-panel-button-disabled.dds",
    };
    UI.RegisterPanelTemplate("MenuButtonDisabledPanel", menuButtonDisabledPanelTemplate);

    local menuButtonHoveredPanelTemplate = {
        foreground = "Data/Texture/interface/buttons/ui-panel-button-highlight.dds",
    };
    UI.RegisterPanelTemplate("MenuButtonHoveredPanel", menuButtonHoveredPanelTemplate);

    local menuButtonTextTemplate = {
        font = "Data/Fonts/Ubuntu-Regular.ttf", -- Obviously we want a different font
        size = 24.0,
        color = vec3.new(1.0, 1.0, 1.0),
        borderSize = 0.3,
        borderFade = 0.8,
        borderColor = vec3.new(0.0, 0.0, 0.0),
    };
    UI.RegisterTextTemplate("MenuButtonText", menuButtonTextTemplate);

    --[[
    OnMouseDown -- Single event when down
    OnMouseUp -- Single event when released
    OnMouseHeld -- Event every frame it's down?
    OnHoverBegin -- Single event when hover starts (Start? Begin? Enter?)
    OnHoverEnd -- Single event when hover starts
    OnHover -- Event every frame it's hovered?
    ]]

    --[[
    1. First check the exact object
    2. Then check the aggregate object parent (For example if you hover a button, first check the text, then check the button)
    3. Then check siblings of the button, IF there is overlap
    4. Then check parent of the button
    5. Repeat 3-5
    ]]

    local menuButtonTemplate = {
        panelTemplate = "MenuButtonPanel",
        textTemplate = "MenuButtonText",
    };
    UI.RegisterButtonTemplate("MenuButton", menuButtonTemplate);

    --local inheritedMenuButtonTemplate = UI.CopyButtonTemplate("MenuButton")
    --menuButtonTemplate.buttonPanelTemplate = "OtherButtonPanel"
    --UI.RegisterButtonTemplate("InheritedMenuButton", inheritedMenuButtonTemplate);
end

return Module