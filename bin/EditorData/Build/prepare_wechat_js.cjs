// WeChat's device-debug uploader parses older JavaScript than its simulator executes.
// Use the compiler/parser already supplied by emsdk; no npm download or engine relink is needed.
"use strict";
const fs = require("fs");
const path = require("path");
const childProcess = require("child_process");

function lowerBigIntLiterals(source, acorn) {
    // The runtime still supports BigInt (and the standard wasm module requires it), but the
    // uploader parser rejects the 123n spelling. Tokenize so strings/comments remain untouched.
    const edits = [];
    for (const token of acorn.tokenizer(source, {ecmaVersion: "latest"})) {
        if (typeof token.value === "bigint")
            edits.push({start: token.start, end: token.end, text: `BigInt(${JSON.stringify(token.value.toString())})`});
    }
    for (const edit of edits.reverse())
        source = source.slice(0, edit.start) + edit.text + source.slice(edit.end);
    return source;
}

function transpile(source, toolchain) {
    const acorn = require(path.join(toolchain, "node_modules/acorn"));
    const compiler = path.join(toolchain, "node_modules/google-closure-compiler/cli.js");
    if (!fs.existsSync(compiler))
        throw Error(`Missing emsdk Closure Compiler: ${compiler}`);
    const result = childProcess.spawnSync(process.execPath, [compiler,
        "--compilation_level", "WHITESPACE_ONLY", "--language_in", "ECMASCRIPT_NEXT",
        "--language_out", "ECMASCRIPT_2017", "--rewrite_polyfills", "false",
        "--emit_use_strict", "false", "--warning_level", "QUIET"], {
        input: lowerBigIntLiterals(source, acorn), encoding: "utf8", maxBuffer: 32 * 1024 * 1024
    });
    if (result.error || result.status !== 0)
        throw Error(`WeChat JavaScript conversion failed: ${result.error || result.stderr}`);
    acorn.parse(result.stdout, {ecmaVersion: 2017, sourceType: "script"});
    return result.stdout;
}

function preparePackage(root, toolchain) {
    const scripts = [];
    for (const entry of fs.readdirSync(root, {withFileTypes: true})) {
        if (entry.isFile() && entry.name.endsWith(".js"))
            scripts.push(path.join(root, entry.name));
        else if (entry.isDirectory() && entry.name.startsWith("package_wasm")) {
            for (const name of fs.readdirSync(path.join(root, entry.name))) {
                if (name.endsWith(".js"))
                    scripts.push(path.join(root, entry.name, name));
            }
        }
    }
    for (const script of scripts.sort()) {
        const output = transpile(fs.readFileSync(script, "utf8"), toolchain);
        fs.writeFileSync(script + ".es2017.tmp", output);
        fs.renameSync(script + ".es2017.tmp", script);
    }
    console.log(`Converted and verified ${scripts.length} WeChat scripts as ES2017`);
}

module.exports = {lowerBigIntLiterals, transpile, preparePackage};
if (require.main === module) {
    try { preparePackage(path.resolve(process.argv[2]), path.resolve(process.argv[3])); }
    catch (error) { console.error(error.stack || error); process.exitCode = 1; }
}
