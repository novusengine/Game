local Terrain = require("@src/API/Editor/Terrain");

return
{
    id = "vertex_paint",
    displayName = "Vertex Paint",
    Apply = function(context)
        local targetColor = if context.inverted then vec.New(1.0, 1.0, 1.0) else context.targetColor;
        return Terrain.ApplyVertexColorSample(context.position, context.radius, context.strength / 100.0, context.hardness, context.deltaTime, targetColor);
    end,
};
