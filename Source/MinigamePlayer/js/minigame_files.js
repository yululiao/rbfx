// rbfx minigame file layer: publishes globalThis.rbfx.files.
//
// Every engine-side read is a blocking call on the wasm thread, and the minigame runtimes
// only allow one synchronous API to reach the package directory (readFileSync). The layer is
// therefore built around one synchronous primitive plus a manifest the packaging tool places
// at the package root (rbfx_files.json):
//
//   {
//     "files": [ { "path": "Data/Game.json", "size": 1234, "root": "package_data/" }, ... ],
//     "remote": { }        // optional: package-relative path -> download URL
//   }
//
//   * package files are read with readFileSync, straight from the package. "path" is the
//     name the engine asks for; the optional "root" names the subpackage the file physically
//     lives in (no root means the main package), so the engine keeps one naming scheme no
//     matter how the build split the package;
//   * files that the manifest marks as remote are downloaded into the user data directory
//     (a) up front, through prefetch(), or (b) on demand when the engine asks for them;
//     they are served from the downloaded copy afterwards;
//   * the user data directory itself (save games, settings, fetched files) is reached
//     through the same synchronous API and surfaces as the "user" scheme of the engine VFS.
//
// Every function here is total: it never throws, it either returns the data or a negative
// status code the C++ side understands (-1 missing, -2 known but not available yet).

(function () {
    var rbfx = globalThis.rbfx;
    var wx_tt = globalThis.wx_tt;

    var manager = (wx_tt && typeof wx_tt.getFileSystemManager === "function")
        ? wx_tt.getFileSystemManager() : null;
    var user_dir = (wx_tt && wx_tt.env && wx_tt.env.USER_DATA_PATH) || "";
    var user_cache_dir = user_dir ? (user_dir + "/fetch") : "";

    // ---------- manifest ----------

    var package_files = {};        // package-relative path -> { size, remote }
    var remote_files = {};         // package-relative path -> download URL
    var pending_downloads = {};    // paths with an on-demand download in flight

    (function load_manifest() {
        if (!manager)
            return;

        var text = null;
        try {
            text = manager.readFileSync("rbfx_files.json", "utf8");
        } catch (error) {
            console.warn("[rbfx] no rbfx_files.json in the main package; the engine will see no package files");
            return;
        }

        var parsed = null;
        try {
            parsed = JSON.parse(text);
        } catch (error) {
            console.error("[rbfx] rbfx_files.json is malformed", error);
            return;
        }

        var files = (parsed && parsed.files) || [];
        for (var i = 0; i < files.length; i++) {
            var entry = files[i];
            if (entry && entry.path)
                package_files[entry.path] = {
                    size: entry.size | 0,
                    remote: false,
                    root: entry.root || "",
                    // Physical name inside the subpackage when it differs from the engine-visible
                    // one: the packaging step renames files whose extension the vendor tool would
                    // drop on import (see the manifest generator).
                    physical: entry.physical || ""
                };
        }

        var remote = (parsed && parsed.remote) || {};
        for (var path in remote) {
            if (!Object.prototype.hasOwnProperty.call(remote, path))
                continue;
            var url = remote[path];
            if (typeof url !== "string" || url.length === 0)
                continue;
            remote_files[path] = url;
            if (package_files[path])
                package_files[path].remote = true;
        }

        console.log("[rbfx] file manifest loaded: " + files.length + " package files, " +
            Object.keys(remote_files).length + " remote files");
    })();

    // ---------- small helpers ----------

    function to_bytes(data) {
        if (!data)
            return null;
        if (data instanceof Uint8Array)
            return data;
        if (data instanceof ArrayBuffer)
            return new Uint8Array(data);
        // Defensive: some implementations hand back a binary string.
        if (typeof data === "string") {
            var bytes = new Uint8Array(data.length);
            for (var i = 0; i < data.length; i++)
                bytes[i] = data.charCodeAt(i) & 0xFF;
            return bytes;
        }
        return null;
    }

    function file_exists(path) {
        if (!manager)
            return false;
        try {
            manager.accessSync(path);
            return true;
        } catch (error) {
            return false;
        }
    }

    function stat_size(path) {
        if (!manager)
            return -1;
        try {
            return manager.statSync(path).size | 0;
        } catch (error) {
            return -1;
        }
    }

    function ensure_dir(directory) {
        if (!manager || !directory)
            return;
        try {
            manager.accessSync(directory);
            return;
        } catch (error) { }
        try {
            manager.mkdirSync(directory, true);
        } catch (error) { }
    }

    function dirname(path) {
        var slash = path.lastIndexOf("/");
        return slash <= 0 ? "" : path.substring(0, slash);
    }

    function user_abs(path) { return user_dir + "/" + path; }
    function cache_abs(path) { return user_cache_dir + "/" + path; }

    // ---------- remote population ----------

    function download_remote(path, url, callback) {
        if (!wx_tt || typeof wx_tt.downloadFile !== "function" || !user_dir) {
            callback(false);
            return;
        }
        wx_tt.downloadFile({
            url: url,
            success: function (res) {
                if (!res || res.statusCode !== 200 || !res.tempFilePath) {
                    callback(false);
                    return;
                }
                try {
                    var target = cache_abs(path);
                    ensure_dir(dirname(target));
                    // The temp file is only valid during this callback; the copy is
                    // synchronous so the temp file is still alive here.
                    manager.copyFileSync(res.tempFilePath, target);
                    callback(true);
                } catch (error) {
                    console.warn("[rbfx] failed to store " + path, error);
                    callback(false);
                }
            },
            fail: function (error) {
                console.warn("[rbfx] failed to download " + path, error);
                callback(false);
            }
        });
    }

    function notify_prefetch_done(ready, failed) {
        var native = rbfx.native;
        if (native && typeof native.on_prefetch_done === "function") {
            try {
                native.on_prefetch_done(ready | 0, failed | 0);
            } catch (error) { }
        }
    }

    function prefetch(entries) {
        var ready = 0;
        var failed = 0;
        var pending = 0;
        var finished = false;

        function complete() {
            if (finished || pending !== 0)
                return;
            finished = true;
            if (ready || failed)
                console.log("[rbfx] prefetch finished: " + ready + " local, " + failed + " failed");
            notify_prefetch_done(ready, failed);
        }

        for (var i = 0; i < entries.length; i++) {
            var path = entries[i];
            var entry = package_files[path];
            if (!entry) {
                ++failed;
                continue;
            }
            if (!entry.remote) {
                ++ready;        // already inside the package
                continue;
            }
            if (file_exists(cache_abs(path))) {
                ++ready;
                continue;
            }
            var url = remote_files[path];
            if (!url) {
                ++failed;
                continue;
            }
            ++pending;
            pending_downloads[path] = true;
            download_remote(path, url, function (ok) {
                if (ok)
                    ++ready;
                else
                    ++failed;
                --pending;
                complete();
            });
        }
        complete();
    }

    // ---------- package reads (called from the engine through EM_JS) ----------

    function size(path) {
        var entry = package_files[path];
        return entry ? entry.size : -1;
    }

    function readBytes(path, maxSize) {
        var entry = package_files[path];
        if (!entry)
            return -1;

        var data = null;
        if (entry.remote) {
            var cached = cache_abs(path);
            if (!file_exists(cached)) {
                // Known to the manifest but not local yet: schedule the download and answer
                // the next time; the engine treats -2 as "retry later".
                if (!pending_downloads[path]) {
                    var url = remote_files[path];
                    if (url) {
                        pending_downloads[path] = true;
                        // Report the outcome through the regular prefetch channel: the game
                        // gets a deterministic "the load can be retried now" point instead
                        // of having to poll the file system.
                        download_remote(path, url, function (ok) {
                            delete pending_downloads[path];
                            notify_prefetch_done(ok ? 1 : 0, ok ? 0 : 1);
                        });
                    }
                }
                return -2;
            }
            try {
                data = manager.readFileSync(cached);
            } catch (error) {
                return -2;
            }
        } else {
            // The file physically lives under <root>/..., where root is the subpackage the
            // packaging step mapped it into, and under its physical name when the step had to
            // rename it for the vendor; a file without a root is in the main package and the
            // engine-visible path is the storage path.
            var physical = (entry.root ? entry.root : "") + (entry.physical || path);
            try {
                data = manager.readFileSync(physical);
            } catch (error) {
                // Listed in the manifest but unreadable: a subpackage may still be
                // downloading; the answer is the same - retry later.
                console.warn("[rbfx] readFileSync failed for " + physical, error);
                return -2;
            }
        }

        var bytes = to_bytes(data);
        if (!bytes)
            return -2;
        if (bytes.length > maxSize) {
            console.warn("[rbfx] " + path + " is larger than its manifest size");
            return -2;
        }
        return bytes;
    }

    function list(directory) {
        var prefix = directory ? directory.replace(/\/+$/, "") + "/" : "";
        var result = [];
        for (var path in package_files) {
            if (Object.prototype.hasOwnProperty.call(package_files, path) &&
                (prefix === "" || path.indexOf(prefix) === 0))
                result.push(path);
        }
        return result;
    }

    // ---------- user data directory (writable storage) ----------

    function user_available() {
        return !!(manager && user_dir);
    }

    function user_size(path) {
        if (!manager || !user_dir)
            return -1;
        return stat_size(user_abs(path));
    }

    function user_read(path, maxSize) {
        if (!manager || !user_dir)
            return -1;
        var data = null;
        try {
            data = manager.readFileSync(user_abs(path));
        } catch (error) {
            return -1;
        }
        var bytes = to_bytes(data);
        if (!bytes)
            return -1;
        if (bytes.length > maxSize)
            return -2;
        return bytes;
    }

    function user_write(path, bytes) {
        if (!manager || !user_dir || !bytes)
            return false;
        try {
            var target = user_abs(path);
            ensure_dir(dirname(target));
            // The view points into the wasm heap; copy the addressed range only, never the
            // whole backing buffer.
            var copy = bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength);
            manager.writeFileSync(target, copy);
            return true;
        } catch (error) {
            console.warn("[rbfx] failed to write user://" + path, error);
            return false;
        }
    }

    function user_remove(path) {
        if (!manager || !user_dir)
            return false;
        try {
            manager.unlinkSync(user_abs(path));
            return true;
        } catch (error) {
            return false;
        }
    }

    function user_list() {
        if (!manager || !user_dir)
            return [];
        var result = [];
        var walk = function (directory, prefix) {
            var names = null;
            try {
                names = manager.readdirSync(directory);
            } catch (error) {
                return;
            }
            for (var i = 0; i < names.length; i++) {
                var name = names[i];
                var full = directory + "/" + name;
                var relative = prefix ? prefix + "/" + name : name;
                var stats = null;
                try {
                    stats = manager.statSync(full);
                } catch (error) {
                    continue;
                }
                var is_directory = typeof stats.isDirectory === "function"
                    ? stats.isDirectory() : ((stats.mode & 0xF000) === 0x4000);
                if (is_directory)
                    walk(full, relative);
                else
                    result.push(relative);
            }
        };
        walk(user_dir, "");
        return result;
    }

    // ---------- publish ----------

    rbfx.files = {
        // Package reads, all synchronously answerable.
        size: size,
        readBytes: readBytes,
        list: list,
        prefetch: prefetch,

        // User data directory.
        userAvailable: user_available,
        userSize: user_size,
        userRead: user_read,
        userWrite: user_write,
        userRemove: user_remove,
        userList: user_list
    };

    console.log("[rbfx] file layer ready: file system=" + !!manager + ", user storage=" + user_available());
})();
