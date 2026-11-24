/*
 * minic-wasi.js - a minimal WASI host for running minic.wasm.
 *
 * minic.wasm is the unmodified compiler (src/*.c) built with
 * `clang --target=wasm32-wasi`. It only needs a handful of WASI calls:
 * command-line arguments, stdin/stdout/stderr and proc_exit. Files are never
 * opened: the program source is passed on stdin (`minic - --run`).
 *
 * runMinic(module, args, stdinText, options) -> { stdout, stderr, exitCode, truncated }
 *   module     a compiled WebAssembly.Module of minic.wasm
 *   args       argument list without argv[0], e.g. ["-", "--ast"]
 *   stdinText  the Mini-C source code
 *   options    { maxOutputBytes } (default 1 MiB per stream)
 *
 * Works in browsers (main thread or worker) and in Node.js (used by test.mjs).
 * Very deep recursion can exhaust the JavaScript engine's stack before
 * minic's own recursion limit; that is reported as a runtime error too.
 */
(function (root) {
    "use strict";

    var ERRNO_SUCCESS = 0;
    var ERRNO_BADF = 8;
    var ERRNO_NOENT = 44;
    var ERRNO_SPIPE = 70;

    /* Thrown by proc_exit to unwind out of the WebAssembly call stack. */
    function ExitSignal(code) { this.code = code; }

    /* Thrown when a program prints more than the output limit. */
    function OutputLimit() {}

    function runMinic(module, args, stdinText, options) {
        var maxOut = (options && options.maxOutputBytes) || 1024 * 1024;
        var encoder = new TextEncoder();
        var argv = ["minic"].concat(args).map(function (a) { return encoder.encode(a + "\0"); });
        var stdin = encoder.encode(stdinText || "");
        var stdinPos = 0;
        var out = { 1: [], 2: [] };
        var outLen = { 1: 0, 2: 0 };
        var truncated = false;
        var memory = null;

        function view() { return new DataView(memory.buffer); }
        function bytes() { return new Uint8Array(memory.buffer); }

        var wasi = {
            args_sizes_get: function (argcPtr, bufSizePtr) {
                var size = argv.reduce(function (n, a) { return n + a.length; }, 0);
                view().setUint32(argcPtr, argv.length, true);
                view().setUint32(bufSizePtr, size, true);
                return ERRNO_SUCCESS;
            },
            args_get: function (argvPtr, bufPtr) {
                var dv = view(), mem = bytes();
                argv.forEach(function (a, i) {
                    dv.setUint32(argvPtr + 4 * i, bufPtr, true);
                    mem.set(a, bufPtr);
                    bufPtr += a.length;
                });
                return ERRNO_SUCCESS;
            },
            fd_write: function (fd, iovs, iovsLen, nwrittenPtr) {
                var dv = view(), mem = bytes(), total = 0, i;
                if (fd !== 1 && fd !== 2) { return ERRNO_BADF; }
                for (i = 0; i < iovsLen; i++) {
                    var ptr = dv.getUint32(iovs + 8 * i, true);
                    var len = dv.getUint32(iovs + 8 * i + 4, true);
                    if (outLen[fd] + len > maxOut) {
                        truncated = true;
                        throw new OutputLimit();
                    }
                    out[fd].push(mem.slice(ptr, ptr + len));
                    outLen[fd] += len;
                    total += len;
                }
                dv.setUint32(nwrittenPtr, total, true);
                return ERRNO_SUCCESS;
            },
            fd_read: function (fd, iovs, iovsLen, nreadPtr) {
                var dv = view(), mem = bytes(), total = 0, i;
                if (fd !== 0) { return ERRNO_BADF; }
                for (i = 0; i < iovsLen; i++) {
                    var ptr = dv.getUint32(iovs + 8 * i, true);
                    var len = dv.getUint32(iovs + 8 * i + 4, true);
                    var chunk = stdin.subarray(stdinPos, stdinPos + len);
                    mem.set(chunk, ptr);
                    stdinPos += chunk.length;
                    total += chunk.length;
                    if (chunk.length < len) { break; }
                }
                dv.setUint32(nreadPtr, total, true);
                return ERRNO_SUCCESS;
            },
            fd_fdstat_get: function (fd, statPtr) {
                var dv = view();
                if (fd > 2) { return ERRNO_BADF; }
                dv.setUint8(statPtr, 2);                     /* character device */
                dv.setUint16(statPtr + 2, 0, true);          /* flags */
                dv.setBigUint64(statPtr + 8, BigInt("0xffffffffffffffff"), true);
                dv.setBigUint64(statPtr + 16, BigInt("0xffffffffffffffff"), true);
                return ERRNO_SUCCESS;
            },
            fd_fdstat_set_flags: function () { return ERRNO_SUCCESS; },
            fd_seek: function () { return ERRNO_SPIPE; },
            fd_close: function () { return ERRNO_SUCCESS; },
            fd_prestat_get: function () { return ERRNO_BADF; },      /* no preopened dirs */
            fd_prestat_dir_name: function () { return ERRNO_BADF; },
            path_open: function () { return ERRNO_NOENT; },          /* no filesystem */
            proc_exit: function (code) { throw new ExitSignal(code); }
        };

        var instance = new WebAssembly.Instance(module, { wasi_snapshot_preview1: wasi });
        memory = instance.exports.memory;
        var exitCode = 0;
        try {
            instance.exports._start();
        } catch (e) {
            if (e instanceof ExitSignal) {
                exitCode = e.code;
            } else if (e instanceof OutputLimit) {
                exitCode = 1;
            } else if (e instanceof RangeError) {
                /* The JavaScript engine's own call stack ran out (deep Mini-C
                 * recursion) before minic's MAX_CALL_DEPTH was reached. */
                out[2].push(encoder.encode("Runtime Error: call stack overflow (recursion too deep for the browser's stack)\n"));
                exitCode = 1;
            } else if (e instanceof WebAssembly.RuntimeError) {
                out[2].push(encoder.encode("minic: internal error: " + e.message + "\n"));
                exitCode = 134;
            } else {
                throw e;
            }
        }

        function join(chunks) {
            var n = chunks.reduce(function (s, c) { return s + c.length; }, 0);
            var all = new Uint8Array(n), off = 0;
            chunks.forEach(function (c) { all.set(c, off); off += c.length; });
            return new TextDecoder("utf-8").decode(all);
        }

        return { stdout: join(out[1]), stderr: join(out[2]), exitCode: exitCode, truncated: truncated };
    }

    root.runMinic = runMinic;
    if (typeof module !== "undefined" && module.exports) {
        module.exports = { runMinic: runMinic };
    }
})(typeof self !== "undefined" ? self : this);
