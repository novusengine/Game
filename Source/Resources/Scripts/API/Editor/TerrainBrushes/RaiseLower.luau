local Terrain = require("@src/API/Editor/Terrain");

return
{
    id = "raise_lower",
    displayName = "Raise / Lower",
    Apply = function(context)
        local signedStrength = if context.inverted then -context.strength else context.strength;
        return Terrain.ApplySample(Terrain.Operations.AddHeight, context.position, context.radius, signedStrength, context.hardness, context.deltaTime, context.position.y);
    end,
};
