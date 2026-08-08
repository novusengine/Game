local GameTooltip = require("@src/API/Game/UI/GameTooltip");
export type SpellAction =
{
    spellID : integer,
    GetIcon : (self : SpellAction) -> string,
    IsAvailable : (self : SpellAction) -> boolean,
    Activate : (self : SpellAction) -> boolean,
    ShowTooltip : (self : SpellAction) -> (),
    HideTooltip : (self : SpellAction) -> (),
}
export type SpellActionAPI = { New : (spellID : integer) -> SpellAction }
local spellActionAPI : SpellActionAPI = { };

function spellActionAPI.New(spellID : integer)
    local self = { spellID = spellID };
    function self:GetIcon()
        local spell = Spell.GetSpellInfo(self.spellID);
        local icon = spell and Spell.GetIconInfo(spell.IconID) or nil;
        return icon and icon.Texture or "texture/unknown.png";
    end
    -- Spell-known and cast-usability queries are not exposed yet. Do not present configured actions
    -- as unavailable based only on whether their metadata happened to resolve during this refresh.
    function self:IsAvailable() return true; end
    function self:Activate() return Spell.CastByID(self.spellID); end
    function self:ShowTooltip()
        GameTooltip.SetSpell(self.spellID);
        GameTooltip.Show();
    end
    function self:HideTooltip()
        GameTooltip.ClearLines();
        GameTooltip.Hide();
    end
    return self;
end

return spellActionAPI;
