// Web UI for aqara-ir-esphome. Loaded by the small index.html the P3 serves (device/web/index.html);
// `device` is that page's origin, which provides /api/* (device/web/handler.sh).
const MAGIC = '1189998819';
const hex = s => [...new TextEncoder().encode(s)].map(b => b.toString(16).padStart(2, '0')).join('') + '00';
const unhex = h => new TextDecoder().decode(new Uint8Array((h.match(/../g) || []).map(x => parseInt(x, 16)))).replace(/\0+$/, '');

const CSS = `
body{font:15px/1.5 system-ui,sans-serif;background:#f6f7f9;color:#1f2328;margin:0}
main{max-width:860px;margin:0 auto;padding:24px}
h1{font-size:22px;margin:0 0 16px}
section{background:#fff;border:1px solid #d8dee4;border-radius:8px;padding:16px;margin-bottom:16px}
h2{font-size:16px;margin:0 0 12px}
dl{display:grid;grid-template-columns:max-content 1fr;gap:4px 16px;margin:0}
dt{color:#59636e}dd{margin:0;font-family:ui-monospace,monospace;word-break:break-all}
textarea{width:100%;min-height:320px;box-sizing:border-box;font:13px/1.4 ui-monospace,monospace;border:1px solid #d8dee4;border-radius:6px;padding:8px}
button{font:inherit;padding:6px 14px;border:1px solid #d1d9e0;border-radius:6px;background:#f6f8fa;cursor:pointer;margin:8px 8px 0 0}
button.primary{background:#1f883d;border-color:#1a7f37;color:#fff}
button:disabled{opacity:.5;cursor:default}
input{font:inherit;padding:5px 8px;border:1px solid #d1d9e0;border-radius:6px;width:12em}
pre{background:#f6f8fa;border:1px solid #d8dee4;border-radius:6px;padding:8px;max-height:280px;overflow:auto;font-size:12px;margin:12px 0 0;white-space:pre-wrap}
.ok{color:#1a7f37}.warn{color:#9a6700}`;

export async function start(base, device) {
  // The P3 serves one connection at a time (busybox nc in a loop), so a request that arrives
  // while nc restarts is refused: retry network errors, never HTTP errors.
  const api = async (path, opts) => {
    for (let i = 0; ; i++) {
      let r;
      try { r = await fetch(device + path, opts); } catch (e) {
        if (i >= 25) throw e;
        await new Promise(res => setTimeout(res, 200));
        continue;
      }
      const body = await r.text();
      if (!r.ok) throw new Error(body);
      return body;
    }
  };
  document.head.insertAdjacentHTML('beforeend', `<style>${CSS}</style>`);
  document.body.innerHTML = `<main>
    <h1>Aqara P3 · ESPHome IR</h1>
    <section><h2>Status</h2><dl id=status></dl>
      <button id=use-esphome>Use ESPHome encoder</button>
      <button id=use-stock>Use stock encoder, remote id</button><input id=stock-id placeholder="e.g. 1881285153">
    </section>
    <section><h2>ESPHome config <small>(irgen.yaml on the device)</small></h2>
      <textarea id=yaml spellcheck=false></textarea>
      <button id=save>Save config</button>
      <button id=build class=primary>Build in browser and install</button>
      <button id=clear-cache>Clear build cache</button>
      <pre id=log hidden></pre>
    </section></main>`;
  const $ = id => document.getElementById(id);
  const log = t => { $('log').hidden = false; $('log').textContent += t + '\n'; $('log').scrollTop = 1e9; };

  async function refresh() {
    const s = JSON.parse(await api('/api/status'));
    const rid = unhex(s.r_id_hex);
    const esphome = rid === MAGIC;
    $('status').innerHTML = `
      <dt>Encoder</dt><dd class=${esphome ? 'ok' : 'warn'}>${esphome ? 'ESPHome (r_id ' + MAGIC + ')' : 'stock (r_id ' + rid + ')'}</dd>
      <dt>mha_ir loads</dt><dd class=${s.mha_ir.includes('libirplug') ? 'ok' : 'warn'}>${s.mha_ir || 'not running'}</dd>
      <dt>irgen md5</dt><dd>${s.irgen_md5 || 'missing'}</dd>
      <dt>IR UART tx bytes</dt><dd>${s.ir_uart_tx}</dd>`;
    if (!esphome && !$('stock-id').value) $('stock-id').value = rid;
  }
  // buildCache (device/web/index.html) holds the downloads and the compiled objects.
  const showCacheSize = async () =>
    $('clear-cache').textContent = `Clear build cache (${((await buildCache.size()) / 1048576).toFixed(1)} MB)`;

  $('yaml').value = await api('/api/config');
  await refresh();
  await showCacheSize();

  $('use-esphome').onclick = async () => { log(await api('/api/use-esphome', { method: 'PUT' })); setTimeout(refresh, 3000); };
  $('use-stock').onclick = async () => {
    const id = $('stock-id').value.trim();
    if (!/^\d+$/.test(id)) return log('Enter the numeric remote id to go back to.');
    log(await api('/api/use-stock', { method: 'PUT', body: hex(id) })); setTimeout(refresh, 3000);
  };
  $('save').onclick = async () => log(await api('/api/config', { method: 'PUT', body: $('yaml').value }));
  $('clear-cache').onclick = async () => { await buildCache.clear(); await showCacheSize(); };

  $('build').onclick = async () => {
    $('build').disabled = true;
    $('log').textContent = '';
    const t0 = Date.now();
    try {
      await api('/api/config', { method: 'PUT', body: $('yaml').value });
      // Workers must be same-origin: a Blob URL runs the page's offline loader, then the real worker.
      const boot = URL.createObjectURL(new Blob([`${loaderSource}
importCached(${JSON.stringify(base + 'worker.js')}).catch(e => postMessage({ error: String(e?.stack || e) }));`],
        { type: 'text/javascript' }));
      const worker = new Worker(boot, { type: 'module' });
      const bin = await new Promise((resolve, reject) => {
        // worker.js posts {ready} once its module has loaded; only then send the config
        worker.onmessage = ({ data }) => data.ready ? worker.postMessage({ yaml: $('yaml').value })
          : data.log ? log(data.log) : data.done ? resolve(data.done) : reject(new Error(data.error));
        worker.onerror = e => reject(new Error(e.message));
      });
      worker.terminate();
      log(`Built ${bin.length} bytes in ${Math.round((Date.now() - t0) / 1000)} s, uploading`);
      log(await api('/api/irgen', { method: 'PUT', body: bin }));
      await refresh();
    } catch (e) {
      log('ERROR: ' + e.message);
    } finally {
      $('build').disabled = false;
      await showCacheSize();
    }
  };
}
