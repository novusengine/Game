local Type = require("Type")
local D = require("Definition")

return D.Definitions
{
    D.Enum("DisplaySource", Type.U8,
    {
        D.Field("CreatureDisplayInfo", 0),
        D.Field("ItemDisplayInfo", 1)
    }),

    D.ClientDB("DisplayRegistration",
    {
        D.Field("modelAssetID", Type.U64),
        D.Field("displayID", Type.U32),
        D.Field("source", Type.U8),
        D.Field("modelVariant", Type.U8),
        D.Field("reserved", Type.U16)
    }),

    D.ClientDB("DisplayParameter",
    {
        D.Field("value0", Type.U64),
        D.Field("value1", Type.U64),
        D.Field("displayRegistrationID", Type.U32),
        D.Field("modelParameterStableID", Type.U32),
        D.Field("type", Type.U8),
        D.Field("reserved", Type.ARRAY, { type = Type.U8, count = 7 })
    })
}
