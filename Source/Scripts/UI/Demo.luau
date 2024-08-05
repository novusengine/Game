local function OnButtonEvent(button : Button, eventID : ButtonEvent, ...)
    if (eventID == ButtonEvent.Hovered) then

    end
end

local function OnGameLoaded(eventID : number, motd : string)
    -- Register a default panel template
    local panelTemplate = {
        background = "interface/framegeneral/ui-background-rock.dds",
        color = vec3.new(0.3, 1.0, 1.0), -- TODO: Color class
        cornerRadius = 3.0
    };
    UI.RegisterPanelTemplate("DefaultPanel", panelTemplate);

    -- Register a default text template
    local textTemplate = {
        font = "Fonts/Calibri.ttf", -- Obviously we want a different font
        size = 20.0,
        color = vec3.new(1.0, 1.0, 1.0),
        border = 1,
        borderColor = vec3.new(0.0, 0.0, 0.0),
    };
    UI.RegisterTextTemplate("DefaultText", textTemplate);

    -- Register a default button template
    local buttonTemplate = {
        background = "interface/framegeneral/ui-background-rock.dds",
        color = vec3.new(1.0, 0.5, 1.0), -- TODO: Color class
        cornerRadius = 5.0
    };
    UI.RegisterButtonTemplate("DefaultButton", buttonTemplate);

    -- Create a canvas
    local canvas = UI.GetCanvas("EscapeMenu", 100, 100);
    -- Add default frame canvas or whatever
    -- Add support for childing canvases to other canvases
    -- Add support for worldspace canvases
    -- Add support for CachedCanvas (needs to explicitly be :Updated or whatever)

    -- Create a panel
    local panel = canvas:Panel(0, 0, 100, 100, 0, "DefaultPanel");

    -- Create a text on the panel
    --local text = panel:Text("Escape Menu", 0, 0, 0, "DefaultText");
    --text:SetAnchor(0.5, 0.0); -- Center the texts placement on the Panel on the X axis
    --text:SetRelativePoint(0.5, 0.0) -- Center the buttons reference point on the X axis

    -- Create a button on the panel
    --local button = panel:Button(0, 50, 30, 50, 0, "DefaultButton");
    --button:Text("Test", Vector2(0, 0), 0, "DefaultText") -- Give the button a text
    --button:SetAnchor(0.5, 0.0) -- Center the buttons placement on the Panel on the X axis
    --button:SetRelativePoint(0.5, 0.0) -- Center the buttons reference point on the X axis

    --button:RegisterEvent(ButtonEvent.Hover);
    --button:RegisterEvent(ButtonEvent.Click);

    print('Hello from Demo.luau');
end

local function OnGameUpdated(eventID : number, motd : string)
    
end

RegisterGameEvent(GameEvent.Loaded, OnGameLoaded)
RegisterGameEvent(GameEvent.Updated, OnGameUpdated)