export type State =
{
    available : boolean,
    enabled : boolean,
    strokeActive : boolean,
    cursorHit : boolean,
    canUndo : boolean,
    canRedo : boolean,
    dirtyChunkCount : integer,
    blockedPaintCellCount : integer,
    cursorPosition : vec,
}

export type TextureLayer =
{
    path : string,
    textureHash : integer,
    layerIndex : integer,
    averageWeight : number,
}

export type TerrainAPI =
{
    Operations : { [string] : integer },
    GetState : () -> State?,
    SetEnabled : (enabled : boolean) -> boolean,
    SetPreviewRadius : (radius : number) -> boolean,
    BeginStroke : (name : string) -> boolean,
    ApplySample : (operation : integer, position : vec, radius : number, strength : number, hardness : number, deltaTime : number, targetHeight : number) -> boolean,
    SetPaintTexture : (virtualPath : string) -> boolean,
    ApplyPaintSample : (position : vec, radius : number, pressure : number, hardness : number, deltaTime : number, targetOpacity : number) -> boolean,
    GetCursorLayers : () -> { TextureLayer },
    CommitStroke : () -> boolean,
    CancelStroke : () -> boolean,
    Undo : () -> boolean,
    Redo : () -> boolean,
    Save : () -> boolean,
}

local terrainAPI : TerrainAPI =
{
    Operations =
    {
        AddHeight = 0i,
        Flatten = 1i,
        Smooth = 2i,
    },
    GetState = TerrainEditor.GetState,
    SetEnabled = TerrainEditor.SetEnabled,
    SetPreviewRadius = TerrainEditor.SetPreviewRadius,
    BeginStroke = TerrainEditor.BeginStroke,
    ApplySample = TerrainEditor.ApplySample,
    SetPaintTexture = TerrainEditor.SetPaintTexture,
    ApplyPaintSample = TerrainEditor.ApplyPaintSample,
    GetCursorLayers = TerrainEditor.GetCursorLayers,
    CommitStroke = TerrainEditor.CommitStroke,
    CancelStroke = TerrainEditor.CancelStroke,
    Undo = TerrainEditor.Undo,
    Redo = TerrainEditor.Redo,
    Save = TerrainEditor.Save,
};

return terrainAPI;
