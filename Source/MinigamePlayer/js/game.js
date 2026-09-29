// rbfx minigame entry point. The vendor runtime executes this file first, inside the main
// package, before any subpackage is downloaded.
//
// The require order is strict: environment detection runs before anything that branches on
// the platform, the adapter installs the DOM surface before the loading screen touches
// canvas/Image, the runtime/files/SDK layers publish the rbfx bridges that the engine hooks
// into, and the loading screen starts last so nothing competes with its first frame.
require("./rbfx_env.js");
require("./rbfx_game_config.js");
require("./weapp-adapter.js");
require("./minigame_runtime.js");
require("./minigame_files.js");
require("./minigame_sdk.js");
require("./loading_config.js");
require("./loading.js");

console.log("rbfx: bootstrap done, platform=" + rbfx.env.name +
    ", engine=" + (rbfx.env.is_compatible ? "compatible" : "standard"));
