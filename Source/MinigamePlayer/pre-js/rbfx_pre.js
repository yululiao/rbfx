// rbfx minigame platform layer, injected into the engine module with --pre-js.
//
// This file runs inside the MODULARIZE factory, after the Module integration code, so
// `Module` here is the argument object the loading manager passes to createRbfxModule()
// (that is where Module['canvas'] comes from). A browser host needs none of this; the
// vendor runtimes do:
//
//   * The engine glue resolves its canvas through DOM lookups ("#canvas" for SDL, and the
//     id lookup for the GL backend's canvas target) and through Module['canvas']. The
//     vendor canvas created by the adapter has no id, so the lookups miss it.
//   * WebAssembly.instantiate cannot load a local package file in the vendor runtimes; the
//     wasm payload lives in a subpackage and must be instantiated by the vendor loader.
//   * The compiled glue references browser globals unconditionally - WebAssembly.RuntimeError
//     in abort(), WebGLRenderingContext in the Safari getContext workaround (which is a
//     built-in default of the pinned emscripten whenever Safari is targeted at all), and
//     crypto.getRandomValues in the WASI randomFill - and vendor runtimes do not define all
//     of them.
//
// The override points used below are contractual emscripten seams: createWasm() consults
// Module['instantiateWasm'] before its default fetch/instantiate path, and the success
// callback it passes runs receiveInstance() and then the module's entry point.

(function () {
    "use strict";

    var rbfx = globalThis.rbfx;
    var wx_tt = globalThis.wx_tt;

    // ---- canvas handoff --------------------------------------------------------------------
    // SDL looks the canvas up as "#canvas" and the GL backend as "canvas"; both go through
    // the adapter's document mock, which resolves entries by comparing against canvas.id
    // (and the legacy event-target behavior in rbfx maps "#canvas" to Module['canvas']).
    // Give the vendor canvas the canonical id, and make sure Module['canvas'] is set: the
    // loading manager normally passes it through the factory argument, the lookup chain
    // below covers direct factory calls (e.g. an embedder hosting the module itself).
    var mainCanvas = Module["canvas"] ||
        (rbfx && rbfx.loading && rbfx.loading.game_canvas) ||
        (typeof window !== "undefined" && window.canvas) ||
        (typeof document !== "undefined" && document.querySelector("canvas"));
    if (mainCanvas) {
        if (mainCanvas.id !== "canvas") {
            console.log("[rbfx] assigning id 'canvas' to the vendor canvas (was: " + mainCanvas.id + ")");
            mainCanvas.id = "canvas";
        }
        if (!Module["canvas"])
            Module["canvas"] = mainCanvas;
    } else {
        console.error("[rbfx] no canvas available for the engine module");
    }

    // ---- browser globals the compiled glue expects ------------------------------------------
    // WebAssembly: the runtime may define only its vendor loader (WeChat has WXWebAssembly,
    // Douyin has the standard global and TTWebAssembly). The glue references e.g.
    // `new WebAssembly.RuntimeError` in abort(), so build a shim over the vendor loader when
    // the standard global is missing. The shim covers the error constructors, which the
    // vendor objects may not provide.
    var vendor_assembly = globalThis.TTWebAssembly || globalThis.WXWebAssembly;
    if (typeof globalThis.WebAssembly === "undefined" && vendor_assembly) {
        var WebAssembly_shim = {};
        for (var key in vendor_assembly)
            WebAssembly_shim[key] = vendor_assembly[key];
        ["RuntimeError", "CompileError", "LinkError"].forEach(function (name) {
            if (typeof WebAssembly_shim[name] === "function")
                return;
            function WasmError(message) {
                this.name = name;
                this.message = message || "";
                if (Error.captureStackTrace)
                    Error.captureStackTrace(this, WasmError);
            }
            WasmError.prototype = Object.create(Error.prototype);
            WasmError.prototype.constructor = WasmError;
            WebAssembly_shim[name] = WasmError;
        });
        globalThis.WebAssembly = WebAssembly_shim;
        console.log("[rbfx] installed a WebAssembly shim over the vendor loader");
    }

    // WebGL contexts: the glue's Safari getContext workaround tests freshly acquired contexts
    // against the WebGLRenderingContext global, which vendor runtimes may not define. An
    // empty class keeps the test safe: the engine only ever requests "webgl2" (the module is
    // built with MIN_WEBGL_VERSION=2), and a context can never be an instance of the stub, so
    // the workaround resolves to the same result it gives on Safari.
    if (typeof globalThis.WebGLRenderingContext === "undefined") {
        globalThis.WebGLRenderingContext = function WebGLRenderingContext() {};
    }

    // crypto: the WASI randomFill reads `crypto.getRandomValues` without a guard. WeChat
    // exposes its own generator; everywhere else fall back to Math.random, which is not
    // cryptographically secure but keeps engine code that seeds generators alive.
    if (typeof globalThis.crypto === "undefined" || typeof globalThis.crypto.getRandomValues !== "function") {
        var crypto_fill = null;
        if (wx_tt && rbfx && rbfx.env && rbfx.env.is_wechat && typeof wx_tt.getUserCryptoManager === "function") {
            try {
                crypto_fill = wx_tt.getUserCryptoManager();
            } catch (error) {
                console.warn("[rbfx] getUserCryptoManager failed, falling back to Math.random", error);
            }
        }
        if (!crypto_fill || typeof crypto_fill.getRandomValues !== "function") {
            crypto_fill = {
                getRandomValues: function (view) {
                    var bytes = new Uint8Array(view.buffer, view.byteOffset, view.byteLength);
                    for (var i = 0; i < bytes.length; ++i)
                        bytes[i] = Math.floor(Math.random() * 256);
                    return view;
                }
            };
        }
        globalThis.crypto = crypto_fill;
    }

    // ---- vendor-only setup ------------------------------------------------------------------
    // Everything below assumes the rbfx bootstrap ran (rbfx.env identifies the runtime). A
    // browser host keeps the default emscripten loader and needs none of it.
    if (!rbfx || !rbfx.env || rbfx.env.is_browser)
        return;

    // Wasm instantiation through the vendor loader: it resolves a package-relative path to a
    // local file inside the downloaded subpackage, which the fetch-based default path cannot
    // reach. createWasm() calls this before its own instantiation attempt.
    Module["instantiateWasm"] = function (imports, successCallback) {
        var loading = rbfx.loading;
        if (loading)
            loading.onEngineCompileBegin();

        var wasm_file = rbfx.env.get_wasm_file();
        console.log("[rbfx] instantiating engine wasm through the vendor loader: " + wasm_file);

        var vendor_loader = rbfx.env.is_wechat ? globalThis.WXWebAssembly : globalThis.TTWebAssembly;
        if (!vendor_loader) {
            var missing = new Error("the vendor WebAssembly loader is missing (runtime: " + rbfx.env.name + ")");
            console.error("[rbfx]", missing);
            if (loading)
                loading.onEngineFactoryFailed(missing);
            return {};
        }

        vendor_loader.instantiate(wasm_file, imports).then(function (result) {
            // Vendors differ in the promise payload: some resolve like the standard API
            // ({instance, module}), others resolve to a bare instance.
            var instance = result && result.instance ? result.instance : result;
            if (loading)
                loading.onEngineCompileEnd();
            // The emscripten seam: the callback runs receiveInstance() and then the module
            // entry point. The second argument is only read by threaded builds.
            successCallback(instance, result && result.module);
        }).catch(function (error) {
            console.error("[rbfx] engine wasm instantiation failed", error);
            if (loading)
                loading.onEngineFactoryFailed(error);
        });

        // The instance is produced asynchronously; returning an empty object is the documented
        // idiom for "no exports yet, wait for the success callback".
        return {};
    };

    // Let the runtime reclaim memory on pressure: the engine is long-running, and on
    // constrained devices the vendor's GC hint is the difference between a frame hitch and a
    // kill. The loading screen also benefits during the boot window.
    if (wx_tt && typeof wx_tt.onMemoryWarning === "function") {
        wx_tt.onMemoryWarning(function (res) {
            console.warn("[rbfx] runtime memory warning, level=" + (res ? res.level : "?"));
            if (typeof wx_tt.triggerGC === "function")
                wx_tt.triggerGC();
        });
    }

    // Surface fatal aborts: after the engine is up there is no console for the player, so
    // leave a trace on the screen. onAbort is additive - emscripten still throws afterwards.
    Module["onAbort"] = function (what) {
        console.error("[rbfx] module aborted:", what);
        // The log mirror flushes on a timer; an abort tears the runtime down long before the
        // timeout fires, so push whatever is buffered to the file synchronously.
        if (rbfx && rbfx.runtime && typeof rbfx.runtime.flushLog === "function")
            rbfx.runtime.flushLog();
        if (wx_tt && wx_tt.showModal)
            wx_tt.showModal({content: "游戏引擎运行时错误，请重新进入游戏", showCancel: false});
    };
})();
