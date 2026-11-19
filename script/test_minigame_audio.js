// Run: node script/test_minigame_audio.js
"use strict";
const assert = require("node:assert/strict");
const fs = require("node:fs");
const vm = require("node:vm");
const path = require("node:path");
const source = fs.readFileSync(path.join(__dirname, "../Source/MinigamePlayer/js/minigame_sdk.js"), "utf8");

function fixture() {
    const contexts = [], results = new Map(), timers = new Map();
    let clock = 1000, nextId = 0, timerId = 0;
    const sandbox = {
        console, Date: { now: () => clock },
        setTimeout(fn) { timers.set(++timerId, fn); return timerId; },
        clearTimeout(id) { timers.delete(id); },
        rbfx: {
            files: { resolve: name => name },
            native: { sdk_complete(id, ok, data) {
                assert(!results.has(id), `duplicate completion ${id}`);
                results.set(id, { ok, data });
            } },
        },
        wx_tt: { createInnerAudioContext() {
            const audio = {
                destroyed: false, playing: false, plays: 0,
                onEnded(fn) { this.end = fn; }, offEnded(fn) { if (this.end === fn) this.end = null; },
                onError(fn) { this.error = fn; }, offError(fn) { if (this.error === fn) this.error = null; },
                play() { assert(!this.destroyed); this.playing = true; ++this.plays; },
                stop() { this.playing = false; },
                destroy() { assert(!this.destroyed); this.destroyed = true; this.playing = false; },
                finish() { this.playing = false; this.end(); },
            };
            contexts.push(audio);
            return audio;
        } },
    };
    vm.runInNewContext(source, sandbox);
    function request(kind, args) {
        const id = ++nextId;
        sandbox.rbfx.sdk.handlers[kind](id, JSON.stringify(args));
        assert(contexts.filter(a => !a.destroyed && !a.loop).length <= 8, "context budget exceeded");
        return id;
    }
    return { contexts, results, timers, sandbox,
        play: args => request(8, { path: "shot.m4a", ...args }),
        stop: name => request(9, { path: name || "" }),
        tick: ms => { clock += ms; },
    };
}

// Saturation, priority protection and exactly-once late callbacks.
{
    const f = fixture();
    for (let i = 0; i < 8; ++i) f.play({ priority: 2 });
    assert.equal(f.contexts.filter(a => a.playing).length, 8);
    const dropped = f.play({ priority: 0 });
    assert.equal(JSON.parse(f.results.get(dropped).data).status, "dropped");
    const oldEnd = f.contexts[0].end;
    f.play({ priority: 3 });
    assert(f.contexts[0].destroyed);
    oldEnd();
    f.stop();
    assert(f.contexts.every(a => a.destroyed));
    assert.equal(f.timers.size, 0);
}

// A new gunshot steals the oldest low-priority impact, not an older important voice.
{
    const f = fixture();
    f.play({ path: "voice.m4a", priority: 3 });
    for (let i = 0; i < 7; ++i) f.play({ path: "impact.m4a", priority: 0 });
    f.play({ priority: 2 });
    assert(f.contexts[0].playing);
    assert(f.contexts[1].destroyed);
    assert.equal(f.contexts.filter(a => a.playing).length, 8);
    f.stop();
}

// Natural completion reuses the same file; old callbacks cannot complete the new request.
{
    const f = fixture();
    f.play({});
    const a = f.contexts[0], oldEnd = a.end, oldError = a.error;
    a.finish();
    const next = f.play({});
    assert.equal(f.contexts.length, 1);
    assert.equal(a.plays, 2);
    oldEnd(); oldError({ errMsg: "late" });
    assert(!f.results.has(next));
    a.finish();
    assert(f.results.get(next).ok);
    f.stop("shot.m4a");
    assert(a.destroyed);
}

// Merge by group across different files/variants; no window for the player gun.
{
    const f = fixture();
    f.play({ path: "ship.m4a", group: "cannon", mergeMs: 90 });
    const merged = f.play({ path: "tower.m4a", group: "cannon", mergeMs: 90 });
    assert.equal(JSON.parse(f.results.get(merged).data).status, "merged");
    assert.equal(f.contexts.length, 1);
    f.tick(90);
    f.play({ path: "tower.m4a", group: "cannon", mergeMs: 90 });
    assert.equal(f.contexts.length, 2);
    f.stop();
}

// Pool cannot grow with different paths; explicit opt-out always destroys on end.
{
    const f = fixture();
    for (let i = 0; i < 40; ++i) {
        f.play({ path: `${i}.m4a` });
        f.contexts.at(-1).finish();
    }
    assert.equal(f.contexts.filter(a => !a.destroyed).length, 8);
    f.play({ path: "no-pool.m4a", reuse: false });
    const a = f.contexts.at(-1);
    a.finish(); assert(a.destroyed);
    f.stop();
    assert(f.contexts.every(a => a.destroyed));
}

// Host errors and missing callbacks release both request and instance.
{
    const f = fixture();
    const failed = f.play({});
    f.contexts[0].error({ errMsg: "decode failed" });
    assert.equal(f.results.get(failed).ok, false);
    assert(f.contexts[0].destroyed);
    const hung = f.play({});
    [...f.timers.values()][0]();
    assert.equal(f.results.get(hung).ok, false);
    assert(f.contexts[1].destroyed);
    assert.equal(f.timers.size, 0);
}

// Hosts without callback removal are safe but do not pool.
{
    const f = fixture();
    f.play({});
    const a = f.contexts[0];
    a.offEnded = undefined;
    a.finish(); assert(a.destroyed);
}
// Sustained fire reuses one context; non-pooled baseline allocates on every shot.
for (const reuse of [true, false]) {
    const f = fixture();
    for (let i = 0; i < 1000; ++i) {
        f.play({ reuse, priority: 2 });
        f.contexts.at(-1).finish();
        f.tick(220);
    }
    assert.equal(f.contexts.length, reuse ? 1 : 1000);
    f.stop();
}

// stop-by-path leaves unrelated active/idle sounds alone; BGM remains independent.
{
    const f = fixture();
    f.play({ path: "music.m4a", loop: true });
    f.play({ path: "a.m4a" }); f.contexts.at(-1).finish();
    const b = f.play({ path: "b.m4a" });
    f.stop("a.m4a");
    assert(f.contexts[1].destroyed);
    assert(f.contexts[0].playing && f.contexts[2].playing);
    assert(!f.results.has(b));
    f.stop();
    assert(f.contexts.every(a => a.destroyed));
}

// Synchronous play/setup failures must also remove the registered active context.
for (const mode of ["create", "play", "source"]) {
    const f = fixture();
    const create = f.sandbox.wx_tt.createInnerAudioContext;
    f.sandbox.wx_tt.createInnerAudioContext = () => {
        if (mode === "create") throw Error("creation failed");
        const a = create();
        if (mode === "play") a.play = () => { throw Error("play failed"); };
        if (mode === "source") Object.defineProperty(a, "src", {
            set() { a.error({ errMsg: "bad source" }); },
        });
        return a;
    };
    const id = f.play({});
    assert.equal(f.results.get(id).ok, false);
    assert(f.contexts.every(a => a.destroyed));
    assert.equal(f.timers.size, 0);
    f.stop();
}
console.log("Minigame audio lifecycle, priority, coalescing and bounded pool checks passed.");
