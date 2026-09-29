// Installed as game.js at the root of the engine wasm subpackage (package_wasm/ or
// package_wasm_compatible/). The vendor runtime executes it right after the subpackage
// download completes, before the loadSubpackage success callback fires.
//
// The Emscripten glue beside this file is built with MODULARIZE=1: requiring it only defines
// the module factory (createRbfxModule). The factory is handed to the loading manager, which
// owns the canvas and calls it. Nothing here may start the engine directly.
(function () {
    try {
        var factory = require("./MinigamePlayer");
        if (globalThis.rbfx && globalThis.rbfx.loading) {
            globalThis.rbfx.loading.onEngineFactoryReady(factory);
        } else {
            console.error("rbfx: engine subpackage ran before the main package bootstrap");
        }
    } catch (error) {
        console.error("rbfx: failed to load the engine module", error);
        if (globalThis.rbfx && globalThis.rbfx.loading) {
            globalThis.rbfx.loading.onEngineFactoryFailed(error);
        }
    }
})();
