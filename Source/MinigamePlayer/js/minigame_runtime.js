// rbfx minigame runtime layer. Loaded right after the adapter, before anything asynchronous
// starts, so the console wrapping below captures the entire boot log.
//
// Responsibilities:
//   * rbfx.runtime - the primitives the engine calls through EM_JS: the user data directory
//     path and the host exit request;
//   * host lifecycle -> rbfx.native - onShow/onHide/memory warning are forwarded as native
//     hook calls. The native side publishes its closures on rbfx.native only once the engine
//     starts, so every call looks the closure up at invocation time; events that fire before
//     the engine exists are dropped, which is correct (there is nothing to notify yet);
//   * the WebAudio shim - the engine probes the browser `AudioContext` global while the vendor
//     runtime offers a context factory instead; the two are bridged here, and the contexts are
//     suspended/resumed alongside the foreground state;
//   * the log mirror - engine output goes to the console, whose history the host keeps for a
//     short window only, so a copy is appended to the user data directory for post-mortem
//     inspection.
//
// None of this may throw into the host: every path is defensive, the mirror disables itself
// silently when the storage is unavailable.

(function () {
    var rbfx = globalThis.rbfx;
    var wx_tt = globalThis.wx_tt;

    // ---------- runtime primitives (called from the engine through EM_JS) ----------

    rbfx.runtime = {
        /// The vendor path of the per-game writable directory, or an empty string.
        userDataPath: function () {
            return (wx_tt && wx_tt.env && wx_tt.env.USER_DATA_PATH) || "";
        },

        /// Ask the host to close the game; browsers fall back to window.close().
        exit: function (code) {
            if (wx_tt && typeof wx_tt.exitMiniProgram === "function") {
                wx_tt.exitMiniProgram({});
                return;
            }
            if (typeof window !== "undefined" && typeof window.close === "function")
                window.close();
        }
    };

    // ---------- WebAudio shim ----------
    // The engine's audio backend is written against the browser WebAudio API: it probes the bare
    // `AudioContext` global to decide whether an audio device exists, constructs the context, and
    // drives it through `createScriptProcessor`. Vendor runtimes expose the same functionality
    // behind a factory instead of a global constructor, so alias one onto the other. Without the
    // shim the engine starts muted with "no audio device"; with an alias over an unusable
    // context it would abort mid-initialization, which is why the factory is probed up front.

    var audio_contexts = [];

    function is_vendor_audio_context(context) {
        // The output path needs a script processor node wired into the destination; a context
        // missing either cannot back the backend and must degrade to "no audio device".
        return !!context &&
            typeof context.createScriptProcessor === "function" &&
            typeof context.destination !== "undefined";
    }

    function suspend_audio() {
        for (var i = 0; i < audio_contexts.length; i++) {
            try {
                var context = audio_contexts[i];
                if (context.state === "running" && typeof context.suspend === "function")
                    context.suspend();
            } catch (error) { }
        }
    }

    function resume_audio() {
        for (var i = 0; i < audio_contexts.length; i++) {
            try {
                var context = audio_contexts[i];
                if (context.state === "suspended" && typeof context.resume === "function")
                    context.resume();
            } catch (error) { }
        }
    }

    (function () {
        if (typeof globalThis.AudioContext !== "undefined" || !wx_tt)
            return;
        if (typeof wx_tt.createWebAudioContext !== "function") {
            console.warn("[rbfx] the vendor runtime has no createWebAudioContext; the engine will start without an audio device");
            return;
        }

        // Probe eagerly: later the constructor is called from inside the engine glue, where a
        // throw would take the whole module down. The probed context goes to the first consumer.
        var first_context = null;
        try {
            var probe = wx_tt.createWebAudioContext();
            if (is_vendor_audio_context(probe))
                first_context = probe;
            else if (probe && typeof probe.close === "function")
                probe.close();
        } catch (error) {
            console.warn("[rbfx] probing the vendor WebAudio context failed", error);
        }
        if (!first_context) {
            console.warn("[rbfx] the vendor WebAudio context cannot back the engine audio path; the engine will start without an audio device");
            return;
        }

        // Keeps the engine alive in silence if a later context creation fails, instead of
        // throwing from inside the glue.
        function create_inert_context() {
            return {
                state: "running",
                sampleRate: 44100,
                destination: {},
                resume: function () { },
                suspend: function () { },
                close: function () { },
                createScriptProcessor: function () {
                    return { onaudioprocess: null, connect: function () { }, disconnect: function () { } };
                }
            };
        }

        // The engine glue reads `state` around resume/suspend; fill in members a vendor may omit
        // so they degrade to no-ops instead of throwing mid-initialization.
        function normalize_context(context) {
            try {
                if (typeof context.resume !== "function")
                    context.resume = function () { };
                if (typeof context.suspend !== "function")
                    context.suspend = function () { };
            } catch (error) { }
            return context;
        }

        // Called by the engine glue as a constructor; returning an object from a constructor
        // hands that object back, which is how the vendor context surfaces through `new`.
        function RbfxAudioContext() {
            var context = null;
            if (first_context) {
                context = first_context;
                first_context = null;
            } else {
                try {
                    context = wx_tt.createWebAudioContext();
                } catch (error) {
                    console.error("[rbfx] createWebAudioContext failed", error);
                }
            }
            if (!is_vendor_audio_context(context)) {
                console.error("[rbfx] the vendor WebAudio context is not usable; running the audio device in silence");
                context = create_inert_context();
            } else {
                normalize_context(context);
            }
            audio_contexts.push(context);
            return context;
        }

        globalThis.AudioContext = RbfxAudioContext;
        globalThis.webkitAudioContext = RbfxAudioContext;
        console.log("[rbfx] installed the WebAudio shim over the vendor context factory");
    })();

    // ---------- host lifecycle -> rbfx.native ----------

    function call_native(name, args) {
        var native = rbfx.native;
        if (native && typeof native[name] === "function") {
            try {
                native[name].apply(null, args);
            } catch (error) {
                console.error("[rbfx] native hook " + name + " failed", error);
            }
        }
    }

    if (wx_tt) {
        if (typeof wx_tt.onShow === "function")
            wx_tt.onShow(function () { resume_audio(); call_native("on_show", []); });
        if (typeof wx_tt.onHide === "function")
            wx_tt.onHide(function () { suspend_audio(); call_native("on_hide", []); });
        if (typeof wx_tt.onMemoryWarning === "function")
            wx_tt.onMemoryWarning(function (res) { call_native("on_memory_warning", [(res && res.level) | 0]); });
    } else if (typeof document !== "undefined" && typeof document.addEventListener === "function") {
        // Browser host: page visibility stands in for the foreground/background callbacks.
        document.addEventListener("visibilitychange", function () {
            call_native(document.hidden ? "on_hide" : "on_show", []);
        });
    }

    // The rendering context may be lost at any time (some drivers drop it when the game is
    // backgrounded); the engine must know before it paints into a dead context. The adapter
    // wires canvas.addEventListener onto the document event target.
    (function () {
        var game_canvas = globalThis.canvas;
        if (!game_canvas || typeof game_canvas.addEventListener !== "function")
            return;
        game_canvas.addEventListener("webglcontextlost", function (event) {
            if (event && typeof event.preventDefault === "function")
                event.preventDefault();
            call_native("on_context_lost", []);
        });
    })();

    // ---------- log mirror ----------

    var LOG_FILE_NAME = "rbfx_log.txt";
    var LOG_FLUSH_INTERVAL_MS = 1000;
    var LOG_MAX_BYTES = 512 * 1024;      // beyond this the file is rewritten from scratch
    var LOG_BUFFER_LIMIT = 1024;         // lines held in memory at most

    var log_buffer = [];
    var log_file_size = -1;              // unknown until the first flush
    var log_flush_scheduled = false;

    function timestamp() {
        var now = new Date();
        var pad = function (value, width) {
            var text = String(value);
            while (text.length < width)
                text = "0" + text;
            return text;
        };
        return pad(now.getHours(), 2) + ":" + pad(now.getMinutes(), 2) + ":" + pad(now.getSeconds(), 2) +
            "." + pad(now.getMilliseconds(), 3);
    }

    function format_log_argument(value) {
        if (typeof value === "string")
            return value;
        if (value instanceof Error)
            return value.stack || (value.name + ": " + value.message);
        try {
            var text = JSON.stringify(value);
            if (typeof text === "string")
                return text;
        } catch (error) { }
        try {
            return String(value);
        } catch (error) {
            return "[unprintable]";
        }
    }

    function schedule_log_flush() {
        if (log_flush_scheduled)
            return;
        log_flush_scheduled = true;
        setTimeout(flush_log, LOG_FLUSH_INTERVAL_MS);
    }

    function flush_log() {
        log_flush_scheduled = false;
        if (log_buffer.length === 0)
            return;

        var text = log_buffer.join("\n") + "\n";
        log_buffer.length = 0;

        var manager = (wx_tt && typeof wx_tt.getFileSystemManager === "function")
            ? wx_tt.getFileSystemManager() : null;
        var directory = rbfx.runtime.userDataPath();
        if (!manager || !directory)
            return;

        var file_path = directory + "/" + LOG_FILE_NAME;
        try {
            if (log_file_size < 0) {
                try {
                    log_file_size = manager.statSync(file_path).size || 0;
                } catch (error) {
                    log_file_size = 0;
                }
            }
            if (log_file_size + text.length > LOG_MAX_BYTES) {
                manager.writeFileSync(file_path, text, "utf8");
                log_file_size = text.length;
            } else {
                manager.appendFileSync(file_path, text, "utf8");
                log_file_size += text.length;
            }
        } catch (error) {
            // The mirror is optional; losing entries must not cascade into the game.
        }
    }

    // Exposed for the platform layer: rbfx_pre.js calls this synchronously when the module
    // aborts, because the timer-scheduled flush would never fire while the host tears down.
    rbfx.runtime.flushLog = flush_log;

    function mirror_log(line) {
        log_buffer.push(line);
        if (log_buffer.length > LOG_BUFFER_LIMIT)
            log_buffer.splice(0, log_buffer.length - LOG_BUFFER_LIMIT);
        schedule_log_flush();
    }

    // Wrap the console methods: the original output is preserved, a stamped copy goes into
    // the mirror. This also captures everything the engine prints, since the Emscripten glue
    // routes stdout/stderr to console.
    (function () {
        if (typeof console === "undefined")
            return;
        var methods = ["log", "info", "warn", "error"];
        for (var i = 0; i < methods.length; i++) {
            (function (name) {
                var original = console[name];
                if (typeof original !== "function")
                    return;
                console[name] = function () {
                    try {
                        var parts = [];
                        for (var arg = 0; arg < arguments.length; arg++)
                            parts.push(format_log_argument(arguments[arg]));
                        mirror_log("[" + timestamp() + "][" + name + "] " + parts.join(" "));
                    } catch (error) { }
                    return original.apply(console, arguments);
                };
            })(methods[i]);
        }
    })();

    // Uncaught errors never reach console on some hosts; mirror them explicitly.
    if (wx_tt && typeof wx_tt.onError === "function") {
        wx_tt.onError(function (error) {
            mirror_log("[uncaught] " + ((error && (error.message || error.stack)) || String(error)));
            flush_log();
        });
    }

    // Rejected promises bypass the console the same way on some hosts; keep the reason too.
    if (wx_tt && typeof wx_tt.onUnhandledRejection === "function") {
        wx_tt.onUnhandledRejection(function (res) {
            mirror_log("[unhandled rejection] " + format_log_argument(res && res.reason));
            flush_log();
        });
    }
})();
