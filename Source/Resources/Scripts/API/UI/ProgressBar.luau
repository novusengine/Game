local RootedWidget = require("@src/API/UI/RootedWidget")

export type Options =
{
    backgroundTemplate : string?,
    fillTemplate : string?,
    inset : vec?,
}

export type ProgressBar =
{
    root : Widget,
    background : Panel,
    fill : Panel,
    progress : number,
    fillWidth : number,

    GetProgress : (self : ProgressBar) -> number,
    SetProgress : (self : ProgressBar, progress : number) -> (),
}

export type ProgressBarAPI =
{
    New : (parent : Widget, position : vec, size : vec, layer : integer, options : Options?) -> ProgressBar,
}

local progressBarAPI : ProgressBarAPI = { }

function progressBarAPI.New(parent : Widget, position : vec, size : vec, layer : integer, options : Options?) : ProgressBar
    local opts = options or { }
    local inset = opts.inset or vec.New(4, 4)
    local fillWidth = math.max(0, size.x - inset.x * 2)
    local fillHeight = math.max(0, size.y - inset.y * 2)
    local root = parent:NewWidget(position, layer)
    local background = root:NewPanel(vec.New(0, 0), size, 0i, opts.backgroundTemplate or "DefaultSliderBackground")
    local fill = background:NewPanel(vec.New(inset.x, inset.y), vec.New(fillWidth, fillHeight), 1i, opts.fillTemplate or "DefaultSliderFill")
    local self : ProgressBar = {
        root = root,
        background = background,
        fill = fill,
        progress = 0,
        fillWidth = fillWidth,
    } :: any
    RootedWidget.Attach(self)

    function self:GetProgress()
        return self.progress
    end

    function self:SetProgress(progress : number)
        self.progress = math.clamp(progress, 0.0, 1.0)
        self.fill:SetWidth(self.fillWidth * self.progress)
    end

    self:SetProgress(0)
    return self
end

return progressBarAPI
