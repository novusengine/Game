local UIButton = require("@src/API/UI/Button")
local UICheckbox = require("@src/API/UI/Checkbox")
local UIInputBox = require("@src/API/UI/InputBox")
local UIStack = require("@src/API/UI/UIStack")

local function RegisterTemplates()
    UI.RegisterPanelTemplate("LoginBackgroundPanel",
    {
        background = "Data/Texture/interface/glues/models/ui_mainmenu_lowbandwidth/ui_mainmenu_pandaria_low_01.dds",
    });

    UI.RegisterTextTemplate("LoginText",
    {
        font = "Data/Fonts/OpenSans-Regular.ttf",
        size = 24.0,
        color = vector.create(255.0/255.0, 199.0/255.0, 0.0/255.0),
        borderSize = 1.5,
        borderColor = vector.create(0.2, 0.2, 0.2),
    });

    UI.RegisterTextTemplate("LoginButtonText",
    {
        font = "Data/Fonts/OpenSans-Regular.ttf",
        size = 24.0,
        color = vector.create(255.0/255.0, 199.0/255.0, 0.0/255.0),
        borderSize = 1.0,
        borderColor = vector.create(0.0, 0.0, 0.0),
    });

    UI.RegisterTextTemplate("VersionText",
    {
        font = "Data/Fonts/OpenSans-Regular.ttf",
        size = 18.0,
        color = vector.create(255.0/255.0, 199.0/255.0, 0.0/255.0),
        borderSize = 1.5,
        borderColor = vector.create(0.0, 0.0, 0.0),
    });
end

local function TryLogin(accountInputBox, passwordInputBox, rememberMe)
    Login(accountInputBox.text, passwordInputBox.text, rememberMe);
    passwordInputBox:SetText("");
end

local function CreateLoginScreen(stack)
    -- Create Canvas
    local canvas = UI.GetCanvas("LoginScreen", 0, 0, 1920, 1080); -- TODO: We don't want to hardcode the reference resolution here
    
    -- Create Panel
    local panel = canvas:NewPanel(0, 0, 1920, 1080, 0, "LoginBackgroundPanel");
    panel:SetEnabled(false);
    panel:SetAnchor(0.5, 0.5);
    panel:SetRelativePoint(0.5, 0.5);

    -- Account Name Input
    local accountName = GetAccountName();
    local accountNameInputBox = UIInputBox.NewInputBox(panel, 0, -40, 200, 40, 0, {
        panelTemplate = "DialogBox",
        textTemplate = "DefaultButtonText",
        defaultText = accountName
    });
    accountNameInputBox.inputBox:SetAnchor(0.5, 0.5);
    accountNameInputBox.button:SetRelativePoint(0.5, 0.5);
    accountNameInputBox.button.text:SetPos(5, 10);

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
    local rememberText = rememberCheckbox.background:NewText("Remember Account", 10, 0, 0, "SmallYellowButtonText");
    rememberText:SetAnchor(1.0, 0.5);
    rememberText:SetRelativePoint(0.0, 0.5);

    -- Password Input
    local passwordInputBox = UIInputBox.NewInputBox(panel, 0, -170, 200, 40, 0, {
        panelTemplate = "DialogBox",
        textTemplate = "DefaultButtonText",
        secure = true,
    });
    passwordInputBox.inputBox:SetAnchor(0.5, 0.5);
    passwordInputBox.button:SetRelativePoint(0.5, 0.5);
    passwordInputBox.button.text:SetPos(5, 7);

    -- Password Text
    local passwordText = passwordInputBox.button:NewText("Password", 0, 5, 0, "LoginText");
    passwordText:SetAnchor(0.5, 1.0);
    passwordText:SetRelativePoint(0.5, 0.0);

    -- Login Button
    local loginButton = UIButton.NewButton(panel, 0, -260, 250, 40, 0, {
        panelTemplate = "DefaultButtonPanel",
        textTemplate = "LoginButtonText",
        text = "Login",
    });
    loginButton:SetAnchor(0.5, 0.5);
    loginButton:SetRelativePoint(0.5, 0.5);

    -- Discord Button
    local discordButton = UIButton.NewButton(panel, 20, -260, 250, 40, 0, {
        panelTemplate = "DefaultButtonPanel",
        textTemplate = "LoginButtonText",
        text = "Discord",
    });
    discordButton:SetAnchor(0.0, 0.5);
    discordButton:SetRelativePoint(0.0, 0.5);

    -- Github Button
    local githubButton = UIButton.NewButton(panel, 20, -310, 250, 40, 0, {
        panelTemplate = "DefaultButtonPanel",
        textTemplate = "LoginButtonText",
        text = "Github",
    });
    githubButton:SetAnchor(0.0, 0.5);
    githubButton:SetRelativePoint(0.0, 0.5);

    -- Version Text
    local versionString = "Version 0.0.1(1) (Debug)"; -- TODO: Not hardcode this
    local versionText = panel:NewText(versionString, 20, 20, 0, "VersionText");
    versionText:SetAnchor(0.0, 0.0);
    versionText:SetRelativePoint(0.0, 0.0);

    -- System Button
    local systemButton = UIButton.NewButton(panel, -20, -260, 250, 40, 0, {
        panelTemplate = "DefaultButtonPanel",
        textTemplate = "LoginButtonText",
        text = "System",
    });
    systemButton:SetAnchor(1.0, 0.5);
    systemButton:SetRelativePoint(1.0, 0.5);

    -- Credits Button
    local creditsButton = UIButton.NewButton(panel, -20, -310, 250, 40, 0, {
        panelTemplate = "DefaultButtonPanel",
        textTemplate = "LoginButtonText",
        text = "Credits",
    });
    creditsButton:SetAnchor(1.0, 0.5);
    creditsButton:SetRelativePoint(1.0, 0.5);

    -- License Button
    local licenseButton = UIButton.NewButton(panel, -20, -360, 250, 40, 0, {
        panelTemplate = "DefaultButtonPanel",
        textTemplate = "LoginButtonText",
        text = "License",
    });
    licenseButton:SetAnchor(1.0, 0.5);
    licenseButton:SetRelativePoint(1.0, 0.5);

    -- Quit Button
    local quitButton = UIButton.NewButton(panel, -20, 20, 250, 40, 0, {
        panelTemplate = "DefaultButtonPanel",
        textTemplate = "LoginButtonText",
        text = "Quit",
    });
    quitButton:SetAnchor(1.0, 0.0);
    quitButton:SetRelativePoint(1.0, 0.0);

    -- Login handlers
    accountNameInputBox:SetOnSubmit(function(table)
        local rememberMe = rememberCheckbox:IsChecked();
        TryLogin(accountNameInputBox, passwordInputBox, rememberMe);
        return true; -- Returning true will unfocus the widget
    end);

    passwordInputBox:SetOnSubmit(function(table)
        local rememberMe = rememberCheckbox:IsChecked();
        TryLogin(accountNameInputBox, passwordInputBox, rememberMe);
        return true; -- Returning true will unfocus the widget
    end);

    loginButton:SetOnMouseUp(function(eventID, widget, ...)
        local rememberMe = rememberCheckbox:IsChecked();
        TryLogin(accountNameInputBox, passwordInputBox, rememberMe);
    end)

    -- Tab Handlers (TODO: Make a system to set this up easier)
    accountNameInputBox:SetOnTab(function(table)
        UI.FocusWidget(passwordInputBox.button);
    end);

    passwordInputBox:SetOnTab(function(table)
        UI.FocusWidget(accountNameInputBox.button);
    end);

    stack:Register("LoginScreen", panel, 0); -- Name, widget, slot, onPush, onPop

    if not IsOfflineMode() then
        if not IsInWorld() and GetAuthStage() == 0 then
            stack:Push("LoginScreen");

            if accountName ~= "" then
                passwordInputBox:Focus();
            else
                accountNameInputBox:Focus();
            end
        end
    end
end

local function OnGameLoaded(eventID : number, data : any)
    local stack = UIStack.Get("Menu");
    RegisterTemplates();

    CreateLoginScreen(stack);
end

RegisterEvent(GameEvent.Loaded, OnGameLoaded)