local UIButton = require("@src/API/UI/Button");
local UIGridLayout = require("@src/API/UI/GridLayout");
local UIHorizontalLayout = require("@src/API/UI/HorizontalLayout");
local RootedWidget = require("@src/API/UI/RootedWidget");
local TextUtil = require("@src/API/UI/TextUtil");
local UIVerticalLayout = require("@src/API/UI/VerticalLayout");

export type Node = { [string] : any }

export type TextOverflowValue = { [string] : any }

export type Options =
{
    id : string?,
    position : vec?,
    offset : vec?,
    size : vec?,
    width : number?,
    height : number?,
    grow : number?,
    fill : boolean?,
    anchor : vec?,
    relativePoint : vec?,
    template : string?,
    layer : integer?,
    enabled : boolean?,
    visible : boolean?,
    interactable : boolean?,
    focusable : boolean?,
    alpha : number?,
    clipChildren : boolean?,
    borderColor : vec?,
    borderSize : number?,
    fontSize : number?,
    color : any?,
    wrapWidth : number?,
    overflow : TextOverflowValue?,
    inset : (number | vec)?,
    align : ("left" | "center" | "right" | "start" | "end")?,
    verticalAlign : ("top" | "center" | "bottom" | "start" | "end")?,
    layoutOpts : { [string] : any }?,
    events : { [string] : any }?,
    setup : ((any) -> ())?,
    factory : ((any, vec, vec, integer) -> any)?,
    onClick : (() -> ())?,
    [string] : any,
}

export type View =
{
    root : any,
    refs : { [string] : any },
    Get : (self : View, id : string) -> any?,
    Destroy : (self : View) -> (),
}

export type AnchorTable =
{
    Center : vec,
    TopLeft : vec,
    Top : vec,
    TopRight : vec,
    Left : vec,
    Right : vec,
    BottomLeft : vec,
    Bottom : vec,
    BottomRight : vec,
}

export type TextOverflowTable =
{
    Visible : TextOverflowValue,
    Wrap : TextOverflowValue,
    Truncate : TextOverflowValue,
}

export type DeclarativeAPI =
{
    Anchor : AnchorTable,
    TextOverflow : TextOverflowTable,
    Build : (parent : any, description : Node) -> View,
    Panel : (options : Options?, ...any) -> Node,
    Grid : (options : Options?, ...any) -> Node,
    Component : (options : Options) -> Node,
    Vertical : (options : Options?, ...any) -> Node,
    Horizontal : (options : Options?, ...any) -> Node,
    Text : (text : string, options : Options?) -> Node,
    Button : (text : string, options : Options?) -> Node,
}

local declarativeAPI : DeclarativeAPI = { };

-- Button panels have no intrinsic native size, so compact buttons use one stable normal size.
local DEFAULT_BUTTON_SIZE = vec.New(120, 28);
local DEFAULT_TEXT_INSET = vec.New(3, 1);
local anchors : AnchorTable =
{
    Center = vec.New(0.5, 0.5),
    TopLeft = vec.New(0.0, 1.0),
    Top = vec.New(0.5, 1.0),
    TopRight = vec.New(1.0, 1.0),
    Left = vec.New(0.0, 0.5),
    Right = vec.New(1.0, 0.5),
    BottomLeft = vec.New(0.0, 0.0),
    Bottom = vec.New(0.5, 0.0),
    BottomRight = vec.New(1.0, 0.0),
};

declarativeAPI.Anchor = anchors;

local textOverflow : TextOverflowTable =
{
    Visible = { },
    Wrap = { },
    Truncate = { },
};

local TEXT_OVERFLOW_MODES : { [any] : string } =
{
    [textOverflow.Visible] = "Visible",
    [textOverflow.Wrap] = "Wrap",
    [textOverflow.Truncate] = "Truncate",
};

declarativeAPI.TextOverflow = textOverflow;

local EVENT_SETTERS =
{
    mouseDown = "SetOnMouseDown",
    mouseUp = "SetOnMouseUp",
    mouseRelease = "SetOnMouseRelease",
    mouseHeld = "SetOnMouseHeld",
    mouseScroll = "SetOnMouseScroll",
    hoverBegin = "SetOnHoverBegin",
    hoverEnd = "SetOnHoverEnd",
    hoverHeld = "SetOnHoverHeld",
    focusBegin = "SetOnFocusBegin",
    focusEnd = "SetOnFocusEnd",
    focusHeld = "SetOnFocusHeld",
    keyboard = "SetOnKeyboard",
};

local LAYOUT_OPTION_KEYS =
{
    spacing = true,
    padding = true,
    paddingTop = true,
    paddingBottom = true,
    paddingLeft = true,
    paddingRight = true,
    reverse = true,
    panelTemplate = true,
    alpha = true,
    widthMode = true,
    heightMode = true,
    alignH = true,
    alignV = true,
    defaultChildOpts = true,
};

local GRID_OPTION_KEYS =
{
    spacing = true,
    spacingX = true,
    spacingY = true,
    padding = true,
    paddingTop = true,
    paddingBottom = true,
    paddingLeft = true,
    paddingRight = true,
    columns = true,
    rows = true,
    cellSizeX = true,
    cellSizeY = true,
    cellAlign = true,
    cellAlignH = true,
    cellAlignV = true,
    gridAlignH = true,
    gridAlignV = true,
    direction = true,
    widthMode = true,
    heightMode = true,
    panelTemplate = true,
    alpha = true,
};

-- Options consumed by the declarative layer instead of being forwarded into
-- widget-specific option tables. Unknown non-reserved keys remain available
-- to compound widgets such as Button.
local DECLARATIVE_RESERVED_OPTION_KEYS =
{
    id = true,
    position = true,
    offset = true,
    size = true,
    width = true,
    height = true,
    grow = true,
    fill = true,
    anchor = true,
    relativePoint = true,
    template = true,
    layer = true,
    inset = true,
    align = true,
    verticalAlign = true,
    layoutOpts = true,
    events = true,
    setup = true,
    factory = true,
    overflow = true,
    onClick = true,
};

local function CopyTable(source : any) : { [string] : any }
    if source == nil then return { }; end
    if type(source) ~= "table" then error("Declarative: options must be a table", 3); end

    local copy = { };
    for key, value in source do copy[key] = value; end
    return copy;
end

local function MakeCompactNode(kind : string, options : any, children : { any }) : Node
    return
    {
        _compactKind = kind,
        options = options or { },
        children = children,
    };
end

local function ValidateComponentOptions(options : { [string] : any }, nodeType : string)
    if type(options.factory) ~= "function" then
        error(`Declarative.Build: {nodeType} requires a function 'factory'`, 3);
    end

    if options.size ~= nil then
        local width, height;
        local success = pcall(function()
            width = options.size.x;
            height = options.size.y;
        end);
        if not success or type(width) ~= "number" or type(height) ~= "number" then
            error(`Declarative.Build: {nodeType} 'size' must provide numeric x and y`, 3);
        end
    elseif type(options.width) ~= "number" or type(options.height) ~= "number" then
        error(`Declarative.Build: {nodeType} requires complete 'size' or numeric 'width' and 'height'`, 3);
    end
end

local function GetParentSize(parent : any) : any?
    if parent == nil or type(parent.GetSize) ~= "function" then return nil; end
    local success, size = pcall(function() return parent:GetSize(); end);
    return if success then size else nil;
end

local function GetParentLayoutAxis(parent : any) : string?
    if type(parent) ~= "table" then return nil; end

    if type(parent.GetAxis) == "function" then
        local success, axis = pcall(function() return parent:GetAxis(); end);
        if success and (axis == "x" or axis == "y") then return axis; end
    end

    -- Backward-compatible fallback for layout objects created before GetAxis existed.
    local options = rawget(parent, "_opts");
    if type(options) == "table" and (options.axis == "x" or options.axis == "y") then return options.axis; end
    return nil;
end

local function ResolveSize(options : { [string] : any }, parent : any, parentLayoutAxis : string?, fallbackSize : vec?, naturalSize : vec?) : vec
    if options.size ~= nil then return options.size; end

    local width = options.width;
    local height = options.height;
    if options.fill == true and parentLayoutAxis == nil then
        local parentSize = GetParentSize(parent);
        if parentSize ~= nil then
            if width == nil then width = parentSize.x; end
            if height == nil then height = parentSize.y; end
        end
    end
    if fallbackSize ~= nil then
        if width == nil then width = fallbackSize.x; end
        if height == nil then height = fallbackSize.y; end
    end
    if naturalSize ~= nil then
        if width == nil then width = naturalSize.x; end
        if height == nil then height = naturalSize.y; end
    end

    return vec.New(width or 0, height or 0);
end

local function GetDimension(options : { [string] : any }, axis : string) : number?
    -- A full size is the explicit semantic size; width/height fill only its absence.
    if options.size ~= nil then return if axis == "x" then options.size.x else options.size.y; end
    return if axis == "x" then options.width else options.height;
end

local function GetExplicitDimension(options : { [string] : any }, axis : string) : number?
    if options.size ~= nil then return if axis == "x" then options.size.x else options.size.y; end

    local layoutOpts = options.layoutOpts;
    local layoutSizeKey = `size{string.upper(axis)}`;
    if type(layoutOpts) == "table" and layoutOpts[layoutSizeKey] ~= nil then return layoutOpts[layoutSizeKey]; end

    return if axis == "x" then options.width else options.height;
end

local function ResolveTextOverflow(options : { [string] : any }) : string
    if options.overflow == nil then
        return if options.wrapWidth ~= nil then "Wrap" else "Visible";
    end

    local mode = TEXT_OVERFLOW_MODES[options.overflow];
    if mode == nil then
        error("Declarative.Text: overflow must be Declarative.TextOverflow.Visible, .Wrap, or .Truncate", 3);
    end
    if mode == "Visible" and options.wrapWidth ~= nil then
        error("Declarative.Text: Visible overflow cannot be combined with wrapWidth", 3);
    end
    return mode;
end

local function ValidateTextOptions(options : { [string] : any })
    local mode = ResolveTextOverflow(options);
    if options.wrapWidth ~= nil and (type(options.wrapWidth) ~= "number" or options.wrapWidth < 0) then
        error("Declarative.Text: wrapWidth must be a non-negative number", 3);
    end

    if mode ~= "Visible" then
        local width = GetExplicitDimension(options, "x") or options.wrapWidth;
        if width == nil then
            error(`Declarative.Text: {mode} requires an explicit content width`, 3);
        end
        if type(width) ~= "number" or width <= 0 then
            error("Declarative.Text: Wrap and Truncate require a positive content width", 3);
        end

        local height = GetExplicitDimension(options, "y");
        if mode == "Wrap" and height ~= nil and (type(height) ~= "number" or height <= 0) then
            error("Declarative.Text: Wrap height must be positive when specified", 3);
        end
    end
end

local function ResolveTextInset(options : { [string] : any }) : vec
    if options.inset == nil then return DEFAULT_TEXT_INSET; end
    if type(options.inset) == "number" then
        local inset = math.max(0, options.inset);
        return vec.New(inset, inset);
    end

    local insetX = math.max(0, options.inset.x or 0);
    local insetY = math.max(0, options.inset.y or 0);
    return vec.New(insetX, insetY);
end

local function GetPadding(options : { [string] : any }, side : string) : number
    return options[`padding{side}`] or options.padding or 0;
end

local function MeasureText(text : string, options : { [string] : any }) : vec
    local fontSize = options.fontSize or 16;
    local lineHeight = math.max(1, fontSize * 1.25);
    local lineCount = 0;
    local longestLineLength = 0;

    -- The native measurement API supplies exact bounds when the template is
    -- available; this deterministic estimate is only the fallback for early or
    -- customized descriptor contexts. An explicit font size uses a conservative
    -- line-count estimate because UI.WrapText measures the registered template size.
    for line in string.gmatch(`{text}\n`, "(.-)\n") do
        lineCount += 1;
        longestLineLength = math.max(longestLineLength, #line);
    end
    lineCount = math.max(1, lineCount);

    local measuredSize : vec?;
    local template = options.template or "DefaultButtonText";
    local wrapWidth = options.wrapWidth;
    if wrapWidth == nil and ResolveTextOverflow(options) == "Wrap" then
        wrapWidth = GetExplicitDimension(options, "x");
    end

    if options.fontSize == nil and wrapWidth == nil then
        local success, size = pcall(function() return UI.CalculateTextSize(text, template); end);
        if success then measuredSize = size; end
    elseif wrapWidth ~= nil then
        local success, wrappedText, size = pcall(function() return UI.WrapText(text, template, wrapWidth); end);
        if success then
            local wrappedLineCount = 0;
            for _ in string.gmatch(`{wrappedText}\n`, "(.-)\n") do wrappedLineCount += 1; end
            local estimatedLineCount = math.max(wrappedLineCount, math.ceil(longestLineLength * fontSize * 0.5 / wrapWidth));
            measuredSize = vec.New(size.x, math.max(size.y, estimatedLineCount * lineHeight));
        end
    end

    local naturalSize = measuredSize or vec.New(math.max(1, longestLineLength * fontSize * 0.5), lineCount * lineHeight);
    local inset = ResolveTextInset(options);
    local width = GetExplicitDimension(options, "x") or naturalSize.x + inset.x * 2;
    local height = GetExplicitDimension(options, "y") or naturalSize.y + inset.y * 2;
    return vec.New(width, height);
end

local function ResolveHorizontalTextAlign(value : any) : string
    if value == nil or value == "left" or value == "start" then return "left"; end
    if value == "center" then return "center"; end
    if value == "right" or value == "end" then return "right"; end
    error(`Declarative: unsupported Text align '{value}'`, 3);
end

local function ResolveVerticalTextAlign(value : any) : string
    if value == nil then return "center"; end
    if value == "top" or value == "start" then return "top"; end
    if value == "center" then return "center"; end
    if value == "bottom" or value == "end" then return "bottom"; end
    error(`Declarative: unsupported Text verticalAlign '{value}'`, 3);
end

local function CacheMeasuredSize(cache : { [any] : vec }, node : Node, size : vec) : vec
    cache[node] = size;
    return size;
end

local function MeasureNode(node : Node, cache : { [any] : vec }) : vec
    if type(node) ~= "table" then return vec.New(0, 0); end

    local cached = cache[node];
    if cached ~= nil then return cached; end

    local compactKind = node._compactKind;
    local nodeType = if compactKind == "Vertical" then "VerticalLayout" elseif compactKind == "Horizontal" then "HorizontalLayout" elseif compactKind ~= nil then compactKind else node.type;
    local options = if compactKind ~= nil then CopyTable(node.options) else CopyTable(node.options or node);
    if compactKind == nil and node.size ~= nil and options.size == nil then options.size = node.size; end
    local explicitWidth = GetExplicitDimension(options, "x");
    local explicitHeight = GetExplicitDimension(options, "y");

    if compactKind == "Text" or nodeType == "Text" then
        return CacheMeasuredSize(cache, node, MeasureText(node.text or "", options));
    end

    if compactKind == "Button" or nodeType == "Button" then
        local width = explicitWidth or DEFAULT_BUTTON_SIZE.x;
        local height = explicitHeight or DEFAULT_BUTTON_SIZE.y;
        return CacheMeasuredSize(cache, node, vec.New(width, height));
    end

    if nodeType ~= "VerticalLayout" and nodeType ~= "HorizontalLayout" and compactKind ~= "Panel" and nodeType ~= "Panel" then
        return CacheMeasuredSize(cache, node, vec.New(explicitWidth or 0, explicitHeight or 0));
    end

    local children = node.children or { };
    local childWidth = 0;
    local childHeight = 0;
    local spacing = options.spacing or 0;
    local childCount = #children;

    -- Growing children contribute their natural/default size as a minimum;
    -- NormalizeChildLayoutOpts still gives flex ownership of the final main-axis allocation.
    if nodeType == "VerticalLayout" then
        for _, child in children do
            local childSize = MeasureNode(child, cache);
            childWidth = math.max(childWidth, childSize.x);
            childHeight += childSize.y;
        end
        if childCount > 1 then childHeight += spacing * (childCount - 1); end
    elseif nodeType == "HorizontalLayout" then
        for _, child in children do
            local childSize = MeasureNode(child, cache);
            childWidth += childSize.x;
            childHeight = math.max(childHeight, childSize.y);
        end
        if childCount > 1 then childWidth += spacing * (childCount - 1); end
    else
        -- Panels do not position children, so their natural size cannot be
        -- inferred from child bounds without inventing a second layout policy.
        return CacheMeasuredSize(cache, node, vec.New(explicitWidth or 0, explicitHeight or 0));
    end

    local naturalWidth = childWidth + GetPadding(options, "Left") + GetPadding(options, "Right");
    local naturalHeight = childHeight + GetPadding(options, "Top") + GetPadding(options, "Bottom");
    return CacheMeasuredSize(cache, node, vec.New(explicitWidth or naturalWidth, explicitHeight or naturalHeight));
end

local function NormalizeChildLayoutOpts(options : { [string] : any }, parentLayoutAxis : string?, naturalSize : vec?, isText : boolean, isSelfSizingLayout : boolean) : { [string] : any }?
    if parentLayoutAxis == nil and options.layoutOpts == nil then return nil; end

    local layoutOpts = CopyTable(options.layoutOpts);
    if parentLayoutAxis == nil then return layoutOpts; end

    local mainAxis = parentLayoutAxis;
    local crossAxis = if mainAxis == "x" then "y" else "x";
    local mainModeKey = `mode{string.upper(mainAxis)}`;
    local crossModeKey = `mode{string.upper(crossAxis)}`;
    local mainSizeKey = `size{string.upper(mainAxis)}`;
    local crossSizeKey = `size{string.upper(crossAxis)}`;
    local mainWeightKey = `weight{string.upper(mainAxis)}`;
    local mainMinKey = `min{string.upper(mainAxis)}`;

    -- Explicit layoutOpts fields win over semantic sizing and inferred fill/grow.
    local mainDimension = GetDimension(options, mainAxis);
    local crossDimension = GetDimension(options, crossAxis);
    if layoutOpts[mainSizeKey] == nil and mainDimension ~= nil then layoutOpts[mainSizeKey] = mainDimension; end
    if layoutOpts[crossSizeKey] == nil and crossDimension ~= nil then layoutOpts[crossSizeKey] = crossDimension; end

    -- Natural size is only inferred when the raw child options leave the main
    -- axis open. A raw mode/size remains the escape hatch, while a growing child
    -- keeps a zero flex basis so it consumes the parent's remaining space.
    local hasRawMainOverride = layoutOpts[mainModeKey] ~= nil or layoutOpts[mainSizeKey] ~= nil;
    -- Let text and self-sizing nested layouts with omitted main dimensions stay
    -- intrinsic so dynamic content can resize a shrink-sized parent.
    local inferNaturalMain = not ((isText or isSelfSizingLayout) and mainDimension == nil);
    if layoutOpts[mainSizeKey] == nil and mainDimension == nil and options.grow == nil and not hasRawMainOverride and naturalSize ~= nil and inferNaturalMain then
        layoutOpts[mainSizeKey] = if mainAxis == "x" then naturalSize.x else naturalSize.y;
    end

    if options.grow ~= nil and layoutOpts[mainModeKey] == nil then
        layoutOpts[mainModeKey] = "flex";
        if layoutOpts[mainWeightKey] == nil then layoutOpts[mainWeightKey] = options.grow; end
        -- Flex still owns the final allocation, but content needs a measured
        -- minimum so a tight parent cannot clip a naturally-sized child.
        if layoutOpts[mainMinKey] == nil and naturalSize ~= nil then
            layoutOpts[mainMinKey] = if mainAxis == "x" then naturalSize.x else naturalSize.y;
        end
    end

    -- A nested linear layout with an omitted axis sizes itself on that axis.
    -- Keep the parent slot intrinsic/fixed so parent flex cannot fight the
    -- child's shrink pass; explicit dimensions, modes, grow, and fill remain authoritative.
    if isSelfSizingLayout and mainDimension == nil and options.grow == nil and layoutOpts[mainModeKey] == nil and layoutOpts[mainSizeKey] == nil then
        layoutOpts[mainModeKey] = "fixed";
    end

    if isSelfSizingLayout and crossDimension == nil and options.fill ~= true and layoutOpts[crossModeKey] == nil and layoutOpts[crossSizeKey] == nil then
        layoutOpts[crossModeKey] = "fixed";
    elseif layoutOpts[crossModeKey] == nil then
        layoutOpts[crossModeKey] = if crossDimension ~= nil or options.fill == false then "fixed" else "flex";
    end

    if isText then
        -- Generic slot alignment: LinearLayout knows only that this child should
        -- be positioned inside its allocated slot; it has no Declarative/Text coupling.
        if layoutOpts.slotInset == nil then layoutOpts.slotInset = ResolveTextInset(options); end
        if layoutOpts.slotAlignH == nil then layoutOpts.slotAlignH = ResolveHorizontalTextAlign(options.align); end
        if layoutOpts.slotAlignV == nil then layoutOpts.slotAlignV = ResolveVerticalTextAlign(options.verticalAlign); end
    end

    return layoutOpts;
end

local function NormalizeLayoutOptions(options : { [string] : any }, axis : string) : { [string] : any }
    local layoutOptions = { };
    for key in LAYOUT_OPTION_KEYS do
        if options[key] ~= nil then layoutOptions[key] = options[key]; end
    end

    -- Each omitted container axis is content-sized independently. An explicit
    -- full size or per-axis dimension retains LinearLayout's fixed default.
    if options.size == nil then
        if layoutOptions.widthMode == nil and options.width == nil then layoutOptions.widthMode = "shrink"; end
        if layoutOptions.heightMode == nil and options.height == nil then layoutOptions.heightMode = "shrink"; end
    end

    if layoutOptions.panelTemplate == nil and options.template ~= nil then layoutOptions.panelTemplate = options.template; end
    if layoutOptions.defaultChildOpts == nil then
        layoutOptions.defaultChildOpts = if axis == "y" then { modeX = "flex" } else { modeY = "flex" };
    end
    return layoutOptions;
end

local function NormalizeGridOptions(options : { [string] : any }) : { [string] : any }
    local gridOptions = { };
    for key in GRID_OPTION_KEYS do
        if options[key] ~= nil then gridOptions[key] = options[key]; end
    end

    if gridOptions.panelTemplate == nil and options.template ~= nil then gridOptions.panelTemplate = options.template; end
    return gridOptions;
end

local function NormalizeButtonOptions(options : { [string] : any }, text : string) : { [string] : any }
    local buttonOptions = { };
    for key, value in options do
        if not DECLARATIVE_RESERVED_OPTION_KEYS[key] then buttonOptions[key] = value; end
    end

    buttonOptions.text = text;
    if buttonOptions.panelTemplate == nil and options.template ~= nil then buttonOptions.panelTemplate = options.template; end
    return buttonOptions;
end

local function NormalizeCompactNode(compactNode : Node, parent : any, parentLayoutAxis : string?, measureCache : { [any] : vec }) : Node
    local kind = compactNode._compactKind;
    if kind == nil then return compactNode; end

    local options = CopyTable(compactNode.options);
    local node : Node =
    {
        type = if kind == "Vertical" then "VerticalLayout" elseif kind == "Horizontal" then "HorizontalLayout" elseif kind == "Grid" then "GridLayout" else kind,
        id = options.id,
        position = options.position or options.offset or vec.New(0, 0),
        layer = options.layer or 0i,
        children = compactNode.children,
        enabled = options.enabled,
        visible = options.visible,
        interactable = options.interactable,
        focusable = options.focusable,
        fontSize = options.fontSize,
        color = options.color,
        alpha = options.alpha,
        clipChildren = options.clipChildren,
        borderColor = options.borderColor,
        borderSize = options.borderSize,
        wrapWidth = options.wrapWidth,
        setup = options.setup,
        factory = options.factory,
        events = options.events,
    };

    local naturalSize = MeasureNode(compactNode, measureCache);
    local isSelfSizingLayout = kind == "Vertical" or kind == "Horizontal";
    node.layoutOpts = NormalizeChildLayoutOpts(options, parentLayoutAxis, naturalSize, kind == "Text", isSelfSizingLayout);

    if kind == "Component" then
        node.size = options.size or vec.New(options.width, options.height);
    elseif kind == "Button" then
        node.size = ResolveSize(options, parent, parentLayoutAxis, DEFAULT_BUTTON_SIZE, naturalSize);
    elseif kind == "Panel" or kind == "Grid" or kind == "Vertical" or kind == "Horizontal" then
        node.size = ResolveSize(options, parent, parentLayoutAxis, nil, naturalSize);
    end

    -- Explicit raw anchors and relative points are independent.
    if options.anchor ~= nil then
        node.anchor = options.anchor;
        node.relativePoint = options.relativePoint;
    elseif options.relativePoint ~= nil then
        node.relativePoint = options.relativePoint;
    end

    if kind == "Text" then
        node.textOverflow = ResolveTextOverflow(options);
        node.textWidth = GetExplicitDimension(options, "x") or options.wrapWidth;
        node.textHeight = GetExplicitDimension(options, "y");
        if node.textOverflow == "Wrap" and node.textWidth ~= nil then node.wrapWidth = node.textWidth; end
        node.text = compactNode.text;
        node.template = options.template or "DefaultButtonText";
    elseif kind == "Button" then
        node.options = NormalizeButtonOptions(options, compactNode.text or "");

        if options.onClick ~= nil then
            local events = CopyTable(options.events);
            if events.mouseUp == nil then events.mouseUp = options.onClick; end
            node.events = events;
        end
    elseif kind == "Vertical" then
        node.options = NormalizeLayoutOptions(options, "y");
    elseif kind == "Horizontal" then
        node.options = NormalizeLayoutOptions(options, "x");
    elseif kind == "Grid" then
        node.options = NormalizeGridOptions(options);
    elseif kind == "Component" then
        -- Component factories receive only the explicit construction contract.
    elseif kind ~= "Panel" then
        error(`Declarative: unknown compact node kind '{kind}'`, 3);
    end

    if kind == "Panel" then node.template = options.template; end
    return node;
end

function declarativeAPI.Panel(options : Options?, ...)
    return MakeCompactNode("Panel", options, { ... });
end

function declarativeAPI.Grid(options : Options?, ...)
    return MakeCompactNode("Grid", options, { ... });
end

function declarativeAPI.Component(options : Options)
    return MakeCompactNode("Component", options, { });
end

function declarativeAPI.Vertical(options : Options?, ...)
    return MakeCompactNode("Vertical", options, { ... });
end

function declarativeAPI.Horizontal(options : Options?, ...)
    return MakeCompactNode("Horizontal", options, { ... });
end

function declarativeAPI.Text(text : string, options : Options?)
    return { _compactKind = "Text", text = text, options = options or { }, children = { } } :: any;
end

function declarativeAPI.Button(text : string, options : Options?)
    return { _compactKind = "Button", text = text, options = options or { }, children = { } } :: any;
end

local function RequireField(node : Node, nodeType : string, fieldName : string) : any
    local value = node[fieldName]
    if value == nil then error(`Declarative.Build: {nodeType} requires '{fieldName}'`, 3); end
    return value;
end

local function IsWidgetLike(value : any) : boolean
    local valueType = type(value);
    if valueType ~= "userdata" and valueType ~= "table" then return false; end
    return type(value.SetPos) == "function"
        and type(value.SetAnchor) == "function"
        and type(value.SetRelativePoint) == "function";
end

local function ResolveComponentRoot(instance : any) : any
    if instance == nil then error("Declarative.Build: Component factory returned nil", 3); end
    if type(instance) == "table" and rawget(instance, "root") ~= nil then
        local root = rawget(instance, "root");
        if not IsWidgetLike(root) then error("Declarative.Build: Component '.root' must be widget-like", 3); end
        return root;
    end
    if not IsWidgetLike(instance) then
        error("Declarative.Build: Component factory must return a widget-like object or compound table with '.root'", 3);
    end
    return instance;
end

local function ResolveDestroyWidget(value : any) : any
    local current = value;
    for _ = 1, 8 do
        if type(current) ~= "table" then return current; end
        local raw = rawget(current, "_raw");
        if raw ~= nil then
            current = raw;
            continue;
        end
        local root = rawget(current, "root");
        if root == nil then return current; end
        current = root;
    end
    error("Declarative.View: could not resolve the root widget for destruction", 3);
end

local function ResolveLayoutOwner(value : any) : any?
    if type(value) ~= "table" then return nil; end
    if type(value.Remove) ~= "function" or type(value.GetChildIndex) ~= "function" then return nil; end
    return value;
end

local function ConstructPanel(parent : any, node : Node) : any
    return parent:NewPanel(node.position or vec.New(0, 0), RequireField(node, "Panel", "size"), node.layer or 0i, node.template);
end

local function CreateTruncateTextPolicy(root : any, node : Node, explicitWidth : number?) : any
    if explicitWidth == nil then
        error("Declarative.Text: Truncate requires an explicit content width", 3);
    end

    local self =
    {
        root = root,
        _template = RequireField(node, "Text", "template"),
        _rawText = node.text or "",
        _explicitWidth = explicitWidth,
        _fontSize = node.fontSize,
        _ready = false,
    } :: any;

    function self:_ResolveWidth() : number?
        return self._explicitWidth;
    end

    function self:RefreshTextPolicy()
        local width = self:_ResolveWidth();
        if width == nil or width <= 0 then
            error("Declarative.Text: Truncate requires a positive explicit content width", 3);
        end
        self.root:SetText(TextUtil.TruncateToWidth(self._rawText, self._template, width, self._fontSize));
    end

    function self:SetText(value : string)
        self._rawText = value;
        if self._ready then self:RefreshTextPolicy(); else self.root:SetText(value); end
    end

    function self:SetFontSize(value : number)
        self._fontSize = value;
        self.root:SetFontSize(value);
        if self._ready then self:RefreshTextPolicy(); end
    end

    function self:SetWrapWidth(value : number)
        if type(value) ~= "number" or value <= 0 then
            error("Declarative.Text: Truncate content width must be positive", 2);
        end
        self._explicitWidth = value;
        if self._ready then self:RefreshTextPolicy(); end
    end

    function self:GetRawText() : string return self._rawText; end
    function self:EnableTextPolicy()
        self._ready = true;
        self:RefreshTextPolicy();
    end

    RootedWidget.Attach(self);
    return self;
end

local function GetTextPrefix(text : string, characterCount : number) : string
    local endByte = utf8.offset(text, characterCount + 1);
    return if endByte == nil then text else string.sub(text, 1, endByte - 1);
end

local function CreateWrapTextPolicy(root : any, node : Node, explicitWidth : number, explicitHeight : number) : any
    if explicitWidth == nil then
        error("Declarative.Text: Wrap requires an explicit content width", 3);
    end
    if explicitHeight <= 0 then
        error("Declarative.Text: Wrap height must be positive when specified", 3);
    end

    local self =
    {
        root = root,
        _rawText = node.text or "",
        _explicitWidth = explicitWidth,
        _explicitHeight = explicitHeight,
        _ready = false,
    } :: any;

    function self:_Fits(value : string) : boolean
        self.root:SetText(value);
        return self.root:GetHeight() <= self._explicitHeight;
    end

    function self:_TruncateToHeight()
        if self:_Fits("") then
            local characterCount = utf8.len(self._rawText) or #self._rawText;
            local low = 0;
            local high = characterCount;
            local suffix = "...";

            if not self:_Fits(suffix) then
                self.root:SetText("");
                return;
            end

            while low < high do
                local candidateLength = math.ceil((low + high) / 2);
                if self:_Fits(GetTextPrefix(self._rawText, candidateLength) .. suffix) then
                    low = candidateLength;
                else
                    high = candidateLength - 1;
                end
            end

            self.root:SetText(GetTextPrefix(self._rawText, low) .. suffix);
        else
            self.root:SetText("");
        end
    end

    function self:RefreshTextPolicy()
        self.root:SetWrapWidth(self._explicitWidth);
        self.root:SetText(self._rawText);
        if self.root:GetHeight() > self._explicitHeight then self:_TruncateToHeight(); end
    end

    function self:SetText(value : string)
        self._rawText = value;
        if self._ready then self:RefreshTextPolicy(); else self.root:SetText(value); end
    end

    function self:SetFontSize(value : number)
        self.root:SetFontSize(value);
        if self._ready then self:RefreshTextPolicy(); end
    end

    function self:SetWrapWidth(value : number)
        if type(value) ~= "number" or value <= 0 then
            error("Declarative.Text: Wrap content width must be positive", 2);
        end
        self._explicitWidth = value;
        if self._ready then self:RefreshTextPolicy(); end
    end

    function self:GetRawText() : string return self._rawText; end
    function self:EnableTextPolicy()
        self._ready = true;
        self:RefreshTextPolicy();
    end

    RootedWidget.Attach(self);
    return self;
end

local function CreateUnboundedWrapTextPolicy(root : any, explicitWidth : number?) : any
    if explicitWidth == nil then
        error("Declarative.Text: Wrap requires an explicit content width", 3);
    end

    local policy =
    {
        _root = root,
        _explicitWidth = explicitWidth,
    } :: any;

    function policy:EnableTextPolicy()
        self._root:SetWrapWidth(self._explicitWidth);
    end

    return policy;
end

local function ConstructText(parent : any, node : Node) : any
    local mode = node.textOverflow or ResolveTextOverflow(node);
    local explicitWidth = node.textWidth or GetExplicitDimension(node, "x") or node.wrapWidth;
    local explicitHeight = node.textHeight;
    local template = RequireField(node, "Text", "template");
    if mode == "Visible" then
        return parent:NewText(node.text or "", node.position or vec.New(0, 0), node.layer or 0i, template);
    end

    if mode == "Wrap" then
        local root = parent:NewText(if explicitHeight == nil then node.text or "" else "", node.position or vec.New(0, 0), node.layer or 0i, template);
        local policy = if explicitHeight == nil
            then CreateUnboundedWrapTextPolicy(root, explicitWidth)
            else CreateWrapTextPolicy(root, node, explicitWidth, explicitHeight);
        if explicitHeight == nil then return root, nil, policy; end
        return policy, policy;
    end

    local root = parent:NewText("", node.position or vec.New(0, 0), node.layer or 0i, template);
    local policy = CreateTruncateTextPolicy(root, node, explicitWidth);
    return policy, policy;
end

local function ConstructButton(parent : any, node : Node) : any
    return UIButton.New(parent, node.position or vec.New(0, 0), RequireField(node, "Button", "size"), node.layer or 0i, node.options);
end

local function ConstructVerticalLayout(parent : any, node : Node) : any
    return UIVerticalLayout.New(parent, node.position or vec.New(0, 0), RequireField(node, "VerticalLayout", "size"), node.layer or 0i, node.options);
end

local function ConstructHorizontalLayout(parent : any, node : Node) : any
    return UIHorizontalLayout.New(parent, node.position or vec.New(0, 0), RequireField(node, "HorizontalLayout", "size"), node.layer or 0i, node.options);
end

local function ConstructGridLayout(parent : any, node : Node) : any
    return UIGridLayout.New(parent, node.position or vec.New(0, 0), RequireField(node, "GridLayout", "size"), node.layer or 0i, node.options);
end

local function ConstructComponent(parent : any, node : Node) : (any, any)
    local size = node.size or vec.New(node.width, node.height);
    local instance = node.factory(parent, node.position or vec.New(0, 0), size, node.layer or 0i);
    return instance, ResolveComponentRoot(instance);
end

local CONSTRUCTORS =
{
    Panel = ConstructPanel,
    Text = ConstructText,
    Button = ConstructButton,
    VerticalLayout = ConstructVerticalLayout,
    HorizontalLayout = ConstructHorizontalLayout,
    GridLayout = ConstructGridLayout,
    Component = ConstructComponent,
};

local VALID_COMPACT_KINDS =
{
    Panel = true,
    Component = true,
    Text = true,
    Button = true,
    Vertical = true,
    Horizontal = true,
    Grid = true,
};

local function ValidateEvents(events : any)
    if events == nil then return; end
    if type(events) ~= "table" then error("Declarative.Build: events must be a table", 3); end
    for eventName, callback in events do
        if EVENT_SETTERS[eventName] == nil then error(`Declarative.Build: unsupported event '{eventName}'`, 3); end
        if type(callback) ~= "function" then error(`Declarative.Build: event '{eventName}' callback must be a function`, 3); end
    end
end

local function ValidateNode(node : any, ids : { [string] : boolean }, visiting : { [any] : boolean })
    if type(node) ~= "table" then error("Declarative.Build: each node must be a table", 3); end
    if visiting[node] then error("Declarative.Build: descriptor tree contains a cycle", 3); end
    visiting[node] = true;

    local compactKind = rawget(node, "_compactKind");
    local options = node;
    if compactKind ~= nil then
        if not VALID_COMPACT_KINDS[compactKind] then error(`Declarative.Build: unknown compact node kind '{compactKind}'`, 3); end
        options = rawget(node, "options");
        if options == nil then options = { }; end
        if type(options) ~= "table" then error("Declarative.Build: options must be a table", 3); end
    else
        if type(node.type) ~= "string" then error("Declarative.Build: each raw node requires a string 'type'", 3); end
        if CONSTRUCTORS[node.type] == nil then error(`Declarative.Build: unknown node type '{node.type}'`, 3); end
    end

    local nodeType = if compactKind == "Vertical" then "VerticalLayout"
        elseif compactKind == "Horizontal" then "HorizontalLayout"
        elseif compactKind ~= nil then compactKind
        else node.type;
    if nodeType == "Component" then ValidateComponentOptions(options, "Component"); end
    if nodeType == "Text" then
        ValidateTextOptions(options);
    elseif options.overflow ~= nil then
        error("Declarative.Build: 'overflow' is supported only by Text nodes", 3);
    end

    local id = options.id;
    if id ~= nil then
        if type(id) ~= "string" then error("Declarative.Build: id must be a string", 3); end
        if ids[id] then error(`Declarative.Build: duplicate id '{id}'`, 3); end
        ids[id] = true;
    end

    ValidateEvents(options.events);
    if options.setup ~= nil and type(options.setup) ~= "function" then error("Declarative.Build: setup must be a function", 3); end
    if options.onClick ~= nil and type(options.onClick) ~= "function" then error("Declarative.Build: onClick must be a function", 3); end

    local children = node.children;
    if children ~= nil then
        if type(children) ~= "table" then error("Declarative.Build: children must be a table", 3); end
        if nodeType == "Component" and next(children) ~= nil then
            error("Declarative.Build: Component does not support children", 3);
        end
        for _, child in children do ValidateNode(child, ids, visiting); end
    end

    visiting[node] = nil;
end

local function ApplyOptionalProperty(object : any, setterName : string, value : any)
    if value == nil then return; end
    local setter = object[setterName];
    if type(setter) == "function" then setter(object, value); end
end

local function ApplyCommonProperties(object : any, node : Node)
    if node.anchor ~= nil then object:SetAnchor(node.anchor); end
    if node.relativePoint ~= nil then object:SetRelativePoint(node.relativePoint); end
    if node.enabled ~= nil then object:SetEnabled(node.enabled); end
    if node.visible ~= nil then object:SetVisible(node.visible); end
    if node.interactable ~= nil then object:SetInteractable(node.interactable); end
    if node.focusable ~= nil then object:SetFocusable(node.focusable); end
    ApplyOptionalProperty(object, "SetFontSize", node.fontSize);
    ApplyOptionalProperty(object, "SetColor", node.color);
    ApplyOptionalProperty(object, "SetAlpha", node.alpha);
    ApplyOptionalProperty(object, "SetClipChildren", node.clipChildren);
    ApplyOptionalProperty(object, "SetBorderColor", node.borderColor);
    ApplyOptionalProperty(object, "SetBorderSize", node.borderSize);
    if node.textOverflow == "Wrap" then ApplyOptionalProperty(object, "SetWrapWidth", node.wrapWidth); end
end

local function ApplyEvents(object : any, events : any)
    if events == nil then return; end
    if type(events) ~= "table" then error("Declarative.Build: events must be a table", 3); end

    for eventName, callback in events do
        local setter = EVENT_SETTERS[eventName]
        if setter == nil then error(`Declarative.Build: unsupported event '{eventName}'`, 3); end
        object[setter](object, callback);
    end
end

local function GetLayoutAxis(nodeType : string) : string?
    if nodeType == "VerticalLayout" then return "y"; end
    if nodeType == "HorizontalLayout" then return "x"; end
    return nil;
end

local function BuildNode(parent : any, node : any, refs : { [string] : any }, parentLayoutAxis : string?, measureCache : { [any] : vec }, textPolicies : { any }, layoutObjects : { any }, layoutOwners : { any }) : any
    if type(node) ~= "table" then error("Declarative.Build: each node must be a table", 3); end

    parentLayoutAxis = parentLayoutAxis or GetParentLayoutAxis(parent);
    node = NormalizeCompactNode(node, parent, parentLayoutAxis, measureCache);

    local nodeType = node.type
    if type(nodeType) ~= "string" then error("Declarative.Build: each node requires a string 'type'", 3); end
    if nodeType == "Text" then
        if node.textOverflow == nil then node.textOverflow = ResolveTextOverflow(node); end
        if node.textHeight == nil then node.textHeight = GetExplicitDimension(node, "y"); end
    end

    local constructor = CONSTRUCTORS[nodeType]
    if constructor == nil then error(`Declarative.Build: unknown node type '{nodeType}'`, 3); end

    local object, resolvedRoot, textPolicy = constructor(parent, node)
    resolvedRoot = resolvedRoot or object
    local parentLayout = ResolveLayoutOwner(parent);
    if parentLayout ~= nil then
        table.insert(layoutOwners, { layout = parentLayout, child = resolvedRoot });
    end
    local id = node.id
    if id ~= nil then
        if type(id) ~= "string" then error(`Declarative.Build: {nodeType} id must be a string`, 3); end
        if refs[id] ~= nil then error(`Declarative.Build: duplicate id '{id}'`, 3); end
        refs[id] = object
    end

    ApplyCommonProperties(resolvedRoot, node)
    ApplyEvents(resolvedRoot, node.events)

    if node.layoutOpts ~= nil and type(parent.SetChildOpts) == "function" then
        parent:SetChildOpts(resolvedRoot, node.layoutOpts)
    end

    if textPolicy ~= nil then
        table.insert(textPolicies, textPolicy);
    elseif type(object.EnableTextPolicy) == "function" then
        table.insert(textPolicies, object);
    end
    if nodeType == "VerticalLayout" or nodeType == "HorizontalLayout" or nodeType == "GridLayout" then
        table.insert(layoutObjects, resolvedRoot);
    end

    if node.setup ~= nil then
        if type(node.setup) ~= "function" then error(`Declarative.Build: {nodeType}.setup must be a function`, 3); end
        node.setup(object);
    end

    if node.children ~= nil then
        if type(node.children) ~= "table" then error(`Declarative.Build: {nodeType}.children must be a table`, 3); end
        for _, child in node.children do
            BuildNode(resolvedRoot, child, refs, GetLayoutAxis(nodeType), measureCache, textPolicies, layoutObjects, layoutOwners);
        end
    end

    return resolvedRoot;
end

function declarativeAPI.Build(parent : any, description : Node) : View
    -- Validate deterministic descriptor errors before allocating any UI objects.
    ValidateNode(description, { }, { });

    local refs : { [string] : any } = { };
    local measureCache : { [any] : vec } = { };
    local textPolicies : { any } = { };
    local layoutObjects : { any } = { };
    local layoutOwners : { any } = { };
    local root = BuildNode(parent, description, refs, nil, measureCache, textPolicies, layoutObjects, layoutOwners);
    for _, text in textPolicies do text:EnableTextPolicy(); end
    -- Keep inner layouts ahead of their parents after native text policies have
    -- updated intrinsic wrapped sizes.
    for index = #layoutObjects, 1, -1 do layoutObjects[index]:Refresh(); end
    local view = {
        root = root,
        refs = refs,
    } :: any;

    function view:Get(id : string) return self.refs[id]; end

    function view:Destroy()
        if self._destroyed then return; end
        self._destroyed = true;

        local oldRoot = self.root;
        self.root = nil;
        table.clear(self.refs);

        for index = #layoutOwners, 1, -1 do
            local owner = layoutOwners[index];
            owner.layout:Remove(owner.child);
        end
        table.clear(layoutOwners);

        if oldRoot ~= nil then
            UI.DestroyWidget(ResolveDestroyWidget(oldRoot));
        end
    end

    return view :: View;
end

return declarativeAPI;
