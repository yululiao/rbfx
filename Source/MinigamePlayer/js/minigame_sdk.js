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
        privacy: 4
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
