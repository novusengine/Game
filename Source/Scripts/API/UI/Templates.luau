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
    local buttonMaxX, buttonMaxY = UI.PixelsToTexCoord(78, 22, 128, 32);

    local buttonNineSliceMinX, buttonNineSliceMinY = UI.PixelsToTexCoord(5, 5, 128, 32);
    local buttonNineSliceMaxX, buttonNineSliceMaxY = UI.PixelsToTexCoord(74, 18, 128, 32);

    -- Button
    UI.RegisterPanelTemplate("DefaultButtonPanel", 
    {
        background = "Data/Texture/interface/buttons/ui-panel-button-up.dds",
        color = vec3.new(1.0, 1.0, 1.0), -- TODO: Color class
        cornerRadius = 0.0,
        texCoords = Box.new(buttonMinX, buttonMinY, buttonMaxX, buttonMaxY),
        nineSliceCoords = Box.new(buttonNineSliceMinX, buttonNineSliceMinY, buttonNineSliceMaxX, buttonNineSliceMaxY),

        -- These get applied additively on this template
        onClickTemplate = "DefaultButtonDownPanel",
        onHoverTemplate = "DefaultButtonHoveredPanel",
        onUninteractableTemplate = "DefaultButtonDisabledPanel",
    });

    UI.RegisterPanelTemplate("DefaultButtonDownPanel", 
    {
        background = "Data/Texture/interface/buttons/ui-panel-button-down.dds",
    });

    UI.RegisterPanelTemplate("DefaultButtonHoveredPanel", 
    {
        foreground = "Data/Texture/interface/buttons/ui-panel-button-highlight.dds",
    });

    UI.RegisterPanelTemplate("DefaultButtonDisabledPanel", 
    {
        background = "Data/Texture/interface/buttons/ui-panel-button-disabled.dds",
    });

    UI.RegisterTextTemplate("DefaultButtonText", 
    {
        font = "Data/Fonts/Ubuntu-Regular.ttf", -- Obviously we want a different font
        size = 24.0,
        color = vec3.new(1.0, 1.0, 1.0),
        borderSize = 0.3,
        borderFade = 0.8,
        borderColor = vec3.new(0.0, 0.0, 0.0),
    });

    UI.RegisterButtonTemplate("DefaultButton", 
    {
        panelTemplate = "DefaultButtonPanel",
        textTemplate = "DefaultButtonText",
    });

    -- Slider
    local sliderMinX, sliderMinY = UI.PixelsToTexCoord(11, 11, 64, 64);
    local sliderMaxX, sliderMaxY = UI.PixelsToTexCoord(51, 51, 64, 64);

    local sliderNineSliceMinX, sliderNineSliceMinY = UI.PixelsToTexCoord(16, 16, 64, 64);
    local sliderNineSliceMaxX, sliderNineSliceMaxY = UI.PixelsToTexCoord(46, 46, 64, 64);

    UI.RegisterPanelTemplate("DefaultSliderBackground",
    {
        background = "Data/Texture/interface/buttons/ui-quickslot.dds",
        texCoords = Box.new(sliderMinX, sliderMinY, sliderMaxX, sliderMaxY),
        nineSliceCoords = Box.new(sliderNineSliceMinX, sliderNineSliceMinY, sliderNineSliceMaxX, sliderNineSliceMaxY),
    });

    UI.RegisterPanelTemplate("DefaultSliderFill",
    {
        background = "Data/Texture/interface/buttons/bluegrad64_faded.dds",
        color = vec3.new(1.0, 1.0, 1.0),
        cornerRadius = 2.0,
    });


    -- Menu Panel

    --local inheritedMenuButtonTemplate = UI.CopyButtonTemplate("MenuButton")
    --menuButtonTemplate.buttonPanelTemplate = "OtherButtonPanel"
    --UI.RegisterButtonTemplate("InheritedMenuButton", inheritedMenuButtonTemplate);
end

return Module