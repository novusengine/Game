local ItemUtil = require("@src/API/Game/ItemUtil")
local Bag = require("@src/API/Game/Bag")
local PlayerContainers = require("@src/API/Game/PlayerContainers")

local GameTooltip = require("@src/API/Game/GameTooltip")

local function OnGameLoaded(eventID : number, motd : string)
    local luaRamUsage : number = gcinfo()
    local version : vector = Engine.Version
    
    local rarityText1 = ItemUtil.GetRarityText(ItemUtil.Rarity.Poor)
    local rarityText2 = ItemUtil.GetRarityText(ItemUtil.Rarity.Epic)
    local rarityText3 = ItemUtil.GetRarityText(ItemUtil.Rarity.Unique)

    local rarityColorText1 = ItemUtil.GetRarityColor(ItemUtil.Rarity.Poor)
    local rarityColorText2 = ItemUtil.GetRarityColor(ItemUtil.Rarity.Epic)
    local rarityColorText3 = ItemUtil.GetRarityColor(ItemUtil.Rarity.Unique)

    local categoryText1 = ItemUtil.GetCategoryText(ItemUtil.Category.Miscellaneous)
    local categoryText2 = ItemUtil.GetCategoryText(ItemUtil.Category.Quest)
    local categoryText3 = ItemUtil.GetCategoryText(ItemUtil.Category.Weapon)

    local typeText1 = ItemUtil.GetCategoryTypeText(ItemUtil.Category.Miscellaneous, ItemUtil.CategoryType.Miscellaneous.Mount)
    local typeText2 = ItemUtil.GetCategoryTypeText(ItemUtil.Category.Quest, ItemUtil.CategoryType.Quest.Quest)
    local typeText3 = ItemUtil.GetCategoryTypeText(ItemUtil.Category.Weapon, ItemUtil.CategoryType.Weapon.FishingPole)

    local weaponStyle1 = ItemUtil.GetWeaponStyleText(ItemUtil.WeaponStyle.Unspecified)
    local weaponStyle2 = ItemUtil.GetWeaponStyleText(ItemUtil.WeaponStyle.OneHand)
    local weaponStyle3 = ItemUtil.GetWeaponStyleText(ItemUtil.WeaponStyle.TwoHand)
    local weaponStyle4 = ItemUtil.GetWeaponStyleText(ItemUtil.WeaponStyle.MainHand)
    local weaponStyle5 = ItemUtil.GetWeaponStyleText(ItemUtil.WeaponStyle.OffHand)
    local weaponStyle6 = ItemUtil.GetWeaponStyleText(ItemUtil.WeaponStyle.Ranged)
    local weaponStyle7 = ItemUtil.GetWeaponStyleText(ItemUtil.WeaponStyle.Wand)
    local weaponStyle8 = ItemUtil.GetWeaponStyleText(ItemUtil.WeaponStyle.Tool)

    local weaponStyleFromWeaponCategoryType1 = ItemUtil.GetWeaponStyleTextFromWeaponCategoryType(ItemUtil.CategoryType.Weapon.Weapon)
    local weaponStyleFromWeaponCategoryType2 = ItemUtil.GetWeaponStyleTextFromWeaponCategoryType(ItemUtil.CategoryType.Weapon.Sword_1H)
    local weaponStyleFromWeaponCategoryType3 = ItemUtil.GetWeaponStyleTextFromWeaponCategoryType(ItemUtil.CategoryType.Weapon.Sword_2H)
    local weaponStyleFromWeaponCategoryType4 = ItemUtil.GetWeaponStyleTextFromWeaponCategoryType(ItemUtil.CategoryType.Weapon.Mace_1H)
    local weaponStyleFromWeaponCategoryType5 = ItemUtil.GetWeaponStyleTextFromWeaponCategoryType(ItemUtil.CategoryType.Weapon.Mace_2H)
    local weaponStyleFromWeaponCategoryType6 = ItemUtil.GetWeaponStyleTextFromWeaponCategoryType(ItemUtil.CategoryType.Weapon.Axe_1H)
    local weaponStyleFromWeaponCategoryType7 = ItemUtil.GetWeaponStyleTextFromWeaponCategoryType(ItemUtil.CategoryType.Weapon.Axe_2H)
    local weaponStyleFromWeaponCategoryType8 = ItemUtil.GetWeaponStyleTextFromWeaponCategoryType(ItemUtil.CategoryType.Weapon.Dagger)
    local weaponStyleFromWeaponCategoryType9 = ItemUtil.GetWeaponStyleTextFromWeaponCategoryType(ItemUtil.CategoryType.Weapon.FistWeapon)
    local weaponStyleFromWeaponCategoryType10 = ItemUtil.GetWeaponStyleTextFromWeaponCategoryType(ItemUtil.CategoryType.Weapon.Polearm)
    local weaponStyleFromWeaponCategoryType11 = ItemUtil.GetWeaponStyleTextFromWeaponCategoryType(ItemUtil.CategoryType.Weapon.Staff)
    local weaponStyleFromWeaponCategoryType12 = ItemUtil.GetWeaponStyleTextFromWeaponCategoryType(ItemUtil.CategoryType.Weapon.Bow)
    local weaponStyleFromWeaponCategoryType13 = ItemUtil.GetWeaponStyleTextFromWeaponCategoryType(ItemUtil.CategoryType.Weapon.Crossbow)
    local weaponStyleFromWeaponCategoryType14 = ItemUtil.GetWeaponStyleTextFromWeaponCategoryType(ItemUtil.CategoryType.Weapon.Gun)
    local weaponStyleFromWeaponCategoryType15 = ItemUtil.GetWeaponStyleTextFromWeaponCategoryType(ItemUtil.CategoryType.Weapon.Wand)
    local weaponStyleFromWeaponCategoryType16 = ItemUtil.GetWeaponStyleTextFromWeaponCategoryType(ItemUtil.CategoryType.Weapon.Miscellaneous_Tools)
    local weaponStyleFromWeaponCategoryType17 = ItemUtil.GetWeaponStyleTextFromWeaponCategoryType(ItemUtil.CategoryType.Weapon.FishingPole)
    
    print(`Message of the day : "{motd}"`)
    print(`Engine : {Engine.Name} (Version : {version.x}.{version.y}.{version.z}) - Lua Ram Usage : {luaRamUsage}KB)`)

    print(`RarityText1 : {rarityText1} - {rarityColorText1}`)
    print(`RarityText2 : {rarityText2} - {rarityColorText2}`)
    print(`RarityText3 : {rarityText3} - {rarityColorText3}`)

    print(`TypeText1 : {categoryText1} - {typeText1}`)
    print(`TypeText2 : {categoryText2} - {typeText2}`)
    print(`TypeText3 : {categoryText3} - {typeText3}`)

    print(`Weapon Styles : {weaponStyle1} - {weaponStyle2} - {weaponStyle3} - {weaponStyle4} - {weaponStyle5} - {weaponStyle6} - {weaponStyle7} - {weaponStyle8}`)
    print(`Weapon Styles from Weapon Category Types 1 : {weaponStyleFromWeaponCategoryType1} - {weaponStyleFromWeaponCategoryType2} - {weaponStyleFromWeaponCategoryType3} - {weaponStyleFromWeaponCategoryType4} - {weaponStyleFromWeaponCategoryType5} - {weaponStyleFromWeaponCategoryType6}`)
    print(`Weapon Styles from Weapon Category Types 2 : {weaponStyleFromWeaponCategoryType7} - {weaponStyleFromWeaponCategoryType8} - {weaponStyleFromWeaponCategoryType9} - {weaponStyleFromWeaponCategoryType10} - {weaponStyleFromWeaponCategoryType11} - {weaponStyleFromWeaponCategoryType12}`)
    print(`Weapon Styles from Weapon Category Types 3 : {weaponStyleFromWeaponCategoryType13} - {weaponStyleFromWeaponCategoryType14} - {weaponStyleFromWeaponCategoryType15} - {weaponStyleFromWeaponCategoryType16} - {weaponStyleFromWeaponCategoryType17}`)
end

RegisterGameEvent(GameEvent.Loaded, OnGameLoaded)