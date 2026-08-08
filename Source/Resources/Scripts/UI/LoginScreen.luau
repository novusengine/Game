local UIButton = require("@src/API/UI/Button")
local UICheckbox = require("@src/API/UI/Checkbox")
local UIInputBox = require("@src/API/UI/InputBox")
local FrameManager = require("@src/API/Game/FrameManager")

local function RegisterTemplates()
    UI.RegisterPanelTemplate("LoginBackgroundPanel",
    {
        background = "texture/ui/loginscreen.png",
    });

    UI.RegisterTextTemplate("LoginText",
    {
        font = "Data/Fonts/OpenSans-Regular.ttf",
        size = 16.0,
        color = vec.New(255.0/255.0, 199.0/255.0, 0.0/255.0),
        borderSize = 1.5,
        borderColor = vec.New(0.2, 0.2, 0.2),
    });

    UI.RegisterTextTemplate("LoginButtonText",
    {
        font = "Data/Fonts/OpenSans-Regular.ttf",
        size = 16.0,
        color = vec.New(255.0/255.0, 199.0/255.0, 0.0/255.0),
        borderSize = 1.0,
        borderColor = vec.New(0.0, 0.0, 0.0),
    });

    UI.RegisterTextTemplate("VersionText",
    {
        font = "Data/Fonts/OpenSans-Regular.ttf",
        size = 12.0,
        color = vec.New(255.0/255.0, 199.0/255.0, 0.0/255.0),
        borderSize = 1.5,
        borderColor = vec.New(0.0, 0.0, 0.0),
    });
end

local function TryLogin(accountInputBox, passwordInputBox, rememberMe)
    Login(accountInputBox.text, passwordInputBox.text, rememberMe);
    passwordInputBox:SetText("");
end

local function CreateLoginScreen()
    -- Create Canvas
    local canvas = UI.GetCanvas("LoginScreen", vec.New(0, 0), vec.New(1920, 1080)); -- TODO: We don't want to hardcode the reference resolution here

    -- Create Panel
    local panel = canvas:NewPanel(vec.New(0, 0), vec.New(1920, 1080), 0i, "LoginBackgroundPanel");
    panel:SetEnabled(false);
    panel:SetAnchor(vec.New(0.5, 0.5));
    panel:SetRelativePoint(vec.New(0.5, 0.5));

    -- Account Name Input
    local accountName = GetAccountName();
    local accountNameInputBox = UIInputBox.New(panel, vec.New(0, -120), vec.New(200, 40), 0i, {
        panelTemplate = "DialogBox",
        textTemplate = "DefaultButtonText",
        defaultText = accountName
    });
    accountNameInputBox:SetAnchor(vec.New(0.5, 0.5));
    accountNameInputBox:SetRelativePoint(vec.New(0.5, 0.5));

    -- Account Name Text
    local accountNameText = accountNameInputBox.button:NewText("Account Name", vec.New(0, 0), 0i, "LoginText");
    accountNameText:SetAnchor(vec.New(0.5, 1.0));
    accountNameText:SetRelativePoint(vec.New(0.5, 0.0));

    -- Password Input
    local passwordInputBox = UIInputBox.New(panel, vec.New(0, -200), vec.New(200, 40), 0i, {
        panelTemplate = "DialogBox",
        textTemplate = "DefaultButtonText",
        secure = true,
    });
    passwordInputBox:SetAnchor(vec.New(0.5, 0.5));
    passwordInputBox:SetRelativePoint(vec.New(0.5, 0.5));

    -- Password Text
    local passwordText = passwordInputBox.button:NewText("Password", vec.New(0, 0), 0i, "LoginText");
    passwordText:SetAnchor(vec.New(0.5, 1.0));
    passwordText:SetRelativePoint(vec.New(0.5, 0.0));

    -- Login Button
    local loginButton = UIButton.New(panel, vec.New(0, -270), vec.New(200, 40), 0i, {
        panelTemplate = "DefaultButtonPanel",
        textTemplate = "LoginButtonText",
        text = "Login",
    });
    loginButton:SetAnchor(vec.New(0.5, 0.5));
    loginButton:SetRelativePoint(vec.New(0.5, 0.5));

    -- Remember Checkbox
    local rememberCheckbox = UICheckbox.New(loginButton.root, vec.New(0, -5), vec.New(20, 20), 0i,
    {
        backgroundTemplate = "DefaultCheckboxBackground",
        fillTemplate = "DefaultCheckboxFill",
        fillInset = 1,
    });
    rememberCheckbox:SetRelativePoint(vec.New(0.0, 1.0));
    rememberCheckbox:Click();

    -- Remember Text
    local rememberText = rememberCheckbox:NewText("Remember Account", vec.New(10, 0), 0i, "LoginButtonText");
    rememberText:SetAnchor(vec.New(1.0, 0.5));
    rememberText:SetRelativePoint(vec.New(0.0, 0.5));

    -- Discord Button
    local discordButton = UIButton.New(panel, vec.New(20, -260), vec.New(180, 40), 0i, {
        panelTemplate = "DefaultButtonPanel",
        textTemplate = "LoginButtonText",
        text = "Discord",
    });
    discordButton:SetAnchor(vec.New(0.0, 0.5));
    discordButton:SetRelativePoint(vec.New(0.0, 0.5));

    -- Github Button
    local githubButton = UIButton.New(panel, vec.New(20, -310), vec.New(180, 40), 0i, {
        panelTemplate = "DefaultButtonPanel",
        textTemplate = "LoginButtonText",
        text = "Github",
    });
    githubButton:SetAnchor(vec.New(0.0, 0.5));
    githubButton:SetRelativePoint(vec.New(0.0, 0.5));

    -- Version Text
    local versionString = "Version 0.0.1(1) (Debug)"; -- TODO: Not hardcode this
    local versionText = panel:NewText(versionString, vec.New(27, 20), 0i, "VersionText");
    versionText:SetAnchor(vec.New(0.0, 0.0));
    versionText:SetRelativePoint(vec.New(0.0, 0.0));

    -- System Button
    local systemButton = UIButton.New(panel, vec.New(-20, -260), vec.New(180, 40), 0i, {
        panelTemplate = "DefaultButtonPanel",
        textTemplate = "LoginButtonText",
        text = "System",
    });
    systemButton:SetAnchor(vec.New(1.0, 0.5));
    systemButton:SetRelativePoint(vec.New(1.0, 0.5));

    -- Credits Button
    local creditsButton = UIButton.New(panel, vec.New(-20, -310), vec.New(180, 40), 0i, {
        panelTemplate = "DefaultButtonPanel",
        textTemplate = "LoginButtonText",
        text = "Credits",
    });
    creditsButton:SetAnchor(vec.New(1.0, 0.5));
    creditsButton:SetRelativePoint(vec.New(1.0, 0.5));

    -- License Button
    local licenseButton = UIButton.New(panel, vec.New(-20, -360), vec.New(180, 40), 0i, {
        panelTemplate = "DefaultButtonPanel",
        textTemplate = "LoginButtonText",
        text = "License",
    });
    licenseButton:SetAnchor(vec.New(1.0, 0.5));
    licenseButton:SetRelativePoint(vec.New(1.0, 0.5));

    -- Quit Button
    local quitButton = UIButton.New(panel, vec.New(-20, 20), vec.New(180, 40), 0i, {
        panelTemplate = "DefaultButtonPanel",
        textTemplate = "LoginButtonText",
        text = "Quit",
    });
    quitButton:SetAnchor(vec.New(1.0, 0.0));
    quitButton:SetRelativePoint(vec.New(1.0, 0.0));
    quitButton:SetOnMouseUp(function(eventID, widget, ...)
        ExitGame();
    end)

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
        UI.FocusWidget(passwordInputBox.root);
    end);

    passwordInputBox:SetOnTab(function(table)
        UI.FocusWidget(accountNameInputBox.root);
    end);

    FrameManager.Register("LoginScreen", {
        root = panel,
        role = "System",
        dismissible = false,
    });

    if not IsOfflineMode() then
        if not IsInWorld() and GetAuthStage() == 0i then
            FrameManager.Open("LoginScreen");

            if accountName ~= "" then
                passwordInputBox:Focus();
            else
                accountNameInputBox:Focus();
            end
        end
    end
end

local function OnGameLoaded(eventID : integer, data : any)
    RegisterTemplates();

    CreateLoginScreen();
end

RegisterEvent(GameEvent.Loaded, OnGameLoaded);
