// test.mjs - checks that build/minic.wasm (via minic-wasi.js) behaves exactly
// like the native ./minic binary: same stdout, stderr and exit code for every
// example in every mode, plus error and limit cases.
//
// Usage (from the project root, after `make` and `make wasm`):
//   node playground/test.mjs
import { readFileSync, readdirSync } from "node:fs";
import { spawnSync } from "node:child_process";
import { createRequire } from "node:module";

const require = createRequire(import.meta.url);
const { runMinic } = require("./minic-wasi.js");
const wasm = new WebAssembly.Module(readFileSync("build/minic.wasm"));

/* Run the native compiler with the source on stdin. */
function native(args, source) {
    const r = spawnSync("./minic", args, { input: source, encoding: "utf8" });
    return { stdout: r.stdout, stderr: r.stderr, exitCode: r.status };
}

let passed = 0;
let failed = 0;

/* Compare wasm and native results for one case. */
function check(name, args, source) {
    const w = runMinic(wasm, args, source);
    const n = native(args, source);
    const same = w.stdout === n.stdout && w.stderr === n.stderr && w.exitCode === n.exitCode;
    if (same) {
        passed++;
        console.log(`PASS wasm ${name}`);
    } else {
        failed++;
        console.log(`FAIL wasm ${name}`);
        console.log(`     wasm:   exit ${w.exitCode} stdout ${JSON.stringify(w.stdout.slice(0, 200))} stderr ${JSON.stringify(w.stderr.slice(0, 200))}`);
        console.log(`     native: exit ${n.exitCode} stdout ${JSON.stringify(n.stdout.slice(0, 200))} stderr ${JSON.stringify(n.stderr.slice(0, 200))}`);
    }
}

const modes = [["--tokens"], ["--ast"], [], ["--run"]];
for (const file of readdirSync("examples").filter((f) => f.endsWith(".c")).sort()) {
    const source = readFileSync(`examples/${file}`, "utf8");
    for (const mode of modes) {
        check(`${file} ${mode[0] || "codegen"}`, ["-", ...mode], source);
    }
}

const cases = {
    "lexer error": "int main() { int x = 1 @ 2; return 0; }",
    "parser error": "int main() { int x = ; }",
    "semantic errors + warning": "int main() { int u; print(u); print(y); print(add(1)); return 0; }",
    "runtime division by zero": "int main() { int z = 0; print(1); print(1 / z); return 0; }",
    "deep recursion (1000)": "int d(int n) { if (n == 0) return 0; return 1 + d(n - 1); } int main() { print(d(1000)); return 0; }",
    "exit code": "int main() { print('x'); return 7; }",
    "unicode in string": 'int main() { print("আমি C কম্পাইলার"); return 0; }',
};
for (const [name, src] of Object.entries(cases)) {
    check(name, ["-", "--run"], src);
}
// Runaway recursion: the browser's stack may run out before minic's own
// limit, so only require a clean runtime error (not identical text).
{
    const r = runMinic(wasm, ["-", "--run"], "int f(int n) { return f(n + 1); } int main() { print(f(0)); return 0; }");
    if (r.exitCode === 1 && r.stderr.includes("call stack overflow")) {
        passed++;
        console.log("PASS wasm runaway recursion is a clean runtime error");
    } else {
        failed++;
        console.log(`FAIL wasm runaway recursion: exit ${r.exitCode} ${JSON.stringify(r.stderr)}`);
    }
}
check("step limit", ["-", "--run", "--max-steps", "100000"], "int main() { while (1) { } return 0; }");
check("help", ["--help"], "");

console.log(`\n${passed} passed, ${failed} failed`);
process.exit(failed === 0 ? 0 : 1);
