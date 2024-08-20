local Module = { }

function Module.RegisterTemplates()
    local menuPanelTemplate = {
        background = "Data/Texture/interface/dialogframe/ui-dialogbox-background.dds",
        color = vec3.new(1.0, 1.0, 1.0), -- TODO: Color class
        cornerRadius = 0.0
    };
    UI.RegisterPanelTemplate("MenuPanel", menuPanelTemplate);

    -- Header
    local menuPanelHeaderTemplate = {
        background = "Data/Texture/interface/dialogframe/ui-dialogbox-header.dds",
        color = vec3.new(1.0, 1.0, 1.0), -- TODO: Color class
        cornerRadius = 0.0
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

    local minX,minY = UI.PixelsToTexCoord(0, 0, 128, 32);
    local maxX,maxY = UI.PixelsToTexCoord(128, 32, 128, 32);

    -- Button
    local menuButtonPanelTemplate = {
        background = "Data/Texture/interface/buttons/ui-panel-button-up.dds",
        color = vec3.new(1.0, 1.0, 1.0), -- TODO: Color class
        cornerRadius = 0.0,
        --texCoords = Box.new(1.1, 2.1, 3.1, 4.1),
        texCoords = Box.new(minX, minY, maxX, maxY),

    };
    UI.RegisterPanelTemplate("MenuButtonPanel", menuButtonPanelTemplate);

    local menuButtonTextTemplate = {
        font = "Data/Fonts/Ubuntu-Regular.ttf", -- Obviously we want a different font
        size = 24.0,
        color = vec3.new(1.0, 1.0, 1.0),
        borderSize = 0.35,
        borderFade = 0.8,
        borderColor = vec3.new(0.0, 0.0, 0.0),
    };
    UI.RegisterTextTemplate("MenuButtonText", menuButtonTextTemplate);

    local menuButtonTemplate = {
        --buttonPanelTemplate = "MenuButtonPanel",
        --pressedButtonPanelTemplate = "MenuButtonPanel",
        --disabledButtonPanelTemplate = "MenuButtonPanel",
        --hoveredButtonPanelTemplate = "MenuButtonPanel",
        panelTemplate = "MenuButtonPanel",
        textTemplate = "MenuButtonText",
    };
    UI.RegisterButtonTemplate("MenuButton", menuButtonTemplate);

    --local inheritedMenuButtonTemplate = UI.CopyButtonTemplate("MenuButton")
    --menuButtonTemplate.buttonPanelTemplate = "OtherButtonPanel"
    --UI.RegisterButtonTemplate("InheritedMenuButton", inheritedMenuButtonTemplate);
end

return Module