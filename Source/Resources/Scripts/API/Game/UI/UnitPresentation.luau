export type ReactionPalette =
{
    hostile : vec,
    unfriendly : vec,
    neutral : vec,
    friendly : vec,
}

export type UnitPresentationAPI =
{
    GetReactionColor : (reaction : integer, palette : ReactionPalette) -> vec,
    GetHealthFillColor : (healthPercent : number) -> vec,
    LerpColor : (from : vec, to : vec, amount : number) -> vec,
}
local unitPresentationAPI : UnitPresentationAPI = { };

local FULL_HEALTH_COLOR = vec.New(0.32, 0.62, 0.07);
local MID_HEALTH_COLOR = vec.New(0.64, 0.58, 0.07);
local LOW_HEALTH_COLOR = vec.New(0.82, 0.15, 0.08);
local EMPTY_HEALTH_COLOR = vec.New(0.44, 0.06, 0.045);

function unitPresentationAPI.GetReactionColor(reaction : integer, palette : ReactionPalette) : vec
    if reaction == FactionReaction.Hostile then return palette.hostile; end
    if reaction == FactionReaction.Unfriendly then return palette.unfriendly; end
    if reaction == FactionReaction.Friendly then return palette.friendly; end
    return palette.neutral;
end

function unitPresentationAPI.LerpColor(from : vec, to : vec, amount : number) : vec
    return from + ((to - from) * math.clamp(amount, 0, 1));
end

function unitPresentationAPI.GetHealthFillColor(healthPercent : number) : vec
    if healthPercent <= 0.35 then
        return unitPresentationAPI.LerpColor(EMPTY_HEALTH_COLOR, LOW_HEALTH_COLOR, healthPercent / 0.35);
    elseif healthPercent <= 0.50 then
        return unitPresentationAPI.LerpColor(LOW_HEALTH_COLOR, MID_HEALTH_COLOR, (healthPercent - 0.35) / 0.15);
    end

    return unitPresentationAPI.LerpColor(MID_HEALTH_COLOR, FULL_HEALTH_COLOR, (healthPercent - 0.50) / 0.50);
end

return unitPresentationAPI;
