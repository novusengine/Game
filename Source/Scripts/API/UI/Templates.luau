local Module = { }

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

function Module.RegisterTemplates()
    -- Dialog Box
    UI.RegisterPanelTemplate("DialogBox",
    {
        background = "Data/Texture/interface/dialogframe/ui-dialogbox-background.dds",
        color = vec3.new(1.0, 1.0, 1.0), -- TODO: Color class
        cornerRadius = 0.0,
    });

    -- Dialog Box Header
    local headerMinX, headerMinY = UI.PixelsToTexCoord(60, 2, 256, 64);
    local headerMaxX, headerMaxY = UI.PixelsToTexCoord(193, 37, 256, 64);

    UI.RegisterPanelTemplate("DialogBoxHeader",
    {
        background = "Data/Texture/interface/dialogframe/ui-dialogbox-header.dds",
        color = vec3.new(1.0, 1.0, 1.0), -- TODO: Color class
        cornerRadius = 0.0,
        texCoords = Box.new(headerMinX, headerMinY, headerMaxX, headerMaxY),
    });
    
    -- Dialog Box Header Text
    UI.RegisterTextTemplate("YellowText",
    {
        font = "Data/Fonts/Ubuntu-Regular.ttf", -- Obviously we want a different font
        size = 24.0,
        color = vec3.new(255.0/255.0, 239.0/255.0, 0.0/255.0),
        borderSize = 0.30,
        borderFade = 0.5,
        borderColor = vec3.new(0.0, 0.0, 0.0),
    });

    local buttonMinX, buttonMinY = UI.PixelsToTexCoord(1, 1, 128, 32);
    local buttonMaxX, buttonMaxY = UI.PixelsToTexCoord(78, 21, 128, 32);

    -- Button
    UI.RegisterPanelTemplate("GenericButtonPanel", 
    {
        background = "Data/Texture/interface/buttons/ui-panel-button-up.dds",
        color = vec3.new(1.0, 1.0, 1.0), -- TODO: Color class
        cornerRadius = 0.0,
        texCoords = Box.new(buttonMinX, buttonMinY, buttonMaxX, buttonMaxY),

        -- These get applied additively on this template
        onClickTemplate = "GenericButtonDownPanel",
        onHoverTemplate = "GenericButtonHoveredPanel",
        onUninteractableTemplate = "GenericButtonDisabledPanel",
    });

    UI.RegisterPanelTemplate("GenericButtonDownPanel", 
    {
        background = "Data/Texture/interface/buttons/ui-panel-button-down.dds",
    });

    UI.RegisterPanelTemplate("GenericButtonHoveredPanel", 
    {
        foreground = "Data/Texture/interface/buttons/ui-panel-button-highlight.dds",
    });

    UI.RegisterPanelTemplate("GenericButtonDisabledPanel", 
    {
        background = "Data/Texture/interface/buttons/ui-panel-button-disabled.dds",
    });

    UI.RegisterTextTemplate("GenericButtonText", 
    {
        font = "Data/Fonts/Ubuntu-Regular.ttf", -- Obviously we want a different font
        size = 24.0,
        color = vec3.new(1.0, 1.0, 1.0),
        borderSize = 0.3,
        borderFade = 0.8,
        borderColor = vec3.new(0.0, 0.0, 0.0),
    });

    UI.RegisterButtonTemplate("GenericButton", 
    {
        panelTemplate = "GenericButtonPanel",
        textTemplate = "GenericButtonText",
    });

    -- Menu Panel

    --local inheritedMenuButtonTemplate = UI.CopyButtonTemplate("MenuButton")
    --menuButtonTemplate.buttonPanelTemplate = "OtherButtonPanel"
    --UI.RegisterButtonTemplate("InheritedMenuButton", inheritedMenuButtonTemplate);
end

return Module