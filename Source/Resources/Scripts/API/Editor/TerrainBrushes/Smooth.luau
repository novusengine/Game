local Terrain = require("@src/API/Editor/Terrain");

return
{
    id = "smooth",
    displayName = "Smooth",
    Apply = function(context)
        return Terrain.ApplySample(Terrain.Operations.Smooth, context.position, context.radius, context.strength, context.hardness, context.deltaTime, context.position.y);
    end,
};
