local input = require("@src/API/Input/Input");
local buttonAPI = require("@src/API/UI/Button");

export type ActionBarAPI =
{
    CreateActionBar : (parent : Widget, position : vector, numRows : number, numColumns : number, buttonSize : number, buttonPadding : vector) -> ActionBar
}

export type ActionBarButton =
{
    widget : buttonAPI.Button,
    callback : ((self : ActionBarButton) -> ())?,
    actionName : string,
    inputConnection : number,
}

export type ActionBar =
{
    buttons : { [number] : ActionBarButton },
    keyToButtonIndex : { [number] : number },

    position : vector,
    numRows : number,
    numColumns : number,
    buttonSize : number,
    buttonPadding : vector,

    SetEnabled : (self : ActionBar, enabled : boolean) -> (),

    GetButtonFromKey : (self : ActionBar, key : number) -> ActionBarButton?,
    SetKeyForButton : (self : ActionBar, index : number, key : number) -> (),

    SetButtonCallback : (self : ActionBar, index : number, callback : (self : ActionBarButton) -> ()) -> (),
    SetSpell : (self : ActionBar, index : number, spellID : number) -> (),
}

local actionBarAPI : ActionBarAPI = {};

local function SetupActionBarMethods(actionBarTable : ActionBar)
    actionBarTable.SetEnabled = function(self, enabled : boolean)
        for _, button in pairs(self.buttons) do
            button.widget:SetEnabled(enabled);
        end
    end

    actionBarTable.GetButtonFromKey = function(self, key : number) : ActionBarButton?
        local index = self.keyToButtonIndex[key];
        if index == nil then
            return nil;
        end

        return self.buttons[index];
    end

    actionBarTable.SetKeyForButton = function(self, index : number, key : number)
        if index < 1 or index > #self.buttons then
            error("ActionBar Button : Index out of bounds");
            return;
        end

        -- Remove any existing key mapping for this button
        for k, v in pairs(self.keyToButtonIndex) do
            if v == index then
                self.keyToButtonIndex[k] = nil;
                break;
            end
        end

        local button = self.buttons[index];
        local binding = input.Keyboard(key, InputModifier.None, InputModifierMatch.Any);
        if button.inputConnection == 0 then
            input.RegisterAction(button.actionName, `Action Bar Slot {index}`, "Action Bar", binding);
            button.inputConnection = input.Connect(button.actionName, function(phase)
                if phase == InputPhase.Pressed or phase == InputPhase.Repeated then
                    button.widget.panel:SetColor(vector.create(0.5, 0.5, 0.5));
                    return InputReply.Consumed;
                end

                if phase == InputPhase.Released or phase == InputPhase.Canceled then
                    button.widget.panel:SetColor(vector.create(1.0, 1.0, 1.0));
                    if phase == InputPhase.Released and button.callback ~= nil then
                        button:callback();
                    end
                end

                return InputReply.Consumed;
            end);
        else
            input.SetBinding(button.actionName, 1, binding);
        end

        local activeBinding = input.GetBinding(button.actionName, 1) or binding;
        button.widget:SetText(input.GetKeyName(activeBinding.code));
        self.keyToButtonIndex[activeBinding.code] = index;
    end
    
    actionBarTable.SetButtonCallback = function(table, index : number, callback : (self : ActionBarButton) -> ())
        if index < 1 or index > #table.buttons then
            error("ActionBar Button : Index out of bounds");
            return;
        end

        local actionButton = table.buttons[index];
        if callback == nil then
            actionButton.callback = nil;
            actionButton.widget:SetOnMouseDown(nil);
            actionButton.widget.panel:SetColor(vector.create(0.33, 0.33, 0.33));
            actionButton.widget.panel:SetAlpha(0.85);
        else
            actionButton.callback = callback;
            actionButton.widget.panel:SetColor(vector.create(1.0, 1.0, 1.0));
            actionButton.widget.panel:SetAlpha(1.0);
            actionButton.widget:SetOnMouseDown(function()
                actionButton:callback();
            end);
        end
    end

    actionBarTable.SetSpell = function(self, index : number, spellID : number)
        if index < 1 or index > #self.buttons then
            error("ActionBar Button : Index out of bounds");
            return;
        end

        local actionButton = self.buttons[index];
        if spellID == 0 then
            actionButton.widget:SetText("");
            actionButton.widget.panel:SetBackground(nil);
            self:SetButtonCallback(index, nil);
        else
            local spellInfo = Spell.GetSpellInfo(spellID);
            local spellIcon = Spell.GetIconInfo(spellInfo.IconID);

            local iconTexture = spellIcon.Texture or nil;
            actionButton.widget.panel:SetBackground(iconTexture);

            self:SetButtonCallback(index, function()
                Spell.CastByID(spellID);
            end);
        end
    end
end

function actionBarAPI.CreateActionBar(parent : Widget, position : vector, numRows : number, numColumns : number, buttonSize : number, buttonPadding : vector) : ActionBar
    if numRows < 1 then numRows = 1 end
    if numColumns < 1 then numColumns = 1 end
    
    local actionBarTable : ActionBar = {
        buttons = {},
        keyToButtonIndex = {},

        position = position,
        numRows = numRows,
        numColumns = numColumns,
        buttonSize = buttonSize,
        buttonPadding = buttonPadding
    };

    for row = 1, numRows do
        for col = 1, numColumns do
            -- Calculate button position
            local posX = position.x + (col - 1) * (buttonSize + buttonPadding.x);
            local posY = position.y + (row - 1) * (buttonSize + buttonPadding.y);

            local button = buttonAPI.NewButton(parent, posX, posY, buttonSize, buttonSize, 0, {
                text = "",
                panelTemplate = "ActionBarButton",
                textTemplate = "ActionBarButtonText",
            });

            local i = (row - 1) * numColumns + col;
            actionBarTable.buttons[i] =
            {
                widget = button,
                callback = nil,
                actionName = `ActionBarSlot{i}`,
                inputConnection = 0,
            };

            button.panel:SetAlpha(0.85);

            button.text:SetAnchor(1.0, 0.0)
            button.text:SetRelativePoint(1.0, 0.0)
            button.text:SetWrapWidth(buttonSize - 4)
        end
    end

    SetupActionBarMethods(actionBarTable);

    return actionBarTable;
end

return actionBarAPI
