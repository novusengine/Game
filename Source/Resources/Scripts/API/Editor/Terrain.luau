export type State =
{
    available : boolean,
    layoutAvailable : boolean,
    enabled : boolean,
    strokeActive : boolean,
    cursorHit : boolean,
    canUndo : boolean,
    canRedo : boolean,
    dirtyChunkCount : integer,
    blockedPaintCellCount : integer,
    layoutGeneration : integer,
    topologyDirty : boolean,
    cursorPosition : vec,
}

export type ChunkLayoutState =
{
    generation : integer,
    headerDirty : boolean,
    stride : integer,
    occupiedChunkIDs : { integer },
}

export type TextureLayer =
{
    path : string,
    textureHash : integer,
    layerIndex : integer,
    averageWeight : number,
}

export type HeightFieldImportPreview =
{
    success : boolean,
    error : string?,
    sourceWidth : integer?,
    sourceHeight : integer?,
    footprintWidth : number?,
    footprintHeight : number?,
    minChunkX : number?,
    minChunkY : number?,
    maxChunkX : number?,
    maxChunkY : number?,
    sourcePixelsPerPatchX : number?,
    sourcePixelsPerPatchY : number?,
    occupiedChunkCount : integer?,
    occupiedChunkIDs : { integer }?,
}

export type HeightFieldImportResult =
{
    success : boolean,
    error : string?,
    importedChunkCount : integer,
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
    SetPaintLayer : (layerIndex : integer) -> boolean, -- 0 selects automatic assignment; 1-4 select an explicit layer.
    ApplyPaintSample : (position : vec, radius : number, pressure : number, hardness : number, deltaTime : number, targetOpacity : number) -> boolean,
    ApplyVertexColorSample : (position : vec, radius : number, flow : number, hardness : number, deltaTime : number, targetColor : vec) -> boolean,
    GetCursorLayers : () -> { TextureLayer },
    CommitStroke : () -> boolean,
    CancelStroke : () -> boolean,
    Undo : () -> boolean,
    Redo : () -> boolean,
    Save : () -> boolean,
    GetChunkLayout : () -> ChunkLayoutState?,
    AddChunk : (x : integer, y : integer) -> boolean,
    RemoveChunk : (x : integer, y : integer) -> boolean,
    ResetChunk : (x : integer, y : integer) -> boolean,
    GoToChunk : (x : integer, y : integer) -> boolean,
    PreviewHeightFieldImport : (manifestPath : string) -> HeightFieldImportPreview,
    ImportHeightField : (manifestPath : string, minimumHeight : number, maximumHeight : number) -> HeightFieldImportResult,
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
    SetPaintLayer = TerrainEditor.SetPaintLayer,
    ApplyPaintSample = TerrainEditor.ApplyPaintSample,
    ApplyVertexColorSample = TerrainEditor.ApplyVertexColorSample,
    GetCursorLayers = TerrainEditor.GetCursorLayers,
    CommitStroke = TerrainEditor.CommitStroke,
    CancelStroke = TerrainEditor.CancelStroke,
    Undo = TerrainEditor.Undo,
    Redo = TerrainEditor.Redo,
    Save = TerrainEditor.Save,
    GetChunkLayout = TerrainEditor.GetChunkLayout,
    AddChunk = TerrainEditor.AddChunk,
    RemoveChunk = TerrainEditor.RemoveChunk,
    ResetChunk = TerrainEditor.ResetChunk,
    GoToChunk = TerrainEditor.GoToChunk,
    PreviewHeightFieldImport = TerrainEditor.PreviewHeightFieldImport,
    ImportHeightField = TerrainEditor.ImportHeightField,
};

return terrainAPI;
