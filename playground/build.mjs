// build.mjs - assembles the web playground into ONE self-contained file:
//   build/playground/index.html  =  template.html
//                                  + minic-wasi.js (inlined)
//                                  + build/minic.wasm (base64)
//                                  + examples/*.c (JSON)
//
// Usage (from the project root, after `make wasm`):  node playground/build.mjs
import { readFileSync, readdirSync, writeFileSync, mkdirSync } from "node:fs";
import { execSync } from "node:child_process";

/* Escape text that will sit inside a <script> element. */
function scriptSafe(s) {
    return s.replace(/<\/(script)/gi, "<\\/$1");
}

const examples = {};
for (const file of readdirSync("examples").filter((f) => f.endsWith(".c")).sort()) {
    examples[file.replace(/\.c$/, "")] = readFileSync(`examples/${file}`, "utf8");
}

let version = "dev";
try {
    version = execSync("git rev-parse --short HEAD", { stdio: ["ignore", "pipe", "ignore"] }).toString().trim();
} catch {
    /* not a git checkout: keep "dev" */
}

const replacements = {
    "/*__MINIC_WASI__*/": scriptSafe(readFileSync("playground/minic-wasi.js", "utf8")),
    "__MINIC_WASM_BASE64__": readFileSync("build/minic.wasm").toString("base64"),
    "/*__EXAMPLES__*/": scriptSafe(JSON.stringify(examples)),
    "__MINIC_VERSION__": version,
};

let html = readFileSync("playground/template.html", "utf8");
for (const [key, value] of Object.entries(replacements)) {
    if (!html.includes(key)) {
        throw new Error(`placeholder ${key} missing from template.html`);
    }
    html = html.split(key).join(value);
}

mkdirSync("build/playground", { recursive: true });
writeFileSync("build/playground/index.html", html);
console.log(`wrote build/playground/index.html (${(html.length / 1024).toFixed(0)} KiB, ${Object.keys(examples).length} examples, version ${version})`);
