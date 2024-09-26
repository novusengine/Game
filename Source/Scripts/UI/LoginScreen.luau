local UICheckbox = require("API/UI/Checkbox")
local UIInputBox = require("API/UI/InputBox")

local function RegisterTemplates()
    UI.RegisterPanelTemplate("LoginBackgroundPanel",
    {
        background = "Data/Texture/interface/glues/models/ui_mainmenu_lowbandwidth/ui_mainmenu_pandaria_low_01.dds",
    });

    UI.RegisterTextTemplate("LoginText",
    {
        font = "Data/Fonts/Ubuntu-Regular.ttf", -- Obviously we want a different font
        size = 28.0,
        color = vec3.new(255.0/255.0, 220.0/255.0, 0.0/255.0),
        borderSize = 0.5,
        borderFade = 0.2,
        borderColor = vec3.new(0.0, 0.0, 0.0),
    });

    UI.RegisterTextTemplate("RememberText",
    {
        font = "Data/Fonts/Ubuntu-Regular.ttf", -- Obviously we want a different font
        size = 18.0,
        color = vec3.new(255.0/255.0, 220.0/255.0, 0.0/255.0),
        borderSize = 0.5,
        borderFade = 0.2,
        borderColor = vec3.new(0.0, 0.0, 0.0),
    });

    UI.RegisterTextTemplate("VersionText",
    {
        font = "Data/Fonts/Ubuntu-Regular.ttf", -- Obviously we want a different font
        size = 14.0,
        color = vec3.new(255.0/255.0, 220.0/255.0, 0.0/255.0),
        borderSize = 0.5,
        borderFade = 0.2,
        borderColor = vec3.new(0.0, 0.0, 0.0),
    });

    -- Input Box
    UI.RegisterButtonTemplate("LoginInputBoxButton", 
    {
        panelTemplate = "DialogBox",
        textTemplate = "DefaultButtonText",
    });

    UI.RegisterButtonTemplate("LoginButton",
    {
        panelTemplate = "DefaultButtonPanel",
        textTemplate = "LoginText",
    });
end

local function Login(accountInputBox, passwordInputBox)
    print(`Logged in: {accountInputBox.text} {passwordInputBox.text}`)

    passwordInputBox:SetText("");
end

local function CreateLoginScreen()
    -- Create Canvas
    local canvas = UI.GetCanvas("LoginScreen", 0, 0, 1920, 1080); -- TODO: We don't want to hardcode the reference resolution here
    
    -- Create Panel
    local panel = canvas:NewPanel(0, 0, 1920, 1080, 0, "LoginBackgroundPanel");
    panel:SetEnabled(false);
    panel:SetAnchor(0.5, 0.5);
    panel:SetRelativePoint(0.5, 0.5);

    -- Account Name Input
    local accountNameInputBox = UIInputBox.NewInputBox(panel, 0, -40, 200, 40, 0, {
        buttonTemplate = "LoginInputBoxButton",
    });
    accountNameInputBox.inputBox:SetAnchor(0.5, 0.5);
    accountNameInputBox.button:SetRelativePoint(0.5, 0.5);

    -- Account Name Text
    local accountNameText = accountNameInputBox.button:NewText("Account Name", 0, 5, 0, "LoginText");
    accountNameText:SetAnchor(0.5, 1.0);
    accountNameText:SetRelativePoint(0.5, 0.0);

    -- Remember Checkbox
    local rememberCheckbox = UICheckbox.NewCheckbox(accountNameInputBox.button, 0, -5, 24, 24, 0,
    {
        backgroundTemplate = "DefaultCheckboxBackground",
        fillTemplate = "DefaultCheckboxFill",
    });
    rememberCheckbox.checkbox:SetAnchor(0.0, 0.0);
    rememberCheckbox.background:SetRelativePoint(0.0, 1.0);
    rememberCheckbox:Click();

    -- Remember Text
    local rememberText = rememberCheckbox.background:NewText("Remember Account", 10, 0, 0, "RememberText");
    rememberText:SetAnchor(1.0, 0.5);
    rememberText:SetRelativePoint(0.0, 0.5);

    -- Password Input
    local passwordInputBox = UIInputBox.NewInputBox(panel, 0, -170, 200, 40, 0, {
        buttonTemplate = "LoginInputBoxButton",
        secure = true,
    });
    passwordInputBox.inputBox:SetAnchor(0.5, 0.5);
    passwordInputBox.button:SetRelativePoint(0.5, 0.5);

    -- Password Text
    local passwordText = passwordInputBox.button:NewText("Password", 0, 5, 0, "LoginText");
    passwordText:SetAnchor(0.5, 1.0);
    passwordText:SetRelativePoint(0.5, 0.0);

    -- Login Button
    local loginButton = panel:NewButton(0, -260, 250, 40, 0, "LoginButton");
    loginButton:SetAnchor(0.5, 0.5);
    loginButton:SetRelativePoint(0.5, 0.5);
    loginButton:SetText("Login");

    -- Discord Button
    local discordButton = panel:NewButton(20, -260, 250, 40, 0, "LoginButton");
    discordButton:SetAnchor(0.0, 0.5);
    discordButton:SetRelativePoint(0.0, 0.5);
    discordButton:SetText("Discord");

    -- Github Button
    local githubButton = panel:NewButton(20, -310, 250, 40, 0, "LoginButton");
    githubButton:SetAnchor(0.0, 0.5);
    githubButton:SetRelativePoint(0.0, 0.5);
    githubButton:SetText("Github");

    -- Version Text
    local versionText = panel:NewText("Version 0.0.1(1) (Debug)", 20, 20, 0, "VersionText");
    versionText:SetAnchor(0.0, 0.0);
    versionText:SetRelativePoint(0.0, 0.0);

    -- System Button
    local systemButton = panel:NewButton(-20, -260, 250, 40, 0, "LoginButton");
    systemButton:SetAnchor(1.0, 0.5);
    systemButton:SetRelativePoint(1.0, 0.5);
    systemButton:SetText("System");

    -- Credits Button
    local creditsButton = panel:NewButton(-20, -310, 250, 40, 0, "LoginButton");
    creditsButton:SetAnchor(1.0, 0.5);
    creditsButton:SetRelativePoint(1.0, 0.5);
    creditsButton:SetText("Credits");

    -- License Button
    local licenseButton = panel:NewButton(-20, -360, 250, 40, 0, "LoginButton");
    licenseButton:SetAnchor(1.0, 0.5);
    licenseButton:SetRelativePoint(1.0, 0.5);
    licenseButton:SetText("License");

    -- Quit Button
    local quitButton = panel:NewButton(-20, 20, 250, 40, 0, "LoginButton");
    quitButton:SetAnchor(1.0, 0.0);
    quitButton:SetRelativePoint(1.0, 0.0);
    quitButton:SetText("Quit");

    -- Login handlers
    accountNameInputBox:SetOnSubmit(function(table)
        Login(accountNameInputBox, passwordInputBox);
        return true; -- Returning true will unfocus the widget
    end);

    passwordInputBox:SetOnSubmit(function(table)
        Login(accountNameInputBox, passwordInputBox);
        return true; -- Returning true will unfocus the widget
    end);

    loginButton:SetOnMouseUp(function(eventID, widget, ...)
        Login(accountNameInputBox, passwordInputBox);
    end)

    -- Tab Handlers (TODO: Make a system to set this up easier)
    accountNameInputBox:SetOnTab(function(table)
        UI.FocusWidget(passwordInputBox.button);
    end);

    passwordInputBox:SetOnTab(function(table)
        UI.FocusWidget(accountNameInputBox.button);
    end);
end

local function OnGameLoaded(eventID : number, motd : string)
    RegisterTemplates();

    CreateLoginScreen();
end

RegisterGameEvent(GameEvent.Loaded, OnGameLoaded)