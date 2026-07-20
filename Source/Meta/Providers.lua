MetaGen.RegisterProvider {
    name = "Game.Runtime",
    definitionRoot = path.getabsolute("Definitions/Game", _SCRIPT_DIR),
    namespace = "MetaGen.Game",
    dependencies = { "Engine.ClientDB", "Engine.Protocol" }
}
