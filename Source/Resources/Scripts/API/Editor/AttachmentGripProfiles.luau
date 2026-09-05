local profiles = {
    CorruptedAshbringer = {
        model = "model/item/objectcomponents/weapon/sword_2h_ashbringercorrupt.model",
        semantic = "right_hand",
        translation = vec.New(0.0, 0.0, 0.0),
        rotation = vec.New(-90.0, -90.0, 0.0),
        scale = 50.0,
        primaryGrip = vec.New(0.0, 0.0, 0.0),
        secondaryGrip = vec.New(0.0, 0.0, 0.25),
        handleAxis = vec.New(0.0, 0.0, 1.0),
        bladeEdgeAxis = vec.New(0.0, 1.0, 0.0),
        rightGripAxis = vec.New(1.0, 0.0, 0.0),
        positionTolerance = 0.015,
        angleTolerance = 8.0,
        axisLength = 0.14,
    },
}

return profiles
