// Builds irgen (ESPHome host build -> static MIPS32r2 binary) entirely in WebAssembly.
// Shared by the web UI and the node test (test/build-node.mjs); the caller injects the runtimes.
//
// deps = {
//   pyodide,            // a loaded Pyodide instance
//   runLLVM,            // @yowasp/clang runLLVM built with the Mips backend
//   asset(path),        // -> Promise<Uint8Array> for files next to this script
//   log(text),          // progress output
//   cache,              // optional {get(key), put(key, Uint8Array)} for compiled objects
// }

const ESPHOME_VERSION = '2026.11.0';
const PY_DEPS = ['voluptuous', 'click', 'colorama', 'platformdirs', 'tzlocal', 'tzdata', 'ruamel.yaml',
  'filelock', 'puremagic', 'argcomplete', 'paho-mqtt', 'requests', 'pyserial'];
const dec = new TextDecoder();
const enc = new TextEncoder();

// Two cyrb53-style 53-bit hashes -> 28 hex chars. crypto.subtle is not available on the
// http:// device page.
function hash(parts) {
  let out = '';
  for (const seed of [1, 2]) {
    let h1 = 0xdeadbeef ^ seed, h2 = 0x41c6ce57 ^ seed;
    for (const p of parts) for (let i = 0; i < p.length; i++) {
      h1 = Math.imul(h1 ^ p[i], 2654435761);
      h2 = Math.imul(h2 ^ p[i], 1597334677);
    }
    h1 = Math.imul(h1 ^ (h1 >>> 16), 2246822507) ^ Math.imul(h2 ^ (h2 >>> 13), 3266489909);
    h2 = Math.imul(h2 ^ (h2 >>> 16), 2246822507) ^ Math.imul(h1 ^ (h1 >>> 13), 3266489909);
    out += (4294967296 * (2097151 & h2) + (h1 >>> 0)).toString(16).padStart(14, '0');
  }
  return out;
}

// Hash input for named byte strings; names and lengths keep the boundaries unambiguous.
const named = entries => entries.flatMap(([name, b]) => [enc.encode(`${name}\0${b.length}\0`), b]);

// Minimal ustar reader for sysroot.tar.gz -> Tree {name: Uint8Array | Tree}.
async function untarGz(bytes) {
  const tar = new Uint8Array(await new Response(
    new Blob([bytes]).stream().pipeThrough(new DecompressionStream('gzip'))).arrayBuffer());
  const root = {};
  for (let off = 0; off + 512 <= tar.length;) {
    const hdr = tar.subarray(off, off + 512);
    if (hdr[0] === 0) break;
    const str = (a, b) => dec.decode(hdr.subarray(a, b)).replace(/\0.*$/s, '');
    const name = (str(345, 500) ? str(345, 500) + '/' : '') + str(0, 100);
    const size = parseInt(str(124, 136).trim() || '0', 8);
    const type = String.fromCharCode(hdr[156]);
    off += 512;
    if (type === '0' || type === '\0') {
      const parts = name.split('/').filter(Boolean);
      let dir = root;
      for (const p of parts.slice(0, -1)) dir = dir[p] ??= {};
      dir[parts.at(-1)] = tar.slice(off, off + size);
    }
    off += Math.ceil(size / 512) * 512;
  }
  return root;
}

// Pyodide FS directory -> Tree of .cpp/.h files.
function readTree(FS, path) {
  const tree = {};
  for (const name of FS.readdir(path)) {
    if (name === '.' || name === '..') continue;
    const full = `${path}/${name}`;
    if (FS.isDir(FS.stat(full).mode)) tree[name] = readTree(FS, full);
    else tree[name] = FS.readFile(full);
  }
  return tree;
}

// Tree -> [path, bytes] pairs, in a stable order.
function* walk(tree, prefix = '') {
  for (const name of Object.keys(tree).sort()) {
    const v = tree[name];
    if (v instanceof Uint8Array) yield [prefix + name, v];
    else yield* walk(v, `${prefix}${name}/`);
  }
}

const getPath = (tree, path) => path.split('/').reduce((t, p) => t?.[p], tree);

export async function codegen(yaml, { pyodide, log }) {
  const { FS } = pyodide;
  log('Installing ESPHome into Pyodide');
  await pyodide.loadPackage(['micropip', 'pyyaml', 'jinja2', 'cryptography', 'pyparsing']);
  FS.mkdirTree('/work');
  FS.writeFile('/work/config.yaml', yaml);
  log('Generating C++ with ESPHome');
  await pyodide.runPythonAsync(`
import micropip, os, json
await micropip.install('esphome==${ESPHOME_VERSION}', deps=False)
await micropip.install(${JSON.stringify(PY_DEPS)})

# There is no git in Pyodide: fetch github:// external components through jsDelivr and point the
# config at the local copy. Everything else in the YAML is left to ESPHome.
from pyodide.http import pyfetch
from ruamel.yaml import YAML
y = YAML()
conf = y.load(open('/work/config.yaml'))
for ext in conf.get('external_components') or []:
    src = ext.get('source')
    url = src if isinstance(src, str) else (src.get('url') if src.get('type') == 'git' else None)
    if not url or not url.startswith(('github://', 'https://github.com/')):
        continue
    repo = url.replace('github://', '').replace('https://github.com/', '').removesuffix('.git')
    repo, _, ref = repo.partition('@')
    ref = ref or (src.get('ref') if isinstance(src, dict) else None) or 'HEAD'
    owner_repo = '/'.join(repo.split('/')[:2])
    sub = '/'.join(repo.split('/')[2:]) or 'components'
    listing = await (await pyfetch(f'https://data.jsdelivr.com/v1/packages/gh/{owner_repo}@{ref}?structure=flat')).json()
    dest = f'/work/ext/{owner_repo.replace("/", "_")}'
    for f in listing['files']:
        if not f['name'].startswith('/' + sub + '/'):
            continue
        data = await (await pyfetch(f'https://cdn.jsdelivr.net/gh/{owner_repo}@{ref}{f["name"]}')).bytes()
        os.makedirs(os.path.dirname(dest + f['name']), exist_ok=True)
        open(dest + f['name'], 'wb').write(data)
    ext['source'] = {'type': 'local', 'path': f'{dest}/{sub}'}
y.dump(conf, open('/work/config.yaml', 'w'))

os.chdir('/work')
from esphome.__main__ import run_esphome
rc = run_esphome(['esphome', 'compile', '--only-generate', 'config.yaml'])
if rc:
    raise RuntimeError(f'esphome codegen failed with {rc}')
`);
  const name = pyodide.runPython(`
from ruamel.yaml import YAML
YAML().load(open('/work/config.yaml'))['esphome']['name']`);
  return readTree(FS, `/work/.esphome/build/${name}/src`);
}

export async function compileAndLink(src, { runLLVM, asset, log, cache = null, jobs = null }) {
  const [cc1Raw, link, sysrootGz] = await Promise.all([
    asset('cc1-args.json'),
    asset('link-args.json').then(b => JSON.parse(dec.decode(b))),
    asset('sysroot.tar.gz'),
  ]);
  const cc1 = JSON.parse(dec.decode(cc1Raw));
  const sysroot = await untarGz(sysrootGz);
  const files = { sysroot: sysroot.sysroot ?? sysroot, work: { src } };
  const all = [...walk(src)];
  const sources = all.filter(([p]) => p.endsWith('.cpp')).map(([p]) => p);
  // An object depends on the compiler args, the sysroot, every header and its own source.
  const common = cache && hash(named([['cc1', cc1Raw], ['sysroot', sysrootGz],
    ...all.filter(([p]) => !p.endsWith('.cpp'))]));
  const objs = {};
  let done = 0;
  // The object goes to stdout (`-o -`): writing a file in YoWASP's virtual FS fails with
  // "IO failure on output stream: Invalid argument" (seek/rename), a pipe makes clang buffer it.
  const one = async (rel) => {
    const key = cache && `obj:${common}:${hash(named([[rel, getPath(src, rel)]]))}`;
    const cached = key && await cache.get(key);
    let obj = cached;
    if (!obj) {
      const args = cc1.map(a => a === '{src}' ? `/work/src/${rel}` : a === '{out}' ? '-'
        : a === '{name}' ? rel.split('/').at(-1) : a);
      const chunks = [];
      await runLLVM(['clang', ...args], files, {
        stdout: b => b && chunks.push(new Uint8Array(b)),
        stderr: b => b && log(dec.decode(b)),
      });
      obj = new Uint8Array(await new Blob(chunks).arrayBuffer());
      if (key) await cache.put(key, obj);
    }
    objs[`${rel.replaceAll('/', '_')}.o`] = obj;
    log(`${cached ? 'cached' : 'compiled'} ${++done}/${sources.length} ${rel}`);
  };
  // jobs: optional function(list, worker) to fan out; default sequential.
  if (jobs) await jobs(sources, one); else for (const rel of sources) await one(rel);

  log('Linking');
  const args = link.flatMap(a => a === '{objs}' ? Object.keys(objs).map(o => `/work/obj/${o}`)
    : a === '{out}' ? '/work/irgen' : [a]);
  const result = await runLLVM(args, { sysroot: files.sysroot, work: { obj: objs } },
    { stderr: b => b && log(dec.decode(b)) });
  return getPath(result, 'work/irgen');
}
