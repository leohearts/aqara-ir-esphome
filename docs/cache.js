// Compiled-object cache in IndexedDB, shared by worker.js (get/put) and app.js (size/clear).
// No expiry of our own: the browser evicts this best-effort storage when it needs the space.
// (Cache Storage and navigator.storage are not available on the http:// device page.)
const db = new Promise((resolve, reject) => {
  const r = indexedDB.open('aqara-ir-esphome', 1);
  r.onupgradeneeded = () => r.result.createObjectStore('obj');
  r.onsuccess = () => resolve(r.result);
  r.onerror = () => reject(r.error);
});
const idb = (mode, op) => db.then(d => new Promise((resolve, reject) => {
  const r = op(d.transaction('obj', mode).objectStore('obj'));
  r.onsuccess = () => resolve(r.result);
  r.onerror = () => reject(r.error);
}));

export const objectCache = {
  get: key => idb('readonly', s => s.get(key)),
  put: (key, value) => idb('readwrite', s => s.put(value, key)),
  clear: () => idb('readwrite', s => s.clear()),
  // total bytes of the cached objects
  size: async () => (await idb('readonly', s => s.getAll())).reduce((n, v) => n + v.byteLength, 0),
};
