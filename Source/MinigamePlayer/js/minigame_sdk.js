// rbfx minigame SDK shim: publishes globalThis.rbfx.sdk.
//
// The engine side (Urho3D/Minigame/MinigameSDK.h) sends capability requests with a kind and
// a request id and expects exactly one completion per request, delivered through
// rbfx.native.sdk_complete(requestId, success, payload). The engine resolves the request
// kind on its side, so the shim only echoes the id.
//
// Every handler is designed to answer exactly once even when the host fires several
// callbacks (ad close plus ad error, share success plus fail): the guarded completion is the
// only path into sdk_complete.
//
// Projects can override individual handlers before the engine starts, e.g.
//     rbfx.sdk.handlers[rbfx.sdk.kind.payment] = function (requestId, payload) { ... };

(function () {
    var rbfx = globalThis.rbfx;
    var wx_tt = globalThis.wx_tt;

    // Mirrors MinigameSDKRequestKind in Urho3D/Minigame/MinigameSDK.h.
    var KIND = {
        login: 0,
        share: 1,
        rewarded_ad: 2,
        payment: 3,
        privacy: 4,
        record_voice: 5,
        play_user_audio: 6,
        stop_user_audio: 7,
        play_game_audio: 8,
        stop_game_audio: 9,
        clear_user_audio: 10,
        report_analytics: 11
    };

    function complete(request_id, success, payload) {
        var native = rbfx.native;
        if (!native || typeof native.sdk_complete !== "function")
            return;
        try {
            native.sdk_complete(request_id | 0, !!success, payload == null ? "" : String(payload));
        } catch (error) {
            console.error("[rbfx] sdk completion failed", error);
        }
    }

    function parse_payload(text) {
        if (!text)
            return {};
        try {
            var parsed = JSON.parse(text);
            return (parsed && typeof parsed === "object") ? parsed : {};
        } catch (error) {
            return {};
        }
    }

    function fail_text(error, fallback) {
        if (!error)
            return fallback;
        return error.errMsg || error.message || String(error);
    }

    var handlers = {};

    // ---------- login ----------

    handlers[KIND.login] = function (request_id) {
        if (!wx_tt || typeof wx_tt.login !== "function")
            return complete(request_id, false, "login is not supported by the host");
        wx_tt.login({
            success: function (res) { complete(request_id, true, JSON.stringify(res || {})); },
            fail: function (error) { complete(request_id, false, fail_text(error, "login failed")); }
        });
    };

    // ---------- share ----------
    // The payload carries { title, imageUrl, query }; absent fields are left to the host.

    handlers[KIND.share] = function (request_id, payload) {
        if (!wx_tt || typeof wx_tt.shareAppMessage !== "function")
            return complete(request_id, false, "share is not supported by the host");

        var args = parse_payload(payload);
        var call = {
            success: function () { complete(request_id, true, "{}"); },
            fail: function (error) { complete(request_id, false, fail_text(error, "share failed")); }
        };
        if (args.title)
            call.title = String(args.title);
        if (args.imageUrl)
            call.imageUrl = String(args.imageUrl);
        if (args.query)
            call.query = String(args.query);

        try {
            wx_tt.shareAppMessage(call);
        } catch (error) {
            complete(request_id, false, fail_text(error, "share failed"));
        }
    };

    // ---------- rewarded ad ----------
    // Instances are cached per ad unit: both platforms support one active show per instance,
    // so a second request while one is pending fails fast instead of interleaving callbacks.

    var ad_instances = {};

    function get_ad(ad_unit_id) {
        var entry = ad_instances[ad_unit_id];
        if (entry)
            return entry;

        var ad = wx_tt.createRewardedVideoAd({ adUnitId: ad_unit_id });
        entry = { ad: ad, pending: 0 };

        ad.onClose(function (res) {
            var request_id = entry.pending;
            entry.pending = 0;
            if (!request_id)
                return;
            // isEnded === false means the player closed early; older base libraries do not
            // report it at all, which counts as a full view.
            var rewarded = !res || res.isEnded !== false;
            complete(request_id, rewarded, JSON.stringify({ isEnded: rewarded }));
        });
        ad.onError(function (error) {
            var request_id = entry.pending;
            entry.pending = 0;
            if (!request_id)
                return;
            complete(request_id, false, fail_text(error, "ad error"));
        });

        ad_instances[ad_unit_id] = entry;
        return entry;
    }

    handlers[KIND.rewarded_ad] = function (request_id, payload) {
        if (!wx_tt || typeof wx_tt.createRewardedVideoAd !== "function")
            return complete(request_id, false, "rewarded ads are not supported by the host");

        var args = parse_payload(payload);
        var ad_unit_id = args.adUnitId ? String(args.adUnitId) : "";
        if (!ad_unit_id)
            return complete(request_id, false, "no ad unit id in the request");

        var entry = get_ad(ad_unit_id);
        if (entry.pending)
            return complete(request_id, false, "an ad is already showing");
        entry.pending = request_id;

        entry.ad.show().catch(function () {
            // Not loaded yet (typically the first show after launch): load, then show once.
            return entry.ad.load().then(function () { return entry.ad.show(); });
        }).catch(function (error) {
            if (entry.pending === request_id) {
                entry.pending = 0;
                complete(request_id, false, fail_text(error, "ad failed to show"));
            }
        });
    };

    // ---------- payment ----------
    // The payload is the vendor order object verbatim: its keys become the vendor call
    // arguments (mode/env/orderInfo/...); the game side owns that contract.

    handlers[KIND.payment] = function (request_id, payload) {
        if (!wx_tt || typeof wx_tt.requestGamePayment !== "function")
            return complete(request_id, false, "payment is not supported by the host");

        var args = parse_payload(payload);
        args.success = function (res) { complete(request_id, true, JSON.stringify(res || {})); };
        args.fail = function (error) { complete(request_id, false, fail_text(error, "payment failed")); };

        try {
            wx_tt.requestGamePayment(args);
        } catch (error) {
            complete(request_id, false, fail_text(error, "payment failed"));
        }
    };

    // ---------- privacy authorization ----------

    handlers[KIND.privacy] = function (request_id) {
        var api = (wx_tt && typeof wx_tt.requirePrivacyAuthorize === "function")
            ? wx_tt.requirePrivacyAuthorize : null;
        if (!api)
            return complete(request_id, false, "privacy authorization is not supported by the host");
        api.call(wx_tt, {
            success: function () { complete(request_id, true, "{}"); },
            fail: function (error) { complete(request_id, false, fail_text(error, "privacy authorization failed")); }
        });
    };

    // ---------- voice (record / play / stop) ----------
    // Clips live entirely on the host side: the recorder writes aac files into the
    // user data directory, playback goes through InnerAudioContext. The engine never
    // decodes these (its Sound loaders only know ogg/wav); the slot name is the only
    // contract between both worlds.
    //
    // Storage scheme (v2, after the fixed-path scheme broke on real devices twice):
    // the earlier rbfx_voice_<slot>.aac path replaced through unlink+rename failed
    // when an audio pipeline still held the file (re-record and clear both silently
    // stuck on the first clip), and same-path playback served cached bytes anyway.
    // Now every save lands under a fresh path and the slot registry below decides
    // what plays.
    //
    // aac, not mp3: the Douyin recorder rejects anything outside "aac" / "pcm" / "wav"
    // (RecorderManager.start:fail expect inputs[0].format to be one of ...), and both vendors
    // decode aac in InnerAudioContext. The extension is a hint only - the bytes are what play.

    function user_data_path() {
        return (wx_tt && wx_tt.env && wx_tt.env.USER_DATA_PATH) || "";
    }

    function voice_file_path(slot, unique) {
        var base = user_data_path() + "/rbfx_voice_" + String(slot || "default");
        return unique ? base + "_" + String(Date.now()) + ".aac" : base + ".aac";
    }

    /// Legacy suffix from the era of in-place replacement; swept for, never written again.
    var VOICE_STAGING_SUFFIX = ".new";

    /// The only compressed container both vendors' recorders accept (Douyin: aac / pcm / wav).
    var RECORD_FORMAT = "aac";

    // ---------- slot -> clip path registry ----------
    // Deleting or replacing a file that an audio pipeline still references fails on real
    // devices in ways this layer cannot diagnose, let alone fix. So the fixed-path scheme
    // is gone: every recording lands under a fresh path, and this table is the single
    // source of truth for what playback serves. A file that cannot be deleted becomes an
    // orphan on disk, never a wrong sound - the map stops pointing at it first. It also
    // sidesteps the host's same-path playback cache (a replaced clip played as its old
    // bytes). The map survives restarts through the host's storage.
    var VOICE_MAP_KEY = "rbfx_voice_slots";
    var voice_paths = {};
    var voice_gc_done = false;

    function load_voice_map() {
        try {
            var stored = wx_tt.getStorageSync(VOICE_MAP_KEY);
            if (stored && typeof stored === "object")
                voice_paths = stored;
        } catch (e) { /* a host without storage keeps the map in memory only */ }
    }

    function save_voice_map() {
        try { wx_tt.setStorageSync(VOICE_MAP_KEY, voice_paths); } catch (e) { /* see above */ }
    }

    /// Drop entries whose file is gone: the host wipes the user dir on uninstall and
    /// system cleanup passes while storage survives both.
    function prune_voice_map() {
        if (!wx_tt || typeof wx_tt.getFileSystemManager !== "function")
            return;
        var changed = false;
        try {
            var fs = wx_tt.getFileSystemManager();
            for (var slot in voice_paths) {
                try { fs.accessSync(voice_paths[slot]); }
                catch (e) { delete voice_paths[slot]; changed = true; }
            }
        } catch (e) { /* no file system: leave the map alone */ }
        if (changed)
            save_voice_map();
    }

    /// Orphan sweep, once before the first voice request: deletes rbfx_voice_* files no
    /// slot points at (replaced clips that could not be unlinked, and files left behind
    /// by the fixed-path build). Best effort - an orphan is harmless.
    function ensure_voice_gc() {
        if (voice_gc_done)
            return;
        voice_gc_done = true;
        try {
            var fs = wx_tt.getFileSystemManager();
            var names = fs.readdirSync(user_data_path()) || [];
            for (var i = 0; i < names.length; ++i) {
                var name = String(names[i]);
                if (name.indexOf("rbfx_voice_") !== 0)
                    continue;
                var live = false;
                for (var slot in voice_paths) {
                    var path = voice_paths[slot];
                    if (path && path.indexOf("/" + name) >= 0) {
                        live = true;
                        break;
                    }
                }
                if (!live) {
                    try { fs.unlinkSync(user_data_path() + "/" + name); } catch (e) { /* harmless */ }
                }
            }
        } catch (e) { /* readdir unsupported: skip the sweep */ }
    }

    load_voice_map();
    prune_voice_map();

    var recorder_manager = null;
    var recording_request = 0;
    var record_watchdog = 0;

    /// Every outcome of a recording request lands here, so one place owns the in-flight flag and
    /// the watchdog: no path can leave the game waiting on a request that will never answer.
    function settle_record(request_id, success, payload) {
        if (recording_request === request_id)
            recording_request = 0;
        dispose_record_watchdog();
        complete(request_id, success, payload);
    }

    function dispose_record_watchdog() {
        if (!record_watchdog)
            return;
        clearTimeout(record_watchdog);
        record_watchdog = 0;
    }

    /// A host that says nothing at all - no onStop, no onError, no authorize callback - would
    /// leave the game on "recording" with no way out, so settle the request here once its budget
    /// (the permission hop, the recording, the save) has run out.
    function arm_record_watchdog(request_id, budget_ms) {
        dispose_record_watchdog();
        record_watchdog = setTimeout(function () {
            record_watchdog = 0;
            if (recording_request !== request_id)
                return;
            try { if (recorder_manager) recorder_manager.stop(); } catch (error) { /* already stopped */ }
            settle_record(request_id, false, "the recorder never answered");
        }, budget_ms);
    }

    /// The recorder is one host-wide manager with callback-style listeners: create it once, and
    /// wire both outcome paths here - this is the only place a clip is written to disk.
    function get_recorder_manager() {
        if (recorder_manager)
            return recorder_manager;
        recorder_manager = wx_tt.getRecorderManager();
        recorder_manager.onStop(function (res) {
            var request_id = recording_request;
            if (!request_id)
                return;
            if (!res || !res.tempFilePath)
                return settle_record(request_id, false, "recorder returned no file");
            var slot = recorder_manager.__voice_slot || "default";
            try {
                var fs = wx_tt.getFileSystemManager();
                // A fresh path per save: nothing to overwrite, nothing for a live player to
                // hold on to. Douyin saveFile MOVES the temp file here, so the temp path
                // dies with this call either way.
                var target = voice_file_path(slot, true);
                fs.saveFile({
                    tempFilePath: res.tempFilePath,
                    filePath: target,
                    success: function (saved) {
                        voice_paths[slot] = (saved && saved.savedFilePath) || target;
                        save_voice_map();
                        // The replaced clip is already unreachable through the map. Unlink
                        // it when the host lets us; on a host that keeps a handle open it
                        // simply becomes an orphan the startup sweep collects.
                        var previous = recorder_manager.__voice_previous;
                        if (previous && previous !== voice_paths[slot]) {
                            try { fs.unlinkSync(previous); } catch (e) { /* harmless orphan */ }
                        }
                        settle_record(request_id, true,
                            JSON.stringify({ durationMs: res.duration || 0 }));
                    },
                    fail: function (error) {
                        settle_record(request_id, false, fail_text(error, "saving the clip failed"));
                    }
                });
            } catch (error) {
                settle_record(request_id, false, fail_text(error, "saving the clip failed"));
            }
        });
        recorder_manager.onError(function (error) {
            var request_id = recording_request;
            if (request_id)
                settle_record(request_id, false, record_failure(error, "recording failed"));
            else
                dispose_record_watchdog();
        });
        return recorder_manager;
    }

    /// Run one recording. The outcome arrives through onStop/onError above; start() itself is
    /// fire-and-forget, and the watchdog covers the host that says nothing back.
    function start_recording(request_id, slot, duration) {
        try {
            var manager = get_recorder_manager();
            // Remember the slot (to file the clip under) and the clip being replaced (to
            // unlink once the new one is safely registered).
            manager.__voice_slot = slot;
            manager.__voice_previous = voice_paths[slot] || null;
            recording_request = request_id;
            manager.start({ duration: duration, format: RECORD_FORMAT });
        } catch (error) {
            // A synchronous throw would otherwise leak recording_request and block every future
            // recording with "already in progress".
            recording_request = 0;
            settle_record(request_id, false, record_failure(error, "recording failed"));
        }
    }

    // ---------- microphone permission ----------
    // The recorder is started inside the request call on purpose. A press reaches this file through
    // the game's click handler, which makes it the user gesture the host wants for a microphone;
    // querying the scope first (getSetting, authorize) costs an async hop and throws that gesture
    // away, and the strict hosts then fail the recording the moment it is asked to run. So the
    // recorder asks for the microphone itself, and its answer is what the game reports.
    var RECORD_SCOPE = "scope.record";

    /// The game turns this exact wording into its "open the microphone in settings" hint.
    var RECORD_DENIED = "microphone permission denied (" + RECORD_SCOPE + ")";

    function looks_like_denial(error) {
        var text = fail_text(error, "").toLowerCase();
        // The vendors word the same denial differently depending on the call it came back from:
        // "authorize:fail auth deny" on the prompt, "operateRecorder:fail ... unauthorized" or
        // "no permission" from the recorder itself on a system level refusal.
        return text.indexOf("deny") >= 0 || text.indexOf("unauthoriz") >= 0 ||
            text.indexOf("not allowed") >= 0 || text.indexOf("permission") >= 0 ||
            text.indexOf("reject") >= 0 || text.indexOf("refuse") >= 0 ||
            text.indexOf("拒绝") >= 0 || text.indexOf("权限") >= 0 ||
            text.indexOf("未授权") >= 0 || text.indexOf("麦克风") >= 0;
    }

    /// Whatever the recorder reports is turned into the answer the game shows. A refusal is a
    /// refusal no matter which call reported it: the recorder's own "no permission" has to reach
    /// the player as the settings hint, not as a generic retry.
    function record_failure(error, fallback) {
        return looks_like_denial(error) ? RECORD_DENIED : fail_text(error, fallback);
    }

    handlers[KIND.record_voice] = function (request_id, payload) {
        if (!wx_tt || typeof wx_tt.getRecorderManager !== "function")
            return complete(request_id, false, "recording is not supported by the host");
        if (recording_request)
            return complete(request_id, false, "a recording is already in progress");

        // The clip being re-recorded is the one that was just previewed (the game
        // auto-plays it after every successful save). Release that player before the
        // save swaps the file out from under it: an instance left holding the old
        // bytes makes the swap fail or the old audio keep playing as if the new
        // recording never landed.
        stop_voice_audio();
        ensure_voice_gc();

        var args = parse_payload(payload);
        var slot = args.slot ? String(args.slot) : "default";
        var duration = Math.max(1000, Math.min(args.maxDurationMs | 0 || 3000, 60000));

        // Covers the whole road to a result: the permission prompt, the recording, the save. The
        // prompt is what makes the budget generous - a player can sit on that dialog.
        arm_record_watchdog(request_id, duration + 30000);
        start_recording(request_id, slot, duration);
    };

    /// Clearing a slot means dropping it from the map, so playback falls back to the game's
    /// own audio. The file unlink is best effort: a host that still holds the bytes keeps
    /// them, but nothing points at them anymore - the next recording or the startup sweep
    /// collects the orphan. A slot without a clip is already where the request wants it,
    /// so that is a success too; with the map as the truth, "clear failed" no longer exists.
    handlers[KIND.clear_user_audio] = function (request_id, payload) {
        if (recording_request)
            return complete(request_id, false, "a recording is in progress");
        var args = parse_payload(payload);
        var slot = args.slot ? String(args.slot) : "default";

        // Release any player still on this slot's clip before unlinking under it.
        stop_voice_audio();

        try {
            var fs = wx_tt.getFileSystemManager();
            // Legacy artifacts from the fixed-path build: swept for, never written again.
            var legacy = [voice_file_path(slot), voice_file_path(slot) + VOICE_STAGING_SUFFIX];
            for (var i = 0; i < legacy.length; ++i) {
                try { fs.unlinkSync(legacy[i]); } catch (e) { /* nothing recorded, or held */ }
            }
            var previous = voice_paths[slot];
            delete voice_paths[slot];
            save_voice_map();
            if (previous) {
                try { fs.unlinkSync(previous); } catch (e) { /* harmless orphan */ }
            }
        } catch (error) {
            // No file system at all: the map drop above still happened through storage, and
            // that alone is what playback consults, so the clear stands.
            delete voice_paths[slot];
            save_voice_map();
        }
        complete(request_id, true, JSON.stringify({ cleared: true }));
    };

    var voice_audio = null;

    /// While a user clip plays, the music bed ducks under it: a phone-mic recording is
    /// the quietest thing in the mix, and that clip IS the feature - it must be heard.
    var VOICE_DUCK_FACTOR = 0.15;
    var voice_ducking = false;

    /// Retune every looped music instance to (or back from) the ducked level. Music
    /// gains are remembered on __rbfx_gain so a retune arriving mid-duck still restores
    /// to the right level afterwards.
    function duck_music_for_voice(on) {
        if (voice_ducking === on)
            return;
        voice_ducking = on;
        for (var path in looped_audio) {
            var music = looped_audio[path];
            var gain = (music.__rbfx_gain === undefined) ? music.volume : music.__rbfx_gain;
            music.volume = on ? gain * VOICE_DUCK_FACTOR : gain;
        }
    }

    function stop_voice_audio() {
        if (!voice_audio)
            return;
        var audio = voice_audio;
        voice_audio = null;
        // Settle the running request before tearing the instance down: destroy()
        // detaches the listeners, so relying on onStop alone could drop the
        // completion of a playback that is being replaced.
        try { if (audio.__settle) audio.__settle(false, "playback stopped"); } catch (e) { /* ignore */ }
        try { audio.stop(); } catch (e) { /* already stopped */ }
        try { audio.destroy(); } catch (e) { /* already destroyed */ }
        // A replacement playback re-ducks on its own; a real teardown (stop request,
        // clear, restart) must hand the music bed back.
        duck_music_for_voice(false);
    }

    handlers[KIND.play_user_audio] = function (request_id, payload) {
        if (!wx_tt || typeof wx_tt.createInnerAudioContext !== "function")
            return complete(request_id, false, "audio playback is not supported by the host");

        var args = parse_payload(payload);
        var slot = args.slot ? String(args.slot) : "default";
        // Documented rate steps are 0.5 / 0.8 / 1.0 / 1.25 / 1.5 / 2.0 on both
        // vendors; other values get snapped by the host. Game code passes the steps.
        var rate = Number(args.rate) || 1.0;

        // A slot that was never recorded (or whose clip was cleaned up) is not a
        // playback error the game should wait on: report it immediately so the caller
        // can fall back to its canned cue on the very next frame. The map is the truth;
        // the access check catches a wiped user dir and prunes the stale entry on the spot.
        var src = voice_paths[slot];
        if (src) {
            try {
                wx_tt.getFileSystemManager().accessSync(src);
            } catch (e) {
                delete voice_paths[slot];
                save_voice_map();
                src = null;
            }
        }
        if (!src)
            return complete(request_id, false, "no clip recorded for this slot");

        // One audio instance at a time: a new playback replaces the previous one and
        // completes its request as "stopped" so no callback leaks.
        stop_voice_audio();
        var instance = wx_tt.createInnerAudioContext();
        voice_audio = instance;
        instance.src = src;
        instance.playbackRate = rate;
        // The user's own clip is the point of the feature: full volume (the game's
        // sfx/music pool always sets volume explicitly, and default is not to be
        // trusted), plus the music bed ducks under it so it is not buried in the mix.
        instance.volume = 1.0;
        var settled = false;
        var settle = function (ok, text) {
            if (settled)
                return;
            settled = true;
            complete(request_id, ok, text || "{}");
        };
        // Teardown on every ending path, natural or not: an instance left alive keeps
        // the clip's file open, which breaks the next recording's save (it replaces
        // that very file) and can make a clear of the slot silently fail.
        var finish = function () {
            if (voice_audio === instance)
                voice_audio = null;
            destroy_audio(instance);
            duck_music_for_voice(false);
        };
        instance.__settle = settle;
        instance.onEnded(function () { settle(true); finish(); });
        instance.onError(function (error) { settle(false, fail_text(error, "playback failed")); finish(); });
        duck_music_for_voice(true);
        try {
            instance.play();
        } catch (error) {
            // A synchronous throw would otherwise leak the request (never completed).
            settle(false, fail_text(error, "playback failed"));
            finish();
        }
    };

    handlers[KIND.stop_user_audio] = function (request_id) {
        stop_voice_audio();
        complete(request_id, true, "{}");
    };

    // ---------- game audio (sfx one-shots + looped music) ----------
    // The vendor runtime may offer no WebAudio at all; then the engine's Sound decoders
    // have no output to mix into and this player is the game's only audio. The packaging
    // step transcodes the ogg masters into m4a, and rbfx.files.resolve maps the
    // engine-visible path onto the physical package file for InnerAudioContext.src.

    var SFX_CAP = 8;          // concurrent one-shots; past it the oldest is stolen
    var sfx_active = [];      // one-shot instances in flight, oldest first
    var looped_audio = {};    // engine path -> instance (music)

    function destroy_audio(audio) {
        if (!audio)
            return;
        try { audio.stop(); } catch (e) { /* already stopped */ }
        try { audio.destroy(); } catch (e) { /* already destroyed */ }
    }

    function sfx_remove(audio) {
        var index = sfx_active.indexOf(audio);
        if (index >= 0)
            sfx_active.splice(index, 1);
    }

    handlers[KIND.play_game_audio] = function (request_id, payload) {
        if (!wx_tt || typeof wx_tt.createInnerAudioContext !== "function")
            return complete(request_id, false, "audio playback is not supported by the host");

        var args = parse_payload(payload);
        var path = args.path ? String(args.path) : "";
        if (!path)
            return complete(request_id, false, "no path in the request");
        var gain = Number(args.gain);
        if (!(gain >= 0))
            gain = 1.0;
        gain = Math.min(gain, 1.0);

        var src = (rbfx.files && typeof rbfx.files.resolve === "function")
            ? rbfx.files.resolve(path) : null;
        if (!src)
            return complete(request_id, false, "not in the package: " + path);

        if (args.loop) {
            // One looped instance per path: a repeat request retunes the running music
            // instead of stacking a second track, and completes immediately.
            var existing = looped_audio[path];
            if (existing) {
                // Remember the wanted gain: a retune that lands while the user's voice
                // ducks the music must still restore to THIS level afterwards.
                existing.__rbfx_gain = gain;
                existing.volume = voice_ducking ? gain * VOICE_DUCK_FACTOR : gain;
                complete(request_id, true, "{}");
                return;
            }
            var music = wx_tt.createInnerAudioContext();
            music.src = src;
            music.loop = true;
            music.__rbfx_gain = gain;
            music.volume = voice_ducking ? gain * VOICE_DUCK_FACTOR : gain;
            looped_audio[path] = music;
            music.onError(function (error) {
                if (looped_audio[path] === music)
                    delete looped_audio[path];
                destroy_audio(music);
                console.warn("[rbfx] music playback failed for " + path + ": " +
                    fail_text(error, "playback failed"));
            });
            try {
                music.play();
            } catch (error) {
                delete looped_audio[path];
                destroy_audio(music);
                return complete(request_id, false, fail_text(error, "playback failed"));
            }
            // Looped playback never "ends": the request completes on start, the
            // instance lives until stop_game_audio.
            complete(request_id, true, "{}");
            return;
        }

        // One-shot: past the cap, steal the oldest (a truncated gunshot is less wrong
        // than a silently dropped one).
        if (sfx_active.length >= SFX_CAP) {
            var oldest = sfx_active.shift();
            if (oldest.__settle) {
                try { oldest.__settle(false, "stolen by a newer playback"); } catch (e) { /* ignore */ }
            } else {
                destroy_audio(oldest);
            }
        }

        var audio = wx_tt.createInnerAudioContext();
        audio.src = src;
        audio.volume = gain;
        audio.__path = path;
        var settled = false;
        var settle = function (ok, text) {
            if (settled)
                return;
            settled = true;
            sfx_remove(audio);
            complete(request_id, ok, text || "{}");
            destroy_audio(audio);
        };
        audio.__settle = settle;
        audio.onEnded(function () { settle(true); });
        audio.onError(function (error) { settle(false, fail_text(error, "playback failed")); });
        try {
            audio.play();
        } catch (error) {
            settle(false, fail_text(error, "playback failed"));
        }
    };

    handlers[KIND.stop_game_audio] = function (request_id, payload) {
        var args = parse_payload(payload);
        var path = args.path ? String(args.path) : "";

        if (path) {
            var music = looped_audio[path];
            if (music) {
                delete looped_audio[path];
                destroy_audio(music);
            }
            // One-shots settle through their own closure, so the matching request is
            // always answered and the instance torn down exactly once.
            for (var i = sfx_active.length - 1; i >= 0; --i) {
                var audio = sfx_active[i];
                if (audio.__path === path && audio.__settle)
                    audio.__settle(false, "playback stopped");
            }
        } else {
            for (var key in looped_audio) {
                if (Object.prototype.hasOwnProperty.call(looped_audio, key)) {
                    destroy_audio(looped_audio[key]);
                    delete looped_audio[key];
                }
            }
            while (sfx_active.length) {
                var active = sfx_active[0];
                if (active.__settle)
                    active.__settle(false, "playback stopped");
                else {
                    sfx_remove(active);
                    destroy_audio(active);
                }
            }
        }
        complete(request_id, true, "{}");
    };

    // ---------- host lifecycle ----------
    // The runtime layer suspends the WebAudio contexts, but the hosts this player is for
    // expose no WebAudio at all: their music runs on InnerAudioContext here, and a host
    // that keeps it playing in the background is a review rejection. Looping music is the
    // only persistent audio, so it is the one worth pausing; one-shots are over before the
    // switch matters. wx_tt.onHide accepts multiple listeners, so this rides along the
    // runtime layer's own hook without replacing it.

    var music_paused = false;

    if (wx_tt && typeof wx_tt.onHide === "function") {
        wx_tt.onHide(function () {
            music_paused = true;
            for (var path in looped_audio) {
                if (Object.prototype.hasOwnProperty.call(looped_audio, path)) {
                    try { looped_audio[path].pause(); } catch (error) { /* the map entry survives regardless */ }
                }
            }
        });
    }
    if (wx_tt && typeof wx_tt.onShow === "function") {
        wx_tt.onShow(function () {
            if (!music_paused)
                return;
            music_paused = false;
            for (var path in looped_audio) {
                if (Object.prototype.hasOwnProperty.call(looped_audio, path)) {
                    try { looped_audio[path].play(); } catch (error) { /* a dead instance cleans itself up via onError */ }
                }
            }
        });
    }

    // ---------- analytics ----------
    // Fire-and-forget by design: the vendor dashboard is a side channel, and a lost event
    // must never disturb the game, so every failure (missing API, bad data, host throw) is
    // swallowed and the request still answers "ok".

    function report_event(event, params) {
        if (!wx_tt)
            return;
        try {
            var data = {};
            if (params && typeof params === "object")
                data = params;
            var name = String(event);
            if (typeof wx_tt.reportAnalytics === "function")
                wx_tt.reportAnalytics(name, data);
            else if (typeof wx_tt.reportEvent === "function")
                wx_tt.reportEvent(name, data);
        } catch (error) {
            // Swallowed on purpose (see above).
        }
    }

    handlers[KIND.report_analytics] = function (request_id, payload) {
        var args = parse_payload(payload);
        report_event(args.event, args.params);
        complete(request_id, true, "{}");
    };

    // ---------- publish ----------

    rbfx.sdk = {
        kind: KIND,
        handlers: handlers,
        // Direct report for the bootstrap layers (loading screen, runtime error hooks):
        // they live outside the engine request path and cannot wait for a wasm round-trip.
        report: report_event,
        invoke: function (kind, request_id, payload) {
            var handler = handlers[kind];
            if (typeof handler !== "function") {
                complete(request_id, false, "unsupported request kind " + kind);
                return;
            }
            try {
                handler(request_id | 0, payload);
            } catch (error) {
                complete(request_id, false, fail_text(error, "request failed"));
            }
        }
    };
})();
