local Module = { }

Module.Rarity =
{
    Poor      = 1,
    Common    = 2,
    Uncommon  = 3,
    Rare      = 4,
    Epic      = 5,
    Legendary = 6,
    Artifact  = 7,
    Unique    = 8
}

Module.RarityNames =
{
    "Poor",
    "Common",
    "Uncommon",
    "Rare",
    "Epic",
    "Legendary",
    "Artifact",
    "Unique"
}
Module.RarityColors =
{
    0x9d9d9d,
    0xffffff,
    0x1eff00,
    0x0070dd,
    0xa335ee,
    0xff8000,
    0xe6cc80,
    0x00ccff
}

Module.GetRarityText = function(rarity : number) : string
    local numRarityNames = #Module.RarityNames
    local rarityIndex = if rarity > 0 and rarity <= numRarityNames then rarity else Module.Rarity.Poor

    return Module.RarityNames[rarityIndex]
end

Module.GetRarityColor = function(rarity : number) : number
    local numRarityColors = #Module.RarityColors
    local rarityIndex = if rarity > 0 and rarity <= numRarityColors then rarity else Module.Rarity.Poor

    return Module.RarityColors[rarityIndex]
end

Module.Category = 
{
    Miscellaneous   = 1,
    TradeGoods      = 2,
    Consumable      = 3,
    Reagent         = 4,
    Container       = 5,
    Quest           = 6,
    Armor           = 7,
    Weapon          = 8
}

Module.CategoryType =
{
    Miscellaneous =
    {
        Miscellaneous   = 1,
        Reagent         = 2,
        Pet             = 3,
        Mount           = 4,
        Junk            = 5
    },
    
    TradeGoods =
    {
        TradeGoods  = 1,
        Meat        = 2,
        Cloth       = 3,
        Leather     = 4,
        MetalStone  = 5,
        Herb        = 6,
        Materials   = 7
    },
    
    Consumable =
    {
        Consumable  = 1,
        FoodDrink   = 2,
        Bandage     = 3,
        Flask       = 4,
        Elixir      = 5,
        Potion      = 6,
        Scroll      = 7
    },
    
    Reagent =
    {
        Reagent = 1
    },
    
    Container =
    {
        Bag = 1
    },
    
    Quest =
    {
        Quest   = 1
    },
    
    Armor =
    {
        Armor   = 1,
        Cloth   = 2,
        Leather = 3,
        Mail    = 4,
        Plate   = 5,
        Shield  = 6,
        Libram  = 7,
        Idol    = 8,
        Totem   = 9
    },
    
    Weapon =
    {
        Weapon              = 1,
        Sword_1H            = 2,
        Sword_2H            = 3,
        Mace_1H             = 4,
        Mace_2H             = 5,
        Axe_1H              = 6,
        Axe_2H              = 7,
        Dagger              = 8,
        FistWeapon          = 9,
        Polearm             = 10,
        Staff               = 11,
        Bow                 = 12,
        Crossbow            = 13,
        Gun                 = 14,
        Wand                = 15,
        Miscellaneous_Tools = 16,
        FishingPole         = 17
    }
}

Module.CategoryNames =
{
    "Miscellaneous",
    "Trade Goods",
    "Consumable",
    "Reagent",
    "Container",
    "Quest",
    "Armor",
    "Weapon"
}

Module.CategoryTypeNames =
{
    -- Miscellaneous
    {
        "Miscellaneous",
        "Reagent",
        "Pet",
        "Mount",
        "Junk"
    },

    -- Trade Goods
    {
        "Trade Goods",
        "Meat",
        "Cloth",
        "Leather",
        "Metal & Stone",
        "Herb",
        "Materials"
    },

    -- Consumable
    {
        "Consumable",
        "Food & Drink",
        "Bandage",
        "Flask",
        "Elixir",
        "Potion",
        "Scroll"
    },

    -- Reagent
    {
        "Reagent"
    },

    -- Container
    {
        "Bag"
    },

    -- Quest
    {
        "Quest"
    },

    -- Armor
    {
        "Armor",
        "Cloth",
        "Leather",
        "Mail",
        "Plate",
        "Shield",
        "Libram",
        "Idol",
        "Totem"
    },

    -- Weapon
    {
        "Weapon",
        "Sword",
        "Sword",
        "Mace",
        "Mace",
        "Axe",
        "Axe",
        "Dagger",
        "Fist Weapon",
        "Polearm",
        "Staff",
        "Bow",
        "Crossbow",
        "Gun",
        "Wand",
        "Miscellaneous (Tools)",
        "Fishing Pole"
    }
}

Module.WeaponStyle =
{
    Unspecified = 1,
    One_Hand    = 2,
    Two_Hand    = 3,
    Ranged      = 4,
    Wand        = 5,
    Tool        = 6
}
Module.WeaponStyleNames =
{
    "Unspecified",
    "One-Hand",
    "Two-Hand",
    "Ranged",
    "Wand",
    "Tool"
}

Module.GetCategoryText = function(category : number) : string
    local numCategoryNames = #Module.CategoryNames
    local categoryIndex = if category > 0 and category <= numCategoryNames then category else Module.Category.Miscellaneous

    return Module.CategoryNames[categoryIndex]
end

Module.GetCategoryTypeText = function(category : number, type : number) : string
    local numCategoryNames = #Module.CategoryNames
    local categoryIndex = if category > 0 and category <= numCategoryNames then category else Module.Category.Miscellaneous

    local numCategoryTypeNames = #Module.CategoryTypeNames[categoryIndex]
    local typeIndex = if type > 0 and type <= numCategoryTypeNames then type else Module.CategoryType.Miscellaneous.Miscellaneous

    return Module.CategoryTypeNames[categoryIndex][typeIndex]
end

Module.GetWeaponStyleText = function(style : number) : string
    local numWeaponStyleNames = #Module.WeaponStyleNames
    local weaponStyleIndex = if style > 0 and style <= numWeaponStyleNames then style else Module.WeaponStyle.Unspecified

    return Module.WeaponStyleNames[weaponStyleIndex]
end

Module.GetWeaponStyleTextFromWeaponCategoryType = function(categoryType : number) : string
    local weaponStyleIndex = Module.WeaponStyle.Unspecified

    if categoryType == Module.CategoryType.Weapon.Sword_1H or
       categoryType == Module.CategoryType.Weapon.Mace_1H or
       categoryType == Module.CategoryType.Weapon.Axe_1H or
       categoryType == Module.CategoryType.Weapon.Dagger or
       categoryType == Module.CategoryType.Weapon.FistWeapon then
        weaponStyleIndex = Module.WeaponStyle.One_Hand
    elseif categoryType == Module.CategoryType.Weapon.Sword_2H or
       categoryType == Module.CategoryType.Weapon.Mace_2H or
       categoryType == Module.CategoryType.Weapon.Axe_2H or
       categoryType == Module.CategoryType.Weapon.Polearm or
       categoryType == Module.CategoryType.Weapon.Staff or
       categoryType == Module.CategoryType.Weapon.FishingPole then
        weaponStyleIndex = Module.WeaponStyle.Two_Hand
    elseif categoryType == Module.CategoryType.Weapon.Bow or
       categoryType == Module.CategoryType.Weapon.Crossbow or
       categoryType == Module.CategoryType.Weapon.Gun then
        weaponStyleIndex = Module.WeaponStyle.Ranged
    elseif categoryType == Module.CategoryType.Weapon.Wand then
        weaponStyleIndex = Module.WeaponStyle.Wand
    elseif categoryType == Module.CategoryType.Weapon.Miscellaneous_Tools then
        weaponStyleIndex = Module.WeaponStyle.Tool
    end

    local numWeaponStyleNames = #Module.WeaponStyleNames
    weaponStyleIndex = if weaponStyleIndex > 0 and weaponStyleIndex <= numWeaponStyleNames then weaponStyleIndex else Module.WeaponStyle.Unspecified

    return Module.WeaponStyleNames[weaponStyleIndex]
end

return Module