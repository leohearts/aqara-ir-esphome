// Runs docs/build.js under node: node test/build-node.mjs <config.yaml> <out-binary> [path to @yowasp/clang bundle.js]
// Without the third argument only the ESPHome codegen step runs.
import fs from 'node:fs';
import path from 'node:path';
import { createRequire } from 'node:module';
import { codegen, compileAndLink } from '../docs/build.js';

const [yamlPath, outPath, clangBundle] = process.argv.slice(2);
const docs = new URL('../docs/', import.meta.url);
const deps = {
  asset: async p => new Uint8Array(fs.readFileSync(new URL(p, docs))),
  log: t => console.log(t.trimEnd()),
};
const require = createRequire(path.resolve('package.json'));
const { loadPyodide } = require('pyodide'); // resolved from the current directory's node_modules
deps.pyodide = await loadPyodide();

let t = Date.now();
const src = await codegen(fs.readFileSync(yamlPath, 'utf8'), deps);
console.log(`codegen ${(Date.now() - t) / 1000}s, top-level:`, Object.keys(src));
if (!clangBundle) process.exit(0);

deps.runLLVM = (await import(path.resolve(clangBundle))).runLLVM;
t = Date.now();
const bin = await compileAndLink(src, deps);
fs.writeFileSync(outPath, bin);
console.log(`compile+link ${(Date.now() - t) / 1000}s, ${bin.length} bytes -> ${outPath}`);
