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
        stop_game_audio: 9
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
    // Clips live entirely on the host side: the recorder writes mp3 files into the
    // user data directory under rbfx_voice_<slot>.mp3, playback goes through
    // InnerAudioContext. The engine never decodes these (its Sound loaders only
    // know ogg/wav); the slot name is the only contract between both worlds.

    function user_data_path() {
        return (wx_tt && wx_tt.env && wx_tt.env.USER_DATA_PATH) || "";
    }

    function voice_file_path(slot) {
        return user_data_path() + "/rbfx_voice_" + String(slot || "default") + ".mp3";
    }

    var recorder_manager = null;
    var recording_request = 0;

    function get_recorder_manager() {
        if (recorder_manager)
            return recorder_manager;
        recorder_manager = wx_tt.getRecorderManager();
        recorder_manager.onStop(function (res) {
            var request_id = recording_request;
            recording_request = 0;
            if (!request_id)
                return;
            if (!res || !res.tempFilePath)
                return complete(request_id, false, "recorder returned no file");
            var fs = wx_tt.getFileSystemManager();
            var target = recorder_manager.__voice_target;
            // Remove a stale clip first: saveFile refuses to overwrite existing files.
            try { fs.unlinkSync(target); } catch (e) { /* not there yet, fine */ }
            fs.saveFile({
                tempFilePath: res.tempFilePath,
                filePath: target,
                success: function () {
                    complete(request_id, true, JSON.stringify({ durationMs: res.duration || 0 }));
                },
                fail: function (error) {
                    complete(request_id, false, fail_text(error, "saving the clip failed"));
                }
            });
        });
        recorder_manager.onError(function (error) {
            var request_id = recording_request;
            recording_request = 0;
            if (request_id)
                complete(request_id, false, fail_text(error, "recording failed"));
        });
        return recorder_manager;
    }

    handlers[KIND.record_voice] = function (request_id, payload) {
        if (!wx_tt || typeof wx_tt.getRecorderManager !== "function")
            return complete(request_id, false, "recording is not supported by the host");
        if (recording_request)
            return complete(request_id, false, "a recording is already in progress");

        var args = parse_payload(payload);
        var slot = args.slot ? String(args.slot) : "default";
        var duration = Math.max(1000, Math.min(args.maxDurationMs | 0 || 3000, 60000));

        var manager = get_recorder_manager();
        manager.__voice_target = voice_file_path(slot);
        recording_request = request_id;
        try {
            manager.start({ duration: duration, format: "mp3" });
        } catch (error) {
            // A synchronous throw would otherwise leak recording_request and block
            // every future recording with "already in progress".
            recording_request = 0;
            return complete(request_id, false, fail_text(error, "recording failed"));
        }
        // The completion arrives through onStop/onError above; start() itself is
        // fire-and-forget. A missing authorization surfaces through onError.
    };

    var voice_audio = null;

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
    }

    handlers[KIND.play_user_audio] = function (request_id, payload) {
        if (!wx_tt || typeof wx_tt.createInnerAudioContext !== "function")
            return complete(request_id, false, "audio playback is not supported by the host");

        var args = parse_payload(payload);
        var slot = args.slot ? String(args.slot) : "default";
        // Documented rate steps are 0.5 / 0.8 / 1.0 / 1.25 / 1.5 / 2.0 on both
        // vendors; other values get snapped by the host. Game code passes the steps.
        var rate = Number(args.rate) || 1.0;

        // One audio instance at a time: a new playback replaces the previous one and
        // completes its request as "stopped" so no callback leaks.
        stop_voice_audio();
        voice_audio = wx_tt.createInnerAudioContext();
        voice_audio.src = voice_file_path(slot);
        voice_audio.playbackRate = rate;
        var settled = false;
        var settle = function (ok, text) {
            if (settled)
                return;
            settled = true;
            complete(request_id, ok, text || "{}");
        };
        voice_audio.__settle = settle;
        voice_audio.onEnded(function () { settle(true); });
        voice_audio.onError(function (error) { settle(false, fail_text(error, "playback failed")); });
        voice_audio.play();
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
                existing.volume = gain;
                complete(request_id, true, "{}");
                return;
            }
            var music = wx_tt.createInnerAudioContext();
            music.src = src;
            music.loop = true;
            music.volume = gain;
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

    // ---------- publish ----------

    rbfx.sdk = {
        kind: KIND,
        handlers: handlers,
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
