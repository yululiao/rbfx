// Default game configuration, loaded right after rbfx_env.js. This file sits at the root of
// the minigame package on purpose: it stays editable after packaging, and the packaging
// pipeline regenerates it with per-project values. Keep it dependency-free - rbfx_env.js
// reads rbfx.game_config lazily, and nothing here may touch the DOM or vendor APIs.

rbfx.game_config = {
    // Engine module location, relative to the package root. Both entries describe the same
    // engine: the compatible variant is only downloaded on devices that need it (see
    // rbfx.env.is_compatible), is size-optimized and avoids WebAssembly BigInt. A package
    // assembled with a single variant carries that variant's path in wasm_file and "" here.
    wasm_file: "package_wasm/MinigamePlayer.wasm",
    wasm_file_compatible: "package_wasm_compatible/MinigamePlayer.wasm",

    // Data subpackage root name, "" when the game data ships in the main package. The
    // loading manager downloads it before the engine starts; the file manifest maps the
    // engine-visible names back onto it.
    data_subpackage: "package_data",

    // Engine VFS mount point for the game files. The platform file layer mounts the vendor
    // file system below this root before main() runs.
    data_root: "Data",

    // Canvas sizing: true sizes the canvas to the physical resolution (logical size times
    // the pixel ratio); false keeps the logical resolution and lets the vendor scale it.
    screen: {native_resolution: true},

    // Show the WeChat debug overlay (vConsole). Douyin has no equivalent switch.
    enable_debug: false,

    // Ask the vendor update manager to check for a new package version on every launch.
    enable_check_update: true
};
