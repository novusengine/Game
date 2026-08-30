export type MacroAction =
{
    macroID : integer,
    GetIcon : (self : MacroAction) -> string,
    IsAvailable : (self : MacroAction) -> boolean,
    Activate : (self : MacroAction) -> boolean,
}
export type MacroActionAPI = { New : (macroID : integer, icon : string, activate : (integer) -> boolean) -> MacroAction }
local macroActionAPI : MacroActionAPI = { };

function macroActionAPI.New(macroID : integer, icon : string, activate : (macroID : integer) -> boolean)
    local self = { macroID = macroID, icon = icon, _activate = activate };
    function self:GetIcon() return self.icon; end
    function self:IsAvailable() return true; end
    function self:Activate() return self._activate(self.macroID); end
    return self;
end

return macroActionAPI;
