// Build worker: started by app.js from a Blob URL that first runs the page's offline loader
// (device/web/index.html), so fetch() and importCached() fall back to IndexedDB when offline.
// Messages in: {yaml}; out: {log} | {done: Uint8Array} | {error}.
import { codegen, compileAndLink } from './build.js';

const PYODIDE = 'https://cdn.jsdelivr.net/pyodide/v314.0.8/full/';
const base = new URL('./', import.meta.url);
const deps = {
  asset: async p => new Uint8Array(await (await fetch(new URL(p, base))).arrayBuffer()),
  log: t => postMessage({ log: t }),
  cache: {
    get: key => buildCache.get(key).then(v => v?.data),
    put: (key, data) => buildCache.put(key, { data }),
  },
};

onmessage = async ({ data: { yaml } }) => {
  try {
    deps.log('Loading Pyodide');
    // Pyodide would import pyodide.asm.mjs itself, bypassing the loader; hand it over instead.
    const { loadPyodide } = await importCached(PYODIDE + 'pyodide.mjs');
    const { default: createPyodideModule } = await importCached(PYODIDE + 'pyodide.asm.mjs');
    deps.pyodide = await loadPyodide({ indexURL: PYODIDE, createPyodideModule, stdout: deps.log, stderr: deps.log });
    const src = await codegen(yaml, deps);
    deps.log('Loading clang/lld (WebAssembly, first run downloads ~100 MB)');
    deps.runLLVM = (await importCached(new URL('clang/bundle.js', base).href)).runLLVM;
    const bin = await compileAndLink(src, deps);
    postMessage({ done: bin }, [bin.buffer]);
  } catch (e) {
    postMessage({ error: String(e?.stack || e) });
  }
};
postMessage({ ready: true });
