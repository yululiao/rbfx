// rbfx minigame bootstrap: base environment, before anything else runs.
//
// The ordering below encodes two runtime pitfalls:
//
//   * Douyin also defines a `wx` global (an alias of `tt`). Probing WeChat with
//     `typeof wx != 'undefined'` misfires there, so is_wechat additionally requires that
//     `tt` is absent, and on Douyin both `wx` and `WXWebAssembly` are scrubbed so that the
//     compat aliases can never silently take over an engine path.
//   * Nothing here may touch DOM APIs: this file runs before weapp-adapter.js installs the
//     window/document surface.

// ---------- namespace and platform detection ----------
var rbfx = {};
rbfx.env = {};
rbfx.env.is_wechat = (typeof wx != "undefined") && (typeof tt == "undefined");
rbfx.env.is_douyin = typeof tt != "undefined";
rbfx.env.is_browser = !rbfx.env.is_wechat && !rbfx.env.is_douyin && (typeof window !== "undefined");
rbfx.env.name =
    rbfx.env.is_wechat ? "wechat" :
    rbfx.env.is_douyin ? "douyin" :
    rbfx.env.is_browser ? "browser" :
    "unknown";

if (rbfx.env.is_wechat) {
    var wechat_device_info = wx.getDeviceInfo();
    if (wechat_device_info === undefined) {
        throw Error("cannot read device info; is the WeChat devtools logged in?");
    }
    rbfx.env.is_devtools = wechat_device_info.platform === "devtools";
} else if (rbfx.env.is_douyin) {
    rbfx.env.is_devtools = tt.getSystemInfoSync().platform === "devtools";
} else {
    rbfx.env.is_devtools = false;
}

// The namespace goes on globalThis so every later file (and the engine glue) sees the same
// object: on browsers that is `window`, in the minigame runtimes it is `GameGlobal`.
globalThis.rbfx = rbfx;

// WeChat and Douyin APIs are mostly identical; `wx_tt` resolves to the vendor object for the
// current runtime so the rest of the bootstrap never branches on the platform name.
globalThis.wx_tt = rbfx.env.is_wechat ? wx : (rbfx.env.is_douyin ? tt : null);
if (rbfx.env.is_douyin) {
    // Douyin defines `wx` (same content as tt) and `WXWebAssembly` (same content as
    // TTWebAssembly) for source compatibility. rbfx does its own platform shimming, and
    // leaving the aliases around would let feature probing route into them by accident.
    globalThis.wx = undefined;
    globalThis.WXWebAssembly = undefined;
}

// ---------- legacy-compatible variant selection ----------
// The legacy variant exists for devices whose WebAssembly implementation has no BigInt
// integration (built with URHO3D_MINIGAME_LEGACY): same engine, size-optimized, no
// i64<->BigInt marshalling. The version rule below was validated against Douyin's iOS
// WebView: iOS 14 and iOS 26.2+ still need it.
rbfx.env.is_compatible = false;
if (rbfx.env.is_wechat || rbfx.env.is_douyin) {
    var cur_system = "";
    if (rbfx.env.is_wechat) {
        cur_system = wx_tt.getDeviceInfo().system;
    } else {
        cur_system = wx_tt.getSystemInfoSync().system;
    }
    if (cur_system && cur_system.startsWith("iOS")) {
        var versionStr = cur_system.split(" ");
        if (versionStr[1]) {
            var versionParts = versionStr[1].split(".").map(Number);
            var major = versionParts[0] || 0;
            var minor = versionParts[1] || 0;
            if (major === 14 || major > 26 || (major === 26 && minor >= 2)) {
                rbfx.env.is_compatible = true;
            }
        }
    }
}
// ---------- config accessors ----------
// rbfx.game_config is defined by rbfx_game_config.js, which is loaded right after this
// file, so every accessor below resolves lazily through it.
rbfx.env.get_wasm_file = function() {
    if (rbfx.env.is_compatible && rbfx.game_config.wasm_file_compatible && rbfx.game_config.wasm_file_compatible != "") {
        return rbfx.game_config.wasm_file_compatible;
    }
    return rbfx.game_config.wasm_file;
};

// The wasm lives in a subpackage. Which one is decided by the config rather than by the
// device: a package assembled with both variants names a separate legacy subpackage, a
// package assembled with one variant points both entries at the one it carries. The
// subpackage name is the first segment of the wasm path, so the two cannot drift apart.
rbfx.env.get_subpackage_name = function() {
    var file = rbfx.env.get_wasm_file();
    var slash = file.indexOf("/");
    return slash > 0 ? file.substring(0, slash) : "package_wasm";
};

// ---------- small utilities ----------
rbfx.compareVersion = function(v1, v2) {
    v1 = v1.split(".");
    v2 = v2.split(".");
    const len = Math.max(v1.length, v2.length);
    while (v1.length < len) {
        v1.push("0");
    }
    while (v2.length < len) {
        v2.push("0");
    }
    for (let i = 0; i < len; i++) {
        const num1 = parseInt(v1[i]);
        const num2 = parseInt(v2[i]);
        if (num1 > num2) {
            return 1;
        } else if (num1 < num2) {
            return -1;
        }
    }
    return 0;
};

// Window size is reported in logical pixels; the pixel ratio is only used to convert to
// physical canvas pixels.
rbfx.env.get_screen_info = function() {
    if (rbfx.env.is_wechat) {
        const info = wx.getWindowInfo();
        return {width: info.windowWidth, height: info.windowHeight, pixel_ratio: info.pixelRatio};
    }
    if (rbfx.env.is_douyin) {
        const info = tt.getSystemInfoSync();
        return {width: info.windowWidth, height: info.windowHeight, pixel_ratio: info.pixelRatio};
    }
    const canvas = document.getElementById("canvas");
    return {
        width: canvas.clientWidth || window.innerWidth,
        height: canvas.clientHeight || window.innerHeight,
        pixel_ratio: window.devicePixelRatio || 1
    };
};
