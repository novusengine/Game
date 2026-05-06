local UIAtlasPanel : AtlasPanelAPI = require("@src/API/UI/AtlasPanel")
local UIButton = require("@src/API/UI/Button")
local UIComboBox = require("@src/API/UI/ComboBox")
local UIInputBox = require("@src/API/UI/InputBox")
local UIScrollBox : ScrollBoxAPI = require("@src/API/UI/ScrollBox")
local UIStack : StackAPI = require("@src/API/UI/UIStack")
local UIVerticalLayout = require("@src/API/UI/VerticalLayout")
local UIHorizontalLayout = require("@src/API/UI/HorizontalLayout")
local UIGridLayout = require("@src/API/UI/GridLayout")

type ClassInfo =
{
    name : string,
    description : string,
    iconAtlasTexture : string,
    iconAtlasTextureNumTexturesX : number,
    iconAtlasTextureNumTexturesY : number,
    iconAtlasIndex : number,
}
type ClassInfos = { ClassInfo }

type Race =
{
    name : string,
    description : string,
    genderIndexToRaceAtlasIndex : { number }
}

type FactionInfo =
{
    name : string,
    factionAtlasTexture : string,
    factionAtlasTextureNumTexturesX : number,
    factionAtlasTextureNumTexturesY : number,
    factionAtlasIndex : number,
    factionDescription : string,

    races : { Race },
    raceAtlasTexture : string,
    raceAtlasTextureNumTexturesX : number,
    raceAtlasTextureNumTexturesY : number,
}
type FactionInfos = { FactionInfo }

type AppearanceOption =
{
    name : string,
    options : { string }
}
type AppearanceOptions = { AppearanceOption }

type CharacterCreationScreen = 
{
    widget : Widget,
    optionsPanel : Panel,
    characterInfoPanel : Panel,
    nameInputBox : UIInputBox,
    selectedFactionIcon : UIAtlasPanel,
    selectedRaceIcon : UIAtlasPanel,
    selectedGenderIcon : UIAtlasPanel,
    selectedClassIcon : UIAtlasPanel,
    selectedAppearanceIcon : UIAtlasPanel,
    selectedAppearanceOptionIcon : UIAtlasPanel,
    descriptionScrollBox : UIScrollBox,
    buttons : 
    {
        raceButtons : { UIAtlasPanel },
        classButtons : { UIAtlasPanel },

    }
}

-- This data is example data, this will be dynamic and there could be more or less classes
local classInfos : ClassInfos = {
    {
        name = "Warrior",
        description = "Warriors are plate-wearing fighters who strive for perfection in armed combat. As warriors fight they generate rage, which is used to power special attacks.",

        iconAtlasTexture = "Data/Texture/interface/glues/charactercreate/ui-charactercreate-classes.dds",
        iconAtlasTextureNumTexturesX = 4,
        iconAtlasTextureNumTexturesY = 4,
        iconAtlasIndex = 1,
    },
    {
        name = "Paladin",
        description = "Paladins are heavily-armored fighters and defenders who use Holy magic to heal wounds and combat evil. They can focus on two-handed weapons, shields or healing.",

        iconAtlasTexture = "Data/Texture/interface/glues/charactercreate/ui-charactercreate-classes.dds",
        iconAtlasTextureNumTexturesX = 4,
        iconAtlasTextureNumTexturesY = 4,
        iconAtlasIndex = 9,
    },
    {
        name = "Hunter",
        description = "Hunters are at home in the wilderness and have a special affinity for beasts. They rely on their weaponry and their pet to deal damage.",

        iconAtlasTexture = "Data/Texture/interface/glues/charactercreate/ui-charactercreate-classes.dds",
        iconAtlasTextureNumTexturesX = 4,
        iconAtlasTextureNumTexturesY = 4,
        iconAtlasIndex = 5,
    },
    {
        name = "Rogue",
        description = "Rogues often serve as assassins or scouts, though many are lone wolves as well. Rogues can often sneak around enemies or attack an opponent from behind to try and finish them off quickly.",

        iconAtlasTexture = "Data/Texture/interface/glues/charactercreate/ui-charactercreate-classes.dds",
        iconAtlasTextureNumTexturesX = 4,
        iconAtlasTextureNumTexturesY = 4,
        iconAtlasIndex = 3,
    },
    {
        name = "Priest",
        description = "Priests are well-rounded healers with a variety of tools. However, they can also sacrifice their healing to deal damage with Shadow magic.",

        iconAtlasTexture = "Data/Texture/interface/glues/charactercreate/ui-charactercreate-classes.dds",
        iconAtlasTextureNumTexturesX = 4,
        iconAtlasTextureNumTexturesY = 4,
        iconAtlasIndex = 7,
    },
    {
        name = "Shaman",
        description = "Shaman use the power of the elements to enhance their weapon damage or spells. Shaman summon totems in combat, small objects that disable enemies, heal, or cause damage to enemies.",

        iconAtlasTexture = "Data/Texture/interface/glues/charactercreate/ui-charactercreate-classes.dds",
        iconAtlasTextureNumTexturesX = 4,
        iconAtlasTextureNumTexturesY = 4,
        iconAtlasIndex = 6,
    },
    {
        name = "Mage",
        description = "Mages are the iconic magic-users of Azeroth who learn their craft through intense research and study. They make up for their light armor with a potent array of offensive and defensive spells.",

        iconAtlasTexture = "Data/Texture/interface/glues/charactercreate/ui-charactercreate-classes.dds",
        iconAtlasTextureNumTexturesX = 4,
        iconAtlasTextureNumTexturesY = 4,
        iconAtlasIndex = 2,
    },
    {
        name = "Warlock",
        description = "Warlocks cast Fire or Shadow magic to damage, drain, or curse their enemy. They summon demons as servants.",

        iconAtlasTexture = "Data/Texture/interface/glues/charactercreate/ui-charactercreate-classes.dds",
        iconAtlasTextureNumTexturesX = 4,
        iconAtlasTextureNumTexturesY = 4,
        iconAtlasIndex = 8,
    },
    {
        name = "Druid",
        description = "Druids are shape-shifters with an affinity for the plant and animal kingdoms. Druids can specialize in healing, casting spells at range, or taking on the form of a cat or bear to fight in melee.",

        iconAtlasTexture = "Data/Texture/interface/glues/charactercreate/ui-charactercreate-classes.dds",
        iconAtlasTextureNumTexturesX = 4,
        iconAtlasTextureNumTexturesY = 4,
        iconAtlasIndex = 4,
    },
};

local factionInfos : FactionInfos = {
    {
        name = "Alliance",
        factionAtlasTexture = "Data/Texture/interface/glues/charactercreate/ui-charactercreate-factions.dds",
        factionAtlasTextureNumTexturesX = 2,
        factionAtlasTextureNumTexturesY = 1,
        factionAtlasIndex = 1,
        factionDescription = "The Alliance consists of five races: the noble humans, the adventurous dwarves, the enigmatic night elves, the ingenious gnomes, and the honorable draenei. Bound by a loathing for all things demonic, they fight to restore order in this war-torn world.",

        races = {
            {
                name = "Human",
                description = "Humans are a young race, and thus highly versatile, mastering the arts of combat, craftsmanship, and magic with stunning efficiency. The humans valor and optimism have led them to build some of the worlds greatest kingdoms. In this troubled era, after generations of conflict, humanity seeks to rekindle its former glory and forge a shining new future.",
                genderIndexToRaceAtlasIndex = { 1, 9 },
            },
            {
                name = "Dwarf",
                description = "In ages past, the dwarves cared only for riches taken from the earth's depths. Then records surfaced of a god-like race said to have given the dwarves life... and an enchanted birthright. Driven to learn more, the dwarves devoted themselves to the pursuit of lost artifacts and ancient knowledge. Today dwarven archaeologists are scattered throughout the globe.",
                genderIndexToRaceAtlasIndex = { 2, 10 },
            },
            {
                name = "Gnome",
                description = "Though small in stature, the clever gnomes of Khaz Modan have used their great intellect to secure a place in history. Their subterranean kingdom of Gnomeregan is a marvel of steam-driven technology, and their skill at both engineering and the arcane arts has been a tremendous benefit to the Alliance across numerous campaigns.",
                genderIndexToRaceAtlasIndex = { 3, 11 },
            },
            {
                name = "Night Elf",
                description = "Ten thousand years ago, the night elves founded a vast empire, but their reckless use of primal magic brought them to ruin. In grief, they withdrew to the forests and remained isolated there until the return of their ancient enemy, the Burning Legion. With no other choice, the night elves emerged at last from their seclusion to fight for their place in the new world.",
                genderIndexToRaceAtlasIndex = { 4, 12 },
            }
        },
        raceAtlasTexture = "Data/Texture/interface/glues/charactercreate/ui-charactercreate-races.dds",
        raceAtlasTextureNumTexturesX = 4,
        raceAtlasTextureNumTexturesY = 4,
    },
    {
        name = "Horde",
        factionAtlasTexture = "Data/Texture/interface/glues/charactercreate/ui-charactercreate-factions.dds",
        factionAtlasTextureNumTexturesX = 2,
        factionAtlasTextureNumTexturesY = 1,
        factionAtlasIndex = 2,
        factionDescription = "Five races comprise the Horde: the brutal orcs, the shadowy undead, the spiritual tauren, the quick-witted trolls, and the driven blood elves. Beset by enemies on all sides, these outcasts have forged a union they hope will ensure their mutual survival.",

        races = {
            {
                name = "Orc",
                description = "The orc race originated on the planet Draenor. A peaceful people with shamanic beliefs, they were enslaved by the Burning Legion and forced into war with the humans of Azeroth. Although it took many years, the orcs finally escaped the demons' corruption and won their freedom. To this day they fight for honor in an alien world that hates and reviles them.",
                genderIndexToRaceAtlasIndex = { 8, 16 },
            },
            {
                name = "Troll",
                description = "Once at home in the jungles of Stranglethorn Vale, the fierce trolls of the Darkspear tribe were besieged on all sides by warring factions. The orcish Horde came to their aid, convincing the Darkspear to sail across the Great Sea and settle in the untamed lands of Kalimdor. Though they cling to their shadowy heritage, the Darkspear remain vocal advocates of a united Horde, lending the strength of their arms and powerful tribal magics to the common cause.",
                genderIndexToRaceAtlasIndex = { 7, 15 },
            },
            {
                name = "Tauren",
                description = "Always the tauren strive to preserve the balance of nature and heed the will of their goddess, the Earth Mother. Many of the nomadic wandering tribes of tauren have gathered together under a single banner to settle in the fertile plains of Mulgore. Were it not for the timely intervention of the orcs, the whole race may have been wiped out by marauding centaur. The tauren honor their blood-debt to this day, fighting alongside the Horde to protect their land.",
                genderIndexToRaceAtlasIndex = { 5, 13 },
            },
            {
                name = "Undead",
                description = "Once mindless slaves to the terrible Lich King, the gruesome Forsaken have overthrown his rule and now pledge loyalty to the wicked banshee queen Sylvanas. Under her reign Forsaken armies have reclaimed the ruined kingdom of Lordaeron. Every day is a struggle against vengeful humans who wish to wipe them out, and a constant battle of wills against Horde allies who distrust the sinister motives of the undead.",
                genderIndexToRaceAtlasIndex = { 6, 14 },
            }
        },
        raceAtlasTexture = "Data/Texture/interface/glues/charactercreate/ui-charactercreate-races.dds",
        raceAtlasTextureNumTexturesX = 4,
        raceAtlasTextureNumTexturesY = 4,
    }
};

local appearanceOptions : AppearanceOptions = {
    {
        name = "Skin Color",
        options = {
            "Light",
            "Medium",
            "Dark",
        }
    },
    {
        name = "Face",
        options = {
            "Bob",
            "Steve",
            "Joe",
            "Burt"
        }
    },
    {
        name = "Hair Style",
        options = {
            "Short",
            "Medium",
            "Long",
            "AsmonBald"
        }
    },
    {
        name = "Hair Color",
        options = {
            "Black",
            "Brown",
            "Blonde",
            "Red"
        }
    },
    {
        name = "Piercings",
        options = {
            "None",
            "Ears",
            "Nose"
        }
    }
}

local function CreateFactionSelectPanel(characterCreationScreen : CharacterCreationScreen, parent : any, factionInfoIndex : number, columnWidth : number, panelHeight : number, maxNumRaces : number)
    local factionInfo : FactionInfo = factionInfos[factionInfoIndex];

    -- Square race icon: constrained by column width and a fraction of available height.
    -- Approximates the original gapY-spread; spacing is uniform here rather than auto-distributed.
    local s = math.floor(math.min(columnWidth, panelHeight / (maxNumRaces + 1)));
    local spacing = math.floor((panelHeight - maxNumRaces * s - 30) / math.max(1, maxNumRaces));

    local column = UIVerticalLayout.NewVerticalLayout(parent, 0, 0, columnWidth, panelHeight, 0, {
        panelTemplate = "DebugRed", alpha = 0.0,
        alignH = "center",
        spacing = spacing,
        paddingTop = 0,
    });

    column:NewText(factionInfo.name, 0, 0, 0, "YellowText");

    for i = 1, #factionInfo.races do
        local race = factionInfo.races[i];

        local raceButton : AtlasPanel = UIAtlasPanel.NewAtlasPanel(column, 0, 0, s, s, 0, {
            texture = factionInfo.raceAtlasTexture,
            numTexturesX = factionInfo.raceAtlasTextureNumTexturesX,
            numTexturesY = factionInfo.raceAtlasTextureNumTexturesY,
            atlasIndex = race.genderIndexToRaceAtlasIndex[1],
        });
        local raceIndex = #characterCreationScreen.buttons.raceButtons + 1;
        raceButton:SetOnMouseUp(function(eventID, widget, ...)
            characterCreationScreen:SetRace(raceIndex);
        end);
        table.insert(characterCreationScreen.buttons.raceButtons, raceButton);
        characterCreationScreen.raceIndexToFactionIndex[raceIndex] = factionInfoIndex;
        characterCreationScreen.raceIndexToFactionLocalRaceIndex[raceIndex] = i;
    end

    return column;
end

local function CreateRaceSelectPanel(characterCreationScreen : CharacterCreationScreen, parent : any, panelHeight : number)
    characterCreationScreen.buttons.raceButtons = {};
    characterCreationScreen.raceIndexToFactionIndex = {};
    characterCreationScreen.raceIndexToFactionLocalRaceIndex = {};

    local numFactions = #factionInfos;
    local maxNumRaces = 0;
    for i = 1, numFactions do
        local numRaces = #factionInfos[i].races;
        if numRaces > maxNumRaces then maxNumRaces = numRaces end
    end

    local optionsWidth = parent:GetWidth();
    local paddingX = 5;
    local factionSelectWidth = math.floor((optionsWidth - (numFactions + 1) * paddingX) / numFactions);

    -- Outer H layout splits the row into N equal-width columns with paddingX gutters.
    local row = UIHorizontalLayout.NewHorizontalLayout(parent, 0, 0, optionsWidth, panelHeight, 0, {
        panelTemplate = "DebugRed", alpha = 0.0,
        spacing = paddingX, paddingLeft = paddingX, paddingRight = paddingX,
        defaultChildOpts = { modeY = "flex" },  -- columns fill the row height
    });

    for i = 1, numFactions do
        local column = CreateFactionSelectPanel(characterCreationScreen, row, i, factionSelectWidth, panelHeight, maxNumRaces);
        row:SetChildOpts(column, { sizeX = factionSelectWidth, modeY = "flex" });
    end

    return row;
end

local function CreateGenderSelectPanel(characterCreationScreen : CharacterCreationScreen, parent : any, panelHeight : number)
    characterCreationScreen.buttons.genderButtons = {};

    local padding = 4;
    local iconSize = panelHeight - padding;

    -- H layout that shrinks horizontally to fit its 2 icons + spacing. The parent V layout
    -- centers it horizontally via alignH="center" set by the caller (CreateLeftPanel).
    -- H layout fills the full parent width; alignH="center" packs the icon pair into the
    -- middle of the row instead of starting at the left edge.
    local row = UIHorizontalLayout.NewHorizontalLayout(parent, 0, 0, parent:GetWidth(), panelHeight, 0, {
        panelTemplate = "DebugRed", alpha = 0.0,
        spacing = padding, alignV = "center", alignH = "center",
    });

    local maleButton : AtlasPanel = UIAtlasPanel.NewAtlasPanel(row, 0, 0, iconSize, iconSize, 0, {
        texture = "Data/Texture/interface/glues/charactercreate/ui-charactercreate-gender.dds",
        numTexturesX = 2, numTexturesY = 1,
    });
    maleButton:SetOnMouseUp(function(eventID, widget, ...)
        characterCreationScreen:SetGender(1);
    end);
    table.insert(characterCreationScreen.buttons.genderButtons, maleButton);

    local femaleButton : AtlasPanel = UIAtlasPanel.NewAtlasPanel(row, 0, 0, iconSize, iconSize, 0, {
        texture = "Data/Texture/interface/glues/charactercreate/ui-charactercreate-gender.dds",
        numTexturesX = 2, numTexturesY = 1, atlasIndex = 2,
    });
    femaleButton:SetOnMouseUp(function(eventID, widget, ...)
        characterCreationScreen:SetGender(2);
    end);
    table.insert(characterCreationScreen.buttons.genderButtons, femaleButton);

    return row;
end

local function CreateClassSelectPanel(characterCreationScreen : CharacterCreationScreen, parent : any, panelHeight : number, maxIconSize : number)
    characterCreationScreen.buttons.classButtons = {};

    -- Compute per-cell size from class count + maxIconSize so the grid still hits the target
    -- icon dimensions; GridLayout positions the cells with cellAlign="center".
    local numClasses = #classInfos;
    local padding = 5;
    local columns = math.ceil(math.sqrt(numClasses));
    local rows = math.ceil(numClasses / columns);
    local optionsWidth = parent:GetWidth();
    local availableWidthPerIcon = (optionsWidth - (columns - 1) * padding) / columns;
    local availableHeightPerIcon = (panelHeight - (rows - 1) * padding) / rows;
    local iconSize = math.min(math.floor(math.min(availableWidthPerIcon, availableHeightPerIcon)), maxIconSize);

    local grid = UIGridLayout.NewGridLayout(parent, 0, 0, optionsWidth, panelHeight, 0, {
        panelTemplate = "DebugRed", alpha = 0.0,
        columns = columns,
        cellSizeX = iconSize, cellSizeY = iconSize,
        spacing = padding,
        cellAlign = "center",
        gridAlignH = "center", gridAlignV = "center",
    });

    for i, classInfo in classInfos do
        local classButton : AtlasPanel = UIAtlasPanel.NewAtlasPanel(grid, 0, 0, iconSize, iconSize, 0, {
            texture = classInfo.iconAtlasTexture,
            numTexturesX = classInfo.iconAtlasTextureNumTexturesX,
            numTexturesY = classInfo.iconAtlasTextureNumTexturesY,
            atlasIndex = classInfo.iconAtlasIndex,
        });

        classButton:SetOnMouseUp(function(eventID, widget, ...)
            characterCreationScreen:SetClass(i);
        end);
        table.insert(characterCreationScreen.buttons.classButtons, classButton);
    end

    return grid;
end

local function CreateAppearanceSelectPanel(characterCreationScreen : CharacterCreationScreen, parent : any, panelHeight : number)
    local optionsWidth = parent:GetWidth();
    local padding = 5;
    local rowHeight = math.floor((panelHeight / #appearanceOptions) - padding);
    local comboBoxWidth = 200;

    local panel = UIVerticalLayout.NewVerticalLayout(parent, 0, 0, optionsWidth, panelHeight, 0, {
        panelTemplate = "DebugRed", alpha = 0.0,
        spacing = padding,
    });

    -- World position used to anchor the combobox dropdowns to the right of the options panel.
    -- Computed up-front because the combobox.selectionPanel positioning is independent of the row layout.
    local sidePanelPosX, sidePanelPosY = panel:GetWorldPos();
    sidePanelPosX += optionsWidth;
    sidePanelPosY += panelHeight;

    for i = 1, #appearanceOptions do
        local row = UIHorizontalLayout.NewHorizontalLayout(panel, 0, 0, optionsWidth, rowHeight, 0, {
            panelTemplate = "DebugRed", alpha = 0.0,
            alignV = "center",
            paddingLeft = 5, paddingRight = padding,
        });
        panel:SetChildOpts(row, { sizeY = rowHeight, modeX = "flex" });

        -- Order matters: create label, spacer (flex, fills remaining), then combobox at the right.
        local optionName : Text = row:NewText(appearanceOptions[i].name, 0, 0, 0, "YellowText");
        local spacer = row:NewWidget(0, 0, 0);
        local comboBox = UIComboBox.NewComboBox(row, 0, 0, comboBoxWidth, rowHeight, 0, {
            options = appearanceOptions[i].options,
            disableButtons = true,
        });

        row:SetChildOpts(spacer,   { modeX = "flex", weightX = 1 });
        row:SetChildOpts(comboBox, { sizeX = comboBoxWidth });

        comboBox.selectionPanel:SetWorldPos(sidePanelPosX, sidePanelPosY);
        comboBox.selectionPanel:SetAnchor(0.0, 0.0);
        comboBox.selectionPanel:SetRelativePoint(0.0, 1.0);
    end

    return panel;
end

local function CreateLeftPanel(characterCreationScreen : CharacterCreationScreen)
    local optionsWidth = 400;
    local optionsHeight = 980;
    local maxIconSize = 80;
    local spacing = 10;

    local raceSelectHeight   = 400;
    local genderSelectHeight = 80;
    local classSelectHeight  = 250;
    -- Appearance fills whatever's left after the three above + spacing gutters.
    local appearanceHeight = optionsHeight - raceSelectHeight - genderSelectHeight - classSelectHeight - spacing * 3;

    -- optionsPanel is a V layout: subpanels become its children stacked top-down.
    characterCreationScreen.optionsPanel = UIVerticalLayout.NewVerticalLayout(
        characterCreationScreen.widget, 20, -80, optionsWidth, optionsHeight, 0,
        { panelTemplate = "DialogBox", spacing = spacing });
    characterCreationScreen.optionsPanel:SetAnchor(0.0, 1.0);
    characterCreationScreen.optionsPanel:SetRelativePoint(0.0, 1.0);
    characterCreationScreen.optionsPanel:SetBorderColor(vector.create(0.1, 0.1, 0.1));
    characterCreationScreen.optionsPanel:SetBorderSize(2);

    local optionsPanel = characterCreationScreen.optionsPanel;

    local raceSelect = CreateRaceSelectPanel(characterCreationScreen, optionsPanel, raceSelectHeight);
    optionsPanel:SetChildOpts(raceSelect, { sizeY = raceSelectHeight, modeX = "flex" });

    local genderSelect = CreateGenderSelectPanel(characterCreationScreen, optionsPanel, genderSelectHeight);
    optionsPanel:SetChildOpts(genderSelect, { sizeY = genderSelectHeight, alignH = "center" });

    local classSelect = CreateClassSelectPanel(characterCreationScreen, optionsPanel, classSelectHeight, maxIconSize);
    optionsPanel:SetChildOpts(classSelect, { sizeY = classSelectHeight, modeX = "flex" });

    local appearance = CreateAppearanceSelectPanel(characterCreationScreen, optionsPanel, appearanceHeight);
    optionsPanel:SetChildOpts(appearance, { sizeY = appearanceHeight, modeX = "flex" });
end

local function CreateMiddlePanel(characterCreationScreen : CharacterCreationScreen)
    -- Name label
    local nameLabel : Text = characterCreationScreen.widget:NewText("Name", 0, 100, 0, "YellowText");
    nameLabel:SetAnchor(0.5, 0.0);
    nameLabel:SetRelativePoint(0.5, 0.0);

    -- Name input box
    characterCreationScreen.nameInputBox = UIInputBox.NewInputBox(characterCreationScreen.widget, 0, 55, 200, 40, 0, {
        defaultText = "",
    });
    characterCreationScreen.nameInputBox.inputBox:SetAnchor(0.5, 0.0);
    characterCreationScreen.nameInputBox.button:SetRelativePoint(0.5, 0.0);
    characterCreationScreen.nameInputBox.button.panel:SetBorderColor(vector.create(0.1, 0.1, 0.1));
    characterCreationScreen.nameInputBox.button.panel:SetBorderSize(2);
end

local function UpdateScrollBoxBasedOnContent(scrollBox : ScrollBox, description : Text)
    local text = description:GetRawText();

    local wrapWidth = scrollBox.width;
    local wrappedText, width, height = UI.WrapText(text, "DefaultButtonText", wrapWidth);

    local scrollBoxHeight = scrollBox.height;

    if (height > scrollBoxHeight) then
        wrapWidth = scrollBox.widthWithScrollBar;
        wrappedText, width, height = UI.WrapText(text, "DefaultButtonText", wrapWidth);
        scrollBox:SetVerticalScrollBarVisible(true);
    else
        scrollBox:SetVerticalScrollBarVisible(false);
    end

    scrollBox:SetContentHeight(height+5); -- 5 because padding looks better
    description:SetWrapWidth(wrapWidth);
end

local function CreateFactionInfoPanel(characterCreationScreen : CharacterCreationScreen, currentY : number, characterInfoWidth : number, factionInfoHeight : number)
    local factionInfo = factionInfos[1];
    
    local factionInfoPanel : Panel = characterCreationScreen.characterInfoPanel:NewPanel(0, currentY, characterInfoWidth, factionInfoHeight, 0, "DialogBox");
    factionInfoPanel:SetAnchor(1.0, 1.0);
    factionInfoPanel:SetRelativePoint(1.0, 1.0);
    factionInfoPanel:SetBorderColor(vector.create(0.1, 0.1, 0.1));
    factionInfoPanel:SetBorderSize(2);

    -- Faction icon
    local factionIconSize = 50;

    characterCreationScreen.selectedFactionIcon = UIAtlasPanel.NewAtlasPanel(factionInfoPanel, 0, 0, factionIconSize, factionIconSize, 0, {
        texture = factionInfo.factionAtlasTexture,
        numTexturesX = factionInfo.factionAtlasTextureNumTexturesX,
        numTexturesY = factionInfo.factionAtlasTextureNumTexturesY,
        atlasIndex = factionInfo.factionAtlasIndex,
    });
    characterCreationScreen.selectedFactionIcon:SetAnchor(0.0, 1.0);
    characterCreationScreen.selectedFactionIcon:SetRelativePoint(0.2, 0.8);

    -- Faction title
    characterCreationScreen.selectedFactionTitle = factionInfoPanel:NewText(factionInfo.name, factionIconSize, 0, 0, "YellowText");
    characterCreationScreen.selectedFactionTitle:SetAnchor(0.0, 1.0);
    characterCreationScreen.selectedFactionTitle:SetRelativePoint(0.0, 1.0);

    -- Faction description scrollbox
    local factionDescriptionAreaOffset = factionIconSize * 0.8;
    local factionDescriptionAreaHeight = factionInfoHeight - factionDescriptionAreaOffset;
    local factionDescriptionPaddingX = 10;
    local factionDescriptionAreaWidth = characterInfoWidth - (factionDescriptionPaddingX * 2);
    
    characterCreationScreen.selectedFactionDescriptionScrollBox = UIScrollBox.NewScrollBox(factionInfoPanel, factionDescriptionPaddingX, -factionDescriptionAreaOffset, factionDescriptionAreaWidth, factionDescriptionAreaHeight, 0, {
        verticalScrollBar = true,
    });
    characterCreationScreen.selectedFactionDescriptionScrollBox:SetAnchor(0.0, 1.0);
    characterCreationScreen.selectedFactionDescriptionScrollBox:SetRelativePoint(0.0, 1.0);

    -- Faction description
    characterCreationScreen.selectedFactionDescription = characterCreationScreen.selectedFactionDescriptionScrollBox.content:NewText(factionInfo.factionDescription, 0, 0, 0, "DefaultButtonText");
    characterCreationScreen.selectedFactionDescription:SetAnchor(0.0, 1.0);
    characterCreationScreen.selectedFactionDescription:SetRelativePoint(0.0, 1.0);

    UpdateScrollBoxBasedOnContent(characterCreationScreen.selectedFactionDescriptionScrollBox, characterCreationScreen.selectedFactionDescription);

    return factionInfoPanel;
end

local function CreateRaceInfoPanel(characterCreationScreen : CharacterCreationScreen, currentY : number, characterInfoWidth : number, raceInfoHeight : number)
    local factionInfo = factionInfos[1];
    local race = factionInfo.races[1];

    local raceInfoPanel : Panel = characterCreationScreen.characterInfoPanel:NewPanel(0, currentY, characterInfoWidth, raceInfoHeight, 0, "DialogBox");
    raceInfoPanel:SetAnchor(1.0, 1.0);
    raceInfoPanel:SetRelativePoint(1.0, 1.0);
    raceInfoPanel:SetBorderColor(vector.create(0.1, 0.1, 0.1));
    raceInfoPanel:SetBorderSize(2);

    -- Race icon
    local raceIconSize = 50;

    characterCreationScreen.selectedRaceIcon = UIAtlasPanel.NewAtlasPanel(raceInfoPanel, 0, 0, raceIconSize, raceIconSize, 0, {
        texture = factionInfo.raceAtlasTexture,
        numTexturesX = factionInfo.raceAtlasTextureNumTexturesX,
        numTexturesY = factionInfo.raceAtlasTextureNumTexturesY,
        atlasIndex = race.genderIndexToRaceAtlasIndex[1],
    });
    characterCreationScreen.selectedRaceIcon:SetAnchor(0.0, 1.0);
    characterCreationScreen.selectedRaceIcon:SetRelativePoint(0.2, 0.8);

    -- Race Title
    characterCreationScreen.selectedRaceTitle = raceInfoPanel:NewText(race.name, raceIconSize, 0, 0, "YellowText");
    characterCreationScreen.selectedRaceTitle:SetAnchor(0.0, 1.0);
    characterCreationScreen.selectedRaceTitle:SetRelativePoint(0.0, 1.0);

    -- Race description scrollbox
    local raceDescriptionAreaOffset = raceIconSize * 0.8;
    local raceDescriptionAreaHeight = raceInfoHeight - raceDescriptionAreaOffset;
    local raceDescriptionAreaWidth = characterInfoWidth-20;
    characterCreationScreen.selectedRaceDescriptionScrollBox = UIScrollBox.NewScrollBox(raceInfoPanel, 10, -raceDescriptionAreaOffset, raceDescriptionAreaWidth, raceDescriptionAreaHeight, 0, {
        verticalScrollBar = true,
    });
    characterCreationScreen.selectedRaceDescriptionScrollBox:SetAnchor(0.0, 1.0);
    characterCreationScreen.selectedRaceDescriptionScrollBox:SetRelativePoint(0.0, 1.0);
    
    -- Race description
    characterCreationScreen.selectedRaceDescription = characterCreationScreen.selectedRaceDescriptionScrollBox.content:NewText(race.description, 0, 0, 0, "DefaultButtonText");
    characterCreationScreen.selectedRaceDescription:SetAnchor(0.0, 1.0);
    characterCreationScreen.selectedRaceDescription:SetRelativePoint(0.0, 1.0);

    UpdateScrollBoxBasedOnContent(characterCreationScreen.selectedRaceDescriptionScrollBox, characterCreationScreen.selectedRaceDescription);

    return raceInfoPanel;
end

local function CreateClassInfoPanel(characterCreationScreen : CharacterCreationScreen, currentY : number, characterInfoWidth : number, classInfoHeight : number)
    local classInfo = classInfos[1];
    
    local classInfoPanel : Panel = characterCreationScreen.characterInfoPanel:NewPanel(0, currentY, characterInfoWidth, classInfoHeight, 0, "DialogBox");
    classInfoPanel:SetAnchor(1.0, 1.0);
    classInfoPanel:SetRelativePoint(1.0, 1.0);
    classInfoPanel:SetBorderColor(vector.create(0.1, 0.1, 0.1));
    classInfoPanel:SetBorderSize(2);

    -- Class icon
    local classIconSize = 50;

    characterCreationScreen.selectedClassIcon = UIAtlasPanel.NewAtlasPanel(classInfoPanel, 0, 0, classIconSize, classIconSize, 0, {
        texture = classInfo.iconAtlasTexture,
        numTexturesX = classInfo.iconAtlasTextureNumTexturesX,
        numTexturesY = classInfo.iconAtlasTextureNumTexturesY,
        atlasIndex = classInfo.iconAtlasIndex,
    });
    characterCreationScreen.selectedClassIcon:SetAnchor(0.0, 1.0);
    characterCreationScreen.selectedClassIcon:SetRelativePoint(0.2, 0.8);

    -- Class Title
    characterCreationScreen.selectedClassTitle = classInfoPanel:NewText(classInfo.name, classIconSize, 0, 0, "YellowText");
    characterCreationScreen.selectedClassTitle:SetAnchor(0.0, 1.0);
    characterCreationScreen.selectedClassTitle:SetRelativePoint(0.0, 1.0);

    -- Class description area
    local classDescriptionAreaOffset = classIconSize * 0.8;
    local classDescriptionAreaHeight = classInfoHeight - classDescriptionAreaOffset;
    local classDescriptionAreaWidth = characterInfoWidth-20;
    characterCreationScreen.selectedClassDescriptionScrollBox = UIScrollBox.NewScrollBox(classInfoPanel, 10, -classDescriptionAreaOffset, classDescriptionAreaWidth, classDescriptionAreaHeight, 0, {
        verticalScrollBar = true,
    });
    characterCreationScreen.selectedClassDescriptionScrollBox:SetAnchor(0.0, 1.0);
    characterCreationScreen.selectedClassDescriptionScrollBox:SetRelativePoint(0.0, 1.0);

    -- Class description
    characterCreationScreen.selectedClassDescription = characterCreationScreen.selectedClassDescriptionScrollBox.content:NewText(classInfo.description, 0, 0, 0, "DefaultButtonText");
    characterCreationScreen.selectedClassDescription:SetAnchor(0.0, 1.0);
    characterCreationScreen.selectedClassDescription:SetRelativePoint(0.0, 1.0);

    UpdateScrollBoxBasedOnContent(characterCreationScreen.selectedClassDescriptionScrollBox, characterCreationScreen.selectedClassDescription);

    return classInfoPanel;
end

local function CreateRightPanel(characterCreationScreen : CharacterCreationScreen)
    local characterInfoWidth = 430;
    local characterInfoPanelHeight = 900;
    local spacing = 15;

    local factionInfoHeight = 250;
    local raceInfoHeight = 410;
    local classInfoHeight = characterInfoPanelHeight - factionInfoHeight - raceInfoHeight - spacing * 2;

    -- characterInfoPanel becomes a V layout. The three info subpanels stack with `spacing` between them.
    -- alpha=0.2 preserves the dialog tint that the original SetAlpha(0.2) set on the container.
    characterCreationScreen.characterInfoPanel = UIVerticalLayout.NewVerticalLayout(
        characterCreationScreen.widget, -20, -40, characterInfoWidth, characterInfoPanelHeight, 0,
        { panelTemplate = "DialogBox", alpha = 0.2, spacing = spacing });
    characterCreationScreen.characterInfoPanel:SetAnchor(1.0, 1.0);
    characterCreationScreen.characterInfoPanel:SetRelativePoint(1.0, 1.0);

    local right = characterCreationScreen.characterInfoPanel;

    -- Sub-info functions still take (currentY, width, height) but currentY is ignored —
    -- the V layout owns positioning. Each returns the panel it created so we can SetChildOpts.
    local factionPanel = CreateFactionInfoPanel(characterCreationScreen, 0, characterInfoWidth, factionInfoHeight);
    local racePanel    = CreateRaceInfoPanel(characterCreationScreen, 0, characterInfoWidth, raceInfoHeight);
    local classPanel   = CreateClassInfoPanel(characterCreationScreen, 0, characterInfoWidth, classInfoHeight);

    right:SetChildOpts(factionPanel, { sizeY = factionInfoHeight, modeX = "flex" });
    right:SetChildOpts(racePanel,    { sizeY = raceInfoHeight,    modeX = "flex" });
    right:SetChildOpts(classPanel,   { sizeY = classInfoHeight,   modeX = "flex" });

    -- Accept button
    local acceptButton = UIButton.NewButton(characterCreationScreen.widget, -20, 80, 200, 40, 0, {
        panelTemplate = "DefaultButtonPanel",
        textTemplate = "SmallYellowButtonText",
    });
    acceptButton:SetAnchor(1.0, 0.0);
    acceptButton:SetRelativePoint(1.0, 0.0);
    acceptButton:SetText("Accept");
    acceptButton:SetOnMouseUp(function(eventID, widget, ...)
        local stack = UIStack.Get("Menu");
        -- TODO: Create character
        stack:Pop();
    end);

    -- Back button
    local backButton = UIButton.NewButton(characterCreationScreen.widget, -30, 40, 180, 30, 0, {
        panelTemplate = "DefaultButtonPanel",
        textTemplate = "SmallYellowButtonText",
    });
    backButton:SetAnchor(1.0, 0.0);
    backButton:SetRelativePoint(1.0, 0.0);
    backButton:SetText("Back");
    backButton:SetOnMouseUp(function(eventID, widget, ...)
        local stack = UIStack.Get("Menu");
        stack:Push("CharacterSelect");
    end);
end

local function CreateCharacterCreationScreenMethods(characterCreationScreen : CharacterCreationScreen)
    characterCreationScreen.SetRace = function(self : CharacterCreationScreen, raceIndex : number)
        self.raceIndex = raceIndex;

        -- Set the color of the option buttons
        for i, button in self.buttons.raceButtons do
            if i == raceIndex then
                button:SetColor(vector.create(1.0, 1.0, 1.0));
            else
                button:SetColor(vector.create(0.4, 0.4, 0.4));
            end
        end

        self:RefreshFactionInfo();
        self:RefreshRaceInfo();
    end

    characterCreationScreen.SetGender = function(self : CharacterCreationScreen, genderIndex : number)
        self.genderIndex = genderIndex;

        -- Set the color of the option buttons
        for i, button in self.buttons.genderButtons do
            if i == genderIndex then
                button:SetColor(vector.create(1.0, 1.0, 1.0));
            else
                button:SetColor(vector.create(0.4, 0.4, 0.4));
            end
        end

        self:OnGenderChanged();
    end

    characterCreationScreen.SetClass = function(self : CharacterCreationScreen, classIndex : number)
        self.classIndex = classIndex;

        -- Set the color of the option buttons
        for i, button in self.buttons.classButtons do
            if i == classIndex then
                button:SetColor(vector.create(1.0, 1.0, 1.0));
            else
                button:SetColor(vector.create(0.4, 0.4, 0.4));
            end
        end

        self:RefreshClassInfo();
    end

    characterCreationScreen.RefreshFactionInfo = function(self : CharacterCreationScreen)
        local factionIndex = self.raceIndexToFactionIndex[self.raceIndex];
        local factionInfo = factionInfos[factionIndex];

        characterCreationScreen.selectedFactionIcon:SetAtlasIndex(factionInfo.factionAtlasIndex);
        characterCreationScreen.selectedFactionTitle:SetText(factionInfo.name);
        characterCreationScreen.selectedFactionDescription:SetText(factionInfo.factionDescription);

        UpdateScrollBoxBasedOnContent(characterCreationScreen.selectedFactionDescriptionScrollBox, characterCreationScreen.selectedFactionDescription);
    end

    characterCreationScreen.RefreshRaceInfo = function(self : CharacterCreationScreen)
        local factionIndex = self.raceIndexToFactionIndex[self.raceIndex];
        local factionInfo = factionInfos[factionIndex];

        local localRaceIndex = self.raceIndexToFactionLocalRaceIndex[self.raceIndex];
        local race = factionInfo.races[localRaceIndex];

        local genderIndex = self.genderIndex;

        local raceAtlasIndex = race.genderIndexToRaceAtlasIndex[genderIndex];
        characterCreationScreen.selectedRaceIcon:SetAtlasIndex(raceAtlasIndex);
        characterCreationScreen.selectedRaceTitle:SetText(race.name);
        characterCreationScreen.selectedRaceDescription:SetText(race.description);

        UpdateScrollBoxBasedOnContent(characterCreationScreen.selectedRaceDescriptionScrollBox, characterCreationScreen.selectedRaceDescription);
    end

    characterCreationScreen.RefreshClassInfo = function(self : CharacterCreationScreen)
        local classInfo = classInfos[self.classIndex];

        characterCreationScreen.selectedClassIcon:SetAtlasIndex(classInfo.iconAtlasIndex);
        characterCreationScreen.selectedClassTitle:SetText(classInfo.name);
        characterCreationScreen.selectedClassDescription:SetText(classInfo.description);

        UpdateScrollBoxBasedOnContent(characterCreationScreen.selectedClassDescriptionScrollBox, characterCreationScreen.selectedClassDescription);
    end

    characterCreationScreen.RefreshRaceSelectionIconGender = function(self : CharacterCreationScreen)
        local genderIndex = self.genderIndex;

        for i, button in self.buttons.raceButtons do
            local factionIndex = self.raceIndexToFactionIndex[i];
            local factionInfo = factionInfos[factionIndex];

            local localRaceIndex = self.raceIndexToFactionLocalRaceIndex[i];
            local race = factionInfo.races[localRaceIndex];

            button:SetAtlasIndex(race.genderIndexToRaceAtlasIndex[genderIndex]);
        end
    end

    characterCreationScreen.OnGenderChanged = function(self : CharacterCreationScreen)
        self:RefreshRaceInfo();
        self:RefreshRaceSelectionIconGender();
    end

    return characterCreationScreen;
end

local function CreateCharacterCreationScreen(stack : Stack)
    local characterCreationScreen : CharacterCreationScreen = {};

    -- Create Canvas
    characterCreationScreen.canvas = UI.GetCanvas("CharacterCreate", 0, 0, 1920, 1080); -- TODO: We don't want to hardcode the reference resolution here
    
    -- Create widget covering whole canvas
    characterCreationScreen.widget = characterCreationScreen.canvas:NewWidget(0, 0, 0);
    characterCreationScreen.widget:SetEnabled(false);
    characterCreationScreen.widget:SetAnchor(0.5, 0.5);
    characterCreationScreen.widget:SetRelativePoint(0.5, 0.5);

    -- Create panels
    characterCreationScreen.buttons = {};
    CreateLeftPanel(characterCreationScreen);
    CreateMiddlePanel(characterCreationScreen);
    CreateRightPanel(characterCreationScreen);

    -- Variables
    characterCreationScreen.name = "";
    characterCreationScreen.factionIndex = 1;
    characterCreationScreen.raceIndex = 1;
    characterCreationScreen.genderIndex = 1;
    characterCreationScreen.classIndex = 1;

    -- Methods
    characterCreationScreen = CreateCharacterCreationScreenMethods(characterCreationScreen);

    -- Force the layouts to settle before the first SetText calls below. Text widgets emit
    -- their GPU vertices using the parent's world position at SetText time, and a parent
    -- transform change does not propagate DirtyWidgetTransform to descendants — so without
    -- this, descriptions whose owning sub-panel moves during the first layout pass end up
    -- with stale vertices and stay invisible until the user manually re-triggers SetText
    -- (e.g. by selecting a different race/class).
    characterCreationScreen.optionsPanel:Refresh();
    characterCreationScreen.characterInfoPanel:Refresh();

    -- Default options
    characterCreationScreen:SetRace(1);
    characterCreationScreen:SetGender(1);
    characterCreationScreen:SetClass(1);

    stack:Register("CharacterCreation", characterCreationScreen.widget, 0); -- Name, widget, slot, onPush, onPop
    --stack:Push("CharacterCreation");

    return characterCreationScreen;
end

local function OnGameLoaded(eventID : number, data : any)
    local stack = UIStack.Get("Menu")

    characterCreationScreen = CreateCharacterCreationScreen(stack);
end

RegisterEvent(GameEvent.Loaded, OnGameLoaded)