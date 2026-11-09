// Usage: node script/test_minigame_package.js <package-directory> <WeChat|Douyin> [emscripten-root]
// Checks the assembled files and exercises the vendor-specific bootstrap without a devtool login.
"use strict";
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const zlib = require("node:zlib");

async function main() {
    const root = path.resolve(process.argv[2]);
    const vendor = process.argv[3];
    assert(["WeChat", "Douyin"].includes(vendor), "Expected WeChat or Douyin");
    const toolchain = path.resolve(process.argv[4] || process.env.EMSCRIPTEN
        || path.join(__dirname, "../../emsdk/upstream/emscripten"));
    const acorn = vendor === "WeChat" ? require(path.join(toolchain, "node_modules/acorn")) : null;
    if (acorn) {
        const {transpile} = require("../bin/EditorData/Build/prepare_wechat_js.cjs");
        const fixture = `(() => {
            let calls = 0;
            const receiver = {value: 7, read() { ++calls; return this.value; }};
            let value; value ??= receiver?.read?.();
            let zero = 0; zero ??= 99;
            let or = 0; or ||= 9;
            let and = 2; and &&= 5;
            return JSON.stringify([value, zero, or, and, calls, null?.missing,
                String(0x20000000000001n), "literal 0n ?. ??="]);
        })()`;
        const converted = transpile(fixture, toolchain);
        acorn.parse(converted, {ecmaVersion: 2017});
        assert.equal(vm.runInNewContext(converted), vm.runInNewContext(fixture),
            "Downleveling must preserve receivers, side effects and exact BigInt values");
    }
    const json = name => JSON.parse(fs.readFileSync(path.join(root, name), "utf8"));
    const project = json("project.config.json");
    const game = json("game.json");
    const manifest = json("rbfx_files.json");
    if (vendor === "WeChat") {
        assert.equal(project.compileType, "game");
        assert.equal(project.simulatorType, "wechat");
        assert(!project.appid || /^wx[0-9a-f]{16}$/i.test(project.appid), "WeChat must not inherit a Douyin AppID");
    }
    for (const subpackage of game.subPackages) {
        assert(fs.existsSync(path.join(root, subpackage.root, "game.js")));
    }
    let fileReads = 0;
    // The host file system rejects engine-private suffixes even when the file exists on disk.
    // Keep this mock stricter than Node's fs, which otherwise hides packaging permission bugs.
    const wechatReadable = new Set([".json", ".js", ".bin", ".png", ".jpg", ".jpeg", ".gif", ".webp",
        ".ttf", ".mp3", ".aac", ".m4a", ".wav", ".ogg", ".wasm", ".br"]);
    const api = {
        env: {USER_DATA_PATH: "user://test"},
        getDeviceInfo: () => ({platform: "devtools", system: "Windows 11"}),
        getSystemInfoSync: () => ({platform: "devtools", system: "Windows 11"}),
        getFileSystemManager: () => ({
            readFileSync(name, encoding) {
                ++fileReads;
                if (vendor === "WeChat" && !wechatReadable.has(path.extname(name).toLowerCase()))
                    throw Error(`readFileSync:fail permission denied, open ${name}`);
                // Regression from the device log: cooked .fbx.d paths are not resolvable by
                // the host package loader. Exercise the logical-to-physical mapping too.
                if (vendor === "WeChat" && name.split("/").slice(0, -1).some(part => part.includes(".")))
                    throw Error(`readFileSync:fail ${name} not absolute path`);
                const bytes = fs.readFileSync(path.join(root, name));
                return encoding ? bytes.toString(encoding) : Uint8Array.from(bytes).buffer;
            }
        })
    };
    let loadedWasm;
    const loader = {instantiate: (file, imports) => {
        loadedWasm = file;
        return Promise.resolve({instance: {exports: {}}, module: {}});
    }};
    const sandbox = {console, ArrayBuffer, Uint8Array, Module: {canvas: {}}, WebAssembly: undefined};
    if (vendor === "WeChat") {
        sandbox.wx = api;
        sandbox.WXWebAssembly = loader;
    } else {
        // Douyin's wx alias must not accidentally select the WeChat branch.
        sandbox.tt = api;
        sandbox.wx = api;
        sandbox.TTWebAssembly = loader;
        sandbox.WXWebAssembly = {instantiate() { throw Error("Wrong vendor loader"); }};
    }
    const context = vm.createContext(sandbox);
    const run = name => vm.runInContext(fs.readFileSync(path.join(root, name), "utf8"), context, {filename: name});
    run("rbfx_env.js");
    run("rbfx_game_config.js");
    assert.equal(sandbox.rbfx.env.name, vendor.toLowerCase());
    if (vendor === "Douyin")
        assert.equal(sandbox.WXWebAssembly, undefined);
    const config = sandbox.rbfx.game_config;
    for (const name of [config.wasm_file, config.wasm_file_compatible].filter(Boolean)) {
        const wasm = zlib.brotliDecompressSync(fs.readFileSync(path.join(root, name)));
        assert(WebAssembly.validate(wasm), `Invalid WebAssembly: ${name}`);
        assert(game.subPackages.some(p => name.startsWith(p.root)), "Wasm subpackage must be declared");
        const engineScript = path.join(root, path.dirname(name), "MinigamePlayer.js");
        const commonJs = {exports: {}};
        vm.runInNewContext(fs.readFileSync(engineScript, "utf8"), {module: commonJs, exports: commonJs.exports, console});
        assert.equal(typeof commonJs.exports, "function", "Engine factory export must survive conversion");
    }
    assert(game.subPackages.some(p => p.name === config.data_subpackage));
    run("minigame_files.js");
    for (const entry of manifest.files) {
        const physical = path.resolve(root, entry.root || "", entry.physical || entry.path);
        assert(physical.startsWith(root + path.sep), "Manifest path escapes the package");
        assert.equal(fs.statSync(physical).size, entry.size, entry.path);
        assert.equal(sandbox.rbfx.files.size(entry.path), entry.size, entry.path);
        assert.equal(sandbox.rbfx.files.resolve(entry.path), (entry.root || "") + (entry.physical || entry.path));
        if (entry.path.startsWith("Data/"))
            assert.equal(sandbox.rbfx.files.resolve(entry.path.slice(5)), sandbox.rbfx.files.resolve(entry.path));
        const data = sandbox.rbfx.files.readBytes(entry.path, entry.size);
        assert.equal(data && data.byteLength, entry.size, `Host cannot read ${entry.path}`);
    }
    const gameEntry = manifest.files.find(p => p.path === "Data/Game.json");
    assert(gameEntry, "Missing game entry configuration");
    assert.equal(sandbox.rbfx.files.readBytes(gameEntry.path, gameEntry.size).byteLength, gameEntry.size);
    assert(fileReads >= 2, "File layer must read actual package files");
    const preJs = path.join(__dirname, "../Source/MinigamePlayer/pre-js/rbfx_pre.js");
    vm.runInContext(fs.readFileSync(preJs, "utf8"), context);
    let instantiated = false;
    sandbox.Module.instantiateWasm({}, instance => { instantiated = !!instance.exports; });
    await new Promise(resolve => setImmediate(resolve));
    assert(instantiated, "Vendor loader must deliver the instance to Emscripten");
    assert.equal(loadedWasm, config.wasm_file);
    let totalBytes = 0;
    let mainBytes = 0;
    function inspect(directory) {
        for (const entry of fs.readdirSync(directory, {withFileTypes: true})) {
            const file = path.join(directory, entry.name);
            if (entry.isDirectory()) {
                inspect(file);
                continue;
            }
            const bytes = fs.statSync(file).size;
            totalBytes += bytes;
            if (directory === root)
                mainBytes += bytes;
            if (entry.name.endsWith(".js"))
                new vm.Script(fs.readFileSync(file, "utf8"), {filename: file});
            if (acorn && entry.name.endsWith(".js")
                && !file.startsWith(path.join(root, "package_data", "files") + path.sep))
                acorn.parse(fs.readFileSync(file, "utf8"), {ecmaVersion: 2017, sourceType: "script"});
        }
    }
    inspect(root);
    assert(mainBytes <= 4 * 1024 * 1024, "Main package exceeds 4 MiB");
    assert(totalBytes <= (vendor === "WeChat" ? 30 : 20) * 1024 * 1024, "Total package exceeds vendor budget");
    console.log(`PASS ${vendor}: ${manifest.files.length} resources, main ${mainBytes} bytes, total ${totalBytes} bytes; wasm, manifest and vendor bootstrap`);
}
main().catch(error => { console.error(error); process.exitCode = 1; });
