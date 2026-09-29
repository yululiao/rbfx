// rbfx minigame loading manager. Owns the boot window between "the runtime started game.js"
// and "the engine renders its first frame":
//
//   1. paints the splash/progress screen while the subpackages (engine and data) download;
//   2. waits for the module factory (the pre-js platform layer reports compile progress);
//   3. instantiates the engine module and keeps painting until the engine reports its first
//      frame, then releases the canvas.
//
// The loading screen shares the main canvas and its WebGL2 context with the engine: the
// engine's graphics subsystem attaches to the very same context later. The context is
// therefore created here first and owned by whoever paints; the loading screen composites
// its 2D canvas on top of it with a single textured quad per vendor frame.
//
// Progress model: the bar splits into a pre-engine phase - download (40%) and compile (60%)
// weighted together into pre_engine_init_rate of the bar - and an engine initialization
// phase taking the rest. The engine reports its own progress through setEngineInitStatus().

class RbfxBlitter {
    // Draws a 2D canvas as a full-screen textured quad. The engine later shares this GL
    // context, so every piece of state this class touches is set explicitly on every blit:
    // nothing may be assumed about the current GL state.
    constructor(gl) {
        this.gl = gl;

        const vertexSource = [
            "attribute vec2 a_position;",
            "attribute vec2 a_texcoord;",
            "varying vec2 v_texcoord;",
            "void main() {",
            "    gl_Position = vec4(a_position, 0.0, 1.0);",
            "    v_texcoord = a_texcoord;",
            "}"
        ].join("\n");

        const fragmentSource = [
            "precision mediump float;",
            "varying vec2 v_texcoord;",
            "uniform sampler2D u_texture;",
            "void main() {",
            "    gl_FragColor = texture2D(u_texture, v_texcoord);",
            "}"
        ].join("\n");

        this.program = this._create_program(vertexSource, fragmentSource);
        this.texture = null;

        const positions = new Float32Array([-1, -1, 1, -1, -1, 1, 1, 1]);
        const texcoords = new Float32Array([0, 1, 1, 1, 0, 0, 1, 0]);

        this.position_buffer = gl.createBuffer();
        gl.bindBuffer(gl.ARRAY_BUFFER, this.position_buffer);
        gl.bufferData(gl.ARRAY_BUFFER, positions, gl.STATIC_DRAW);

        this.texcoord_buffer = gl.createBuffer();
        gl.bindBuffer(gl.ARRAY_BUFFER, this.texcoord_buffer);
        gl.bufferData(gl.ARRAY_BUFFER, texcoords, gl.STATIC_DRAW);

        this.position_location = gl.getAttribLocation(this.program, "a_position");
        this.texcoord_location = gl.getAttribLocation(this.program, "a_texcoord");
        this.texture_location = gl.getUniformLocation(this.program, "u_texture");
    }

    _create_program(vertexSource, fragmentSource) {
        const gl = this.gl;

        const compile = (type, source) => {
            const shader = gl.createShader(type);
            gl.shaderSource(shader, source);
            gl.compileShader(shader);
            if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS)) {
                console.error("[rbfx] shader compile error:", gl.getShaderInfoLog(shader));
                gl.deleteShader(shader);
                return null;
            }
            return shader;
        };

        const vertexShader = compile(gl.VERTEX_SHADER, vertexSource);
        const fragmentShader = compile(gl.FRAGMENT_SHADER, fragmentSource);
        if (!vertexShader || !fragmentShader)
            throw new Error("failed to compile the loading blitter shaders");

        const program = gl.createProgram();
        gl.attachShader(program, vertexShader);
        gl.attachShader(program, fragmentShader);
        gl.linkProgram(program);
        gl.deleteShader(vertexShader);
        gl.deleteShader(fragmentShader);
        if (!gl.getProgramParameter(program, gl.LINK_STATUS)) {
            console.error("[rbfx] program link error:", gl.getProgramInfoLog(program));
            gl.deleteProgram(program);
            throw new Error("failed to link the loading blitter program");
        }
        return program;
    }

    blit(canvas2d) {
        const gl = this.gl;

        // The engine may have rendered before us in the same frame; restore the default
        // framebuffer and the full viewport, then set every drawing state this quad needs.
        gl.bindFramebuffer(gl.FRAMEBUFFER, null);
        gl.viewport(0, 0, gl.drawingBufferWidth, gl.drawingBufferHeight);
        gl.disable(gl.DEPTH_TEST);
        gl.disable(gl.CULL_FACE);
        gl.disable(gl.STENCIL_TEST);
        gl.disable(gl.SCISSOR_TEST);

        gl.useProgram(this.program);
        gl.enable(gl.BLEND);
        gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);

        gl.bindBuffer(gl.ARRAY_BUFFER, this.position_buffer);
        gl.enableVertexAttribArray(this.position_location);
        gl.vertexAttribPointer(this.position_location, 2, gl.FLOAT, false, 0, 0);

        gl.bindBuffer(gl.ARRAY_BUFFER, this.texcoord_buffer);
        gl.enableVertexAttribArray(this.texcoord_location);
        gl.vertexAttribPointer(this.texcoord_location, 2, gl.FLOAT, false, 0, 0);

        gl.activeTexture(gl.TEXTURE0);
        if (!this.texture)
            this.texture = gl.createTexture();
        gl.bindTexture(gl.TEXTURE_2D, this.texture);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
        gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, canvas2d);

        gl.uniform1i(this.texture_location, 0);
        gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);
    }

    destroy() {
        const gl = this.gl;
        if (this.texture)
            gl.deleteTexture(this.texture);
        gl.deleteBuffer(this.position_buffer);
        gl.deleteBuffer(this.texcoord_buffer);
        gl.deleteProgram(this.program);
        this.texture = null;
    }
}

class RbfxLoading {
    constructor() {
        // The host canvas: the vendor runtime hands out its own; a browser host uses the page
        // element (rbfx_pre.js gives the engine that same element, the canonical id included).
        this.game_canvas = (typeof canvas !== "undefined" && canvas) ? canvas
            : (globalThis.wx_tt ? wx_tt.createCanvas() : document.querySelector("canvas"));
        if (!this.game_canvas) {
            console.error("[rbfx] no canvas found; browser hosts need a <canvas> element on the page");
            throw new Error("A canvas is required to start the game");
        }

        // The engine's GL backend needs WebGL2 (the module is built with FULL_ES3). This
        // context is the one the engine attaches to later; on some vendors WebGL2 additionally
        // requires a backend whitelist, so failing here deserves an explicit message.
        this.gl = this.game_canvas.getContext("webgl2", {
            alpha: false,
            antialias: false,
            depth: true,
            stencil: false,
            premultipliedAlpha: true,
            preserveDrawingBuffer: false,
            powerPreference: "high-performance"
        });
        if (!this.gl) {
            console.error("[rbfx] failed to create a WebGL2 context");
            if (globalThis.wx_tt && wx_tt.showModal)
                wx_tt.showModal({title: "错误", content: "无法创建 WebGL2 上下文，请升级客户端到最新版本", showCancel: false});
            throw new Error("WebGL2 is required");
        }

        this.blitter = new RbfxBlitter(this.gl);

        // Off-screen 2D canvas the loading screen is painted into; the blitter puts it on
        // screen. The main canvas keeps the WebGL2 context created above.
        if (globalThis.wx_tt)
            this.loading_canvas = wx_tt.createCanvas();
        else if (typeof OffscreenCanvas !== "undefined")
            this.loading_canvas = new OffscreenCanvas(1, 1);
        else
            this.loading_canvas = document.createElement("canvas");
        this.loading_ctx = this.loading_canvas.getContext("2d");

        this._resize_canvas();

        const config = rbfx.loading_config;
        this.background_img = config.image ? new Image() : null;
        if (this.background_img)
            this.background_img.src = config.image;   // starts loading asynchronously
        this.splash_img = config.splash ? new Image() : null;
        if (this.splash_img)
            this.splash_img.src = config.splash;

        this.splash_start_time = 0;
        this.downloadRate = 0;         // merged subpackage download progress, 0..1
        this.subpackages = [];         // per-subpackage download bookkeeping (see _load_engine)
        this.pending_factory = null;   // engine factory that arrived before the downloads finished
        this.compile_done = false;     // wasm instantiation finished
        this.engine_init_rate = 0;     // engine-reported initialization progress, 0..1
        this.engine_started = false;   // the module factory has been called
        this.info = "启动中";
        this.last_info = null;
        this.frame_count = 0;
        this.done = false;
        this.pre_engine_init_rate = 0.1;   // bar share of the download+compile phase
    }

    _resize_canvas() {
        const info = rbfx.env.get_screen_info();
        const pixel_ratio = info.pixel_ratio || 1;
        const width = Math.max(1, Math.floor(info.width * pixel_ratio));
        const height = Math.max(1, Math.floor(info.height * pixel_ratio));
        if (this.game_canvas.width !== width || this.game_canvas.height !== height) {
            this.game_canvas.width = width;
            this.game_canvas.height = height;
        }
        this.loading_canvas.width = width;
        this.loading_canvas.height = height;
    }

    /// Draw an image covering the whole canvas, keeping its aspect ratio (the overflowing
    /// edge is cropped). Returns false when the image is not usable and the caller should
    /// paint the background color instead.
    _draw_cover(ctx, img, width, height) {
        if (!(img && img.complete && img.width > 0))
            return false;

        const img_ratio = img.width / img.height;
        const canvas_ratio = width / height;
        let draw_width, draw_height, offset_x, offset_y;
        if (img_ratio > canvas_ratio) {
            draw_height = height;
            draw_width = height * img_ratio;
            offset_x = (width - draw_width) / 2;
            offset_y = 0;
        } else {
            draw_width = width;
            draw_height = width / img_ratio;
            offset_x = 0;
            offset_y = (height - draw_height) / 2;
        }
        ctx.drawImage(img, offset_x, offset_y, draw_width, draw_height);
        return true;
    }

    _draw_splash() {
        const ctx = this.loading_ctx;
        const width = this.loading_canvas.width;
        const height = this.loading_canvas.height;
        if (!this._draw_cover(ctx, this.splash_img, width, height)) {
            ctx.fillStyle = rbfx.loading_config.background_style_when_no_image;
            ctx.fillRect(0, 0, width, height);
        }
    }

    _draw_progress() {
        const ctx = this.loading_ctx;
        const width = this.loading_canvas.width;
        const height = this.loading_canvas.height;
        const config = rbfx.loading_config;

        if (!this._draw_cover(ctx, this.background_img, width, height)) {
            ctx.fillStyle = config.background_style_when_no_image;
            ctx.fillRect(0, 0, width, height);
        }

        const pre_engine_rate =
            (this.downloadRate * 0.4 + (this.compile_done ? 1 : 0) * 0.6) * this.pre_engine_init_rate;
        const rate = Math.min(1, pre_engine_rate + this.engine_init_rate * (1 - this.pre_engine_init_rate));

        const scale = Math.max(width, height) / config.reference_resolution_long_edge;
        const bar_width = width * config.process_bar_width_rate;
        const bar_height = Math.max(4, config.process_bar_height * scale);
        const bar_left = (width - bar_width) / 2;
        const bar_top = height * config.process_bar_top_rate;
        const font = Math.max(12, config.font_size * scale) + "px sans-serif";

        // Bar background.
        const background = ctx.createLinearGradient(bar_left, 0, bar_left + bar_width, 0);
        background.addColorStop(0, config.process_bar_background_left_color);
        background.addColorStop(1, config.process_bar_background_right_color);
        ctx.fillStyle = background;
        ctx.fillRect(bar_left, bar_top, bar_width, bar_height);

        // Current progress on top of it.
        const current_width = bar_width * rate;
        if (current_width > 0) {
            const current = ctx.createLinearGradient(bar_left, 0, bar_left + current_width, 0);
            current.addColorStop(0, config.process_bar_current_left_color);
            current.addColorStop(1, config.process_bar_current_right_color);
            ctx.fillStyle = current;
            ctx.fillRect(bar_left, bar_top, current_width, bar_height);
        }

        // Percentage above the bar.
        ctx.fillStyle = config.text_style;
        ctx.font = font;
        ctx.textAlign = "center";
        ctx.textBaseline = "bottom";
        ctx.fillText(Math.floor(rate * 100) + "%", width / 2, bar_top - bar_height / 2);

        // Status line below the bar; while the status is unchanged, trailing dots animate
        // with symmetric leading spaces so the text stays centered.
        let info_text = this.info;
        if (this.last_info === this.info) {
            const dots = Math.floor((Date.now() / 500) % 4);
            info_text = " ".repeat(dots) + this.info + ".".repeat(dots);
        } else {
            this.last_info = this.info;
        }
        ctx.textBaseline = "top";
        ctx.fillText(info_text, width / 2, bar_top + bar_height + bar_height / 2);
    }

    draw() {
        // The engine may resize the canvas while initializing; keep the 2D canvas in sync
        // with whatever buffer size the main canvas currently has.
        if (this.loading_canvas.width !== this.game_canvas.width ||
            this.loading_canvas.height !== this.game_canvas.height) {
            this.loading_canvas.width = this.game_canvas.width;
            this.loading_canvas.height = this.game_canvas.height;
        }

        if (!this.splash_start_time)
            this.splash_start_time = Date.now();
        const within_splash = rbfx.loading_config.splash_time > 0 &&
            (Date.now() - this.splash_start_time < rbfx.loading_config.splash_time * 1000);

        if (within_splash)
            this._draw_splash();
        else
            this._draw_progress();

        this.blitter.blit(this.loading_canvas);
    }

    run() {
        if (rbfx.env.is_wechat && rbfx.game_config.enable_debug && typeof wx.setEnableDebug === "function")
            wx.setEnableDebug({enableDebug: true});

        this._install_update_hooks();
        this._load_engine();

        const frame = () => {
            if (this.done)
                return;
            ++this.frame_count;
            this.draw();
            requestAnimationFrame(frame);
        };
        requestAnimationFrame(frame);
    }

    /// Vendor-side update handling; a project that wants its own update UI can turn it off
    /// with rbfx.game_config.enable_check_update and call the vendor API itself.
    _install_update_hooks() {
        const wx_tt = globalThis.wx_tt;
        if (!wx_tt || !rbfx.game_config.enable_check_update || typeof wx_tt.getUpdateManager !== "function")
            return;

        const update_manager = wx_tt.getUpdateManager();
        update_manager.onCheckForUpdate((res) => {
            console.log("[rbfx] update check:", res.hasUpdate ? "new version found, downloading in background" : "up to date");
            if (res.hasUpdate)
                wx_tt.showToast({icon: "loading", title: "后台更新中……", duration: 1500});
        });
        update_manager.onUpdateReady(() => {
            wx_tt.showModal({
                title: "更新提示",
                content: "新版本已经准备好，是否重启小游戏？",
                success: (res) => {
                    if (res.confirm)
                        update_manager.applyUpdate();
                }
            });
        });
        update_manager.onUpdateFailed((err) => {
            console.error("[rbfx] update failed:", err);
            wx_tt.showModal({title: "更新失败", content: "版本更新失败，请重新进入小游戏重试", showCancel: false});
        });
    }

    _load_engine() {
        if (rbfx.env.is_browser) {
            // A browser host has no subpackages; the embedder supplies the module factory.
            return;
        }

        // The engine module and, when the build put the game files in one, the data bundle
        // download concurrently. Nothing instantiates the engine until both are local: the
        // C++ host reads Data/Game.json before its first frame, so starting earlier would
        // surface as a failed file read instead of a download that is still running.
        const engine_subpackage = rbfx.env.get_subpackage_name();
        this.subpackages = [{name: engine_subpackage, engine: true, rate: 0, done: false}];
        const data_subpackage = rbfx.game_config.data_subpackage || "";
        if (data_subpackage && data_subpackage !== engine_subpackage)
            this.subpackages.push({name: data_subpackage, engine: false, rate: 0, done: false});

        this.info = "下载引擎";
        console.log("[rbfx] downloading subpackages: " +
            this.subpackages.map((entry) => entry.name).join(", "));

        for (const entry of this.subpackages)
            this._download_subpackage(entry);
    }

    /// Download one subpackage, with a small retry policy: a failed download of a shipping
    /// build is a flat failure, so it is retried before the player is told about it.
    _download_subpackage(entry) {
        const max_attempts = 3;
        let attempt = 0;
        const load = () => {
            ++attempt;
            const task = wx_tt.loadSubpackage({
                name: entry.name,
                success: () => {
                    console.log("[rbfx] subpackage ready: " + entry.name);
                    entry.done = true;
                    entry.rate = 1;
                    this._on_subpackage_download();
                },
                fail: (res) => {
                    if (attempt < max_attempts) {
                        console.warn("[rbfx] subpackage '" + entry.name + "' download failed, retrying ("
                            + (attempt + 1) + "/" + max_attempts + ")", res);
                        entry.rate = 0;
                        setTimeout(load, 1000);
                    } else {
                        console.error("[rbfx] subpackage '" + entry.name + "' download failed, giving up", res);
                        wx_tt.showModal({
                            content: entry.engine ? "游戏引擎下载失败，请检查网络后重新进入游戏"
                                : "游戏资源下载失败，请检查网络后重新进入游戏",
                            showCancel: false
                        });
                    }
                }
            });

            let last_log = 0;
            task.onProgressUpdate((res) => {
                const rate = res.totalBytesExpectedToWrite > 0
                    ? res.totalBytesWritten / res.totalBytesExpectedToWrite : 0;
                // Real devices can report a zero/overflowed pair right before finishing;
                // clamping to the maximum observed rate keeps the bar from rolling back.
                if (rate > entry.rate)
                    entry.rate = rate;
                this._on_subpackage_download();
                const now = Date.now();
                if (now - last_log >= 300 || res.totalBytesWritten === res.totalBytesExpectedToWrite) {
                    last_log = now;
                    console.log("[rbfx] downloading " + entry.name + " ... " + (rate * 100).toFixed(1) + "%");
                }
            });
        };
        load();
    }

    /// Merge every download into the single rate the bar shows, and hand a factory that
    /// arrived early to the instantiation step once the last subpackage landed.
    _on_subpackage_download() {
        let rate = 0;
        let done = 0;
        for (const entry of this.subpackages) {
            rate += entry.rate;
            if (entry.done)
                ++done;
        }
        this.downloadRate = this.subpackages.length ? rate / this.subpackages.length : 1;
        if (done === this.subpackages.length && this.pending_factory)
            this._instantiate_engine(this.pending_factory);
    }

    // ---------- interface for the subpackage entry and the pre-js platform layer ----------

    /// Called by the subpackage's game.js once the MODULARIZE factory (createRbfxModule) is
    /// defined. The loading manager owns the canvas and calls the factory itself - but only
    /// after every subpackage finished downloading, so the engine never races a download.
    onEngineFactoryReady(factory) {
        if (this.done || this.engine_started)
            return;

        for (const entry of this.subpackages) {
            if (!entry.done) {
                // The wasm subpackage's own game.js ran when its download finished, so the
                // factory normally arrives while the data subpackage is still on the wire.
                // Keep it; _on_subpackage_download starts the engine instead.
                console.log("[rbfx] engine module factory is ready, waiting for the remaining subpackages");
                this.pending_factory = factory;
                return;
            }
        }
        this._instantiate_engine(factory);
    }

    _instantiate_engine(factory) {
        if (this.done || this.engine_started)
            return;
        this.pending_factory = null;
        this.engine_started = true;
        this.downloadRate = 1.0;
        this.info = "编译引擎";

        console.log("[rbfx] engine module factory ready, instantiating");
        try {
            const result = factory({canvas: this.game_canvas});
            if (result && typeof result.catch === "function")
                result.catch((error) => this.onEngineFactoryFailed(error));
        } catch (error) {
            this.onEngineFactoryFailed(error);
        }
    }

    onEngineFactoryFailed(error) {
        console.error("[rbfx] engine module failed to start", error);
        if (globalThis.wx_tt && wx_tt.showModal)
            wx_tt.showModal({content: "游戏引擎启动失败，请重新进入游戏", showCancel: false});
    }

    /// Reported by the pre-js platform layer around the wasm compilation step.
    onEngineCompileBegin() {
        this.info = "编译引擎";
    }

    onEngineCompileEnd() {
        this.compile_done = true;
        this.info = "初始化引擎";
    }

    /// Reported by the engine as it initializes; finished=true means the engine has rendered
    /// its first frame and the loading screen can hand the canvas over for good.
    setEngineInitStatus(finished, rate) {
        if (this.done)
            return;
        if (typeof rate === "number")
            this.engine_init_rate = Math.max(this.engine_init_rate, Math.min(1, rate));
        if (finished)
            this._finish();
    }

    getCurrentFrame() {
        return this.frame_count;
    }

    _finish() {
        this.done = true;
        if (this.blitter) {
            this.blitter.destroy();
            this.blitter = null;
        }
        this.loading_ctx = null;
        this.loading_canvas = null;
        // The engine owns the canvas from here on; drop the reference so nothing in the
        // loading path can touch it again.
        this.game_canvas = null;
        if (globalThis.wx_tt && typeof wx_tt.triggerGC === "function")
            wx_tt.triggerGC();
        console.info("[rbfx] engine initialization finished, loading screen released");
    }
}

globalThis.rbfx.loading = new RbfxLoading();
globalThis.rbfx.loading.run();
