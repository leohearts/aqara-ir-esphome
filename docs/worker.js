// Build worker: started from a Blob URL by app.js, so it can live on GitHub Pages while the page
// itself is served by the P3. Messages in: {yaml}; out: {log} | {done: Uint8Array} | {error}.
import { codegen, compileAndLink } from './build.js';
import { objectCache } from './cache.js';

const PYODIDE = 'https://cdn.jsdelivr.net/pyodide/v314.0.8/full/pyodide.mjs';
const base = new URL('./', import.meta.url);
const deps = {
  asset: async p => new Uint8Array(await (await fetch(new URL(p, base))).arrayBuffer()),
  log: t => postMessage({ log: t }),
  cache: objectCache,
};

onmessage = async ({ data: { yaml } }) => {
  try {
    deps.log('Loading Pyodide');
    deps.pyodide = await (await import(PYODIDE)).loadPyodide({ stdout: deps.log, stderr: deps.log });
    const src = await codegen(yaml, deps);
    deps.log('Loading clang/lld (WebAssembly, first run downloads ~100 MB)');
    deps.runLLVM = (await import(new URL('clang/bundle.js', base))).runLLVM;
    const bin = await compileAndLink(src, deps);
    postMessage({ done: bin }, [bin.buffer]);
  } catch (e) {
    postMessage({ error: String(e?.stack || e) });
  }
};
postMessage({ ready: true });
