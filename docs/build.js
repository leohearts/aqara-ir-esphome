// Builds irgen (ESPHome host build -> static MIPS32r2 binary) entirely in WebAssembly.
// Shared by the web UI and the node test (test/build-node.mjs); the caller injects the runtimes.
//
// deps = {
//   pyodide,            // a loaded Pyodide instance
//   runLLVM,            // @yowasp/clang runLLVM built with the Mips backend
//   asset(path),        // -> Promise<Uint8Array> for files next to this script
//   log(text),          // progress output
// }

const ESPHOME_VERSION = '2026.11.0';
const PY_DEPS = ['voluptuous', 'click', 'colorama', 'platformdirs', 'tzlocal', 'tzdata', 'ruamel.yaml',
  'filelock', 'puremagic', 'argcomplete', 'paho-mqtt', 'requests', 'pyserial'];
// From src/irgen/components (copied into the Pages site by .github/workflows/pages.yml).
const BUNDLED_COMPONENTS = {
  aqara_ir: ['__init__.py', 'aqara_ir.h'],
  remote_transmitter: ['__init__.py', 'remote_transmitter.h'],
  remote_receiver: ['__init__.py', 'remote_receiver.h'],
};

const dec = new TextDecoder();

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

function* cppFiles(tree, prefix = '') {
  for (const [name, v] of Object.entries(tree)) {
    if (v instanceof Uint8Array) { if (name.endsWith('.cpp')) yield prefix + name; }
    else yield* cppFiles(v, `${prefix}${name}/`);
  }
}

const getPath = (tree, path) => path.split('/').reduce((t, p) => t?.[p], tree);

export async function codegen(yaml, { pyodide, asset, log }) {
  const { FS } = pyodide;
  log('Installing ESPHome into Pyodide');
  await pyodide.loadPackage(['micropip', 'pyyaml', 'jinja2', 'cryptography', 'pyparsing']);
  for (const [dir, files] of Object.entries(BUNDLED_COMPONENTS)) {
    FS.mkdirTree(`/work/components/${dir}`);
    for (const f of files) FS.writeFile(`/work/components/${dir}/${f}`, await asset(`components/${dir}/${f}`));
  }
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

export async function compileAndLink(src, { runLLVM, asset, log, jobs = null }) {
  const [cc1, link, sysroot] = await Promise.all([
    asset('cc1-args.json').then(b => JSON.parse(dec.decode(b))),
    asset('link-args.json').then(b => JSON.parse(dec.decode(b))),
    asset('sysroot.tar.gz').then(untarGz),
  ]);
  const files = { sysroot: sysroot.sysroot ?? sysroot, work: { src } };
  const sources = [...cppFiles(src)];
  const objs = {};
  let done = 0;
  // The object goes to stdout (`-o -`): writing a file in YoWASP's virtual FS fails with
  // "IO failure on output stream: Invalid argument" (seek/rename), a pipe makes clang buffer it.
  const one = async (rel) => {
    const args = cc1.map(a => a === '{src}' ? `/work/src/${rel}` : a === '{out}' ? '-'
      : a === '{name}' ? rel.split('/').at(-1) : a);
    const chunks = [];
    await runLLVM(['clang', ...args], files, {
      stdout: b => b && chunks.push(new Uint8Array(b)),
      stderr: b => b && log(dec.decode(b)),
    });
    objs[`${rel.replaceAll('/', '_')}.o`] = new Uint8Array(await new Blob(chunks).arrayBuffer());
    log(`compiled ${++done}/${sources.length} ${rel}`);
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
