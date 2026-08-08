local Terrain = require("@src/API/Editor/Terrain");

return
{
    id = "texture_paint",
    displayName = "Texture Paint",
    Apply = function(context)
        local targetOpacity = if context.inverted then 0.0 else context.targetOpacity;
        return Terrain.ApplyPaintSample(context.position, context.radius, context.strength / 100.0, context.hardness, context.deltaTime, targetOpacity);
    end,
};
