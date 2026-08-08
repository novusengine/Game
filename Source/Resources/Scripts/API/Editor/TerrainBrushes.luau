export type StrokeState = { [string] : any }

export type Context =
{
    position : vec,
    deltaTime : number,
    radius : number,
    strength : number,
    hardness : number,
    targetOpacity : number,
    inverted : boolean,
    stroke : StrokeState,
}

export type Brush =
{
    id : string,
    displayName : string,
    BeginStroke : ((context : Context) -> ())?,
    Apply : (context : Context) -> boolean,
    EndStroke : ((context : Context, committed : boolean) -> ())?,
}

export type RegistryAPI =
{
    Register : (brush : Brush) -> (),
    Get : (id : string) -> Brush?,
    GetAll : () -> { Brush },
}

local brushes : { [string] : Brush } = { };
local orderedBrushes : { Brush } = { };
local registryAPI : RegistryAPI = { };

function registryAPI.Register(brush : Brush)
    if brush.id == "" then error("TerrainBrushes: brush id cannot be empty"); end
    if brushes[brush.id] ~= nil then error(`TerrainBrushes: brush '{brush.id}' is already registered`); end

    brushes[brush.id] = brush;
    table.insert(orderedBrushes, brush);
end

function registryAPI.Get(id : string) : Brush?
    return brushes[id];
end

function registryAPI.GetAll() : { Brush }
    return table.clone(orderedBrushes);
end

return registryAPI;
