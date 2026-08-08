local Terrain = require("@src/API/Editor/Terrain");

return
{
    id = "flatten",
    displayName = "Flatten",
    BeginStroke = function(context)
        context.stroke.targetHeight = context.position.y;
    end,
    Apply = function(context)
        return Terrain.ApplySample(Terrain.Operations.Flatten, context.position, context.radius, context.strength, context.hardness, context.deltaTime, context.stroke.targetHeight);
    end,
};
