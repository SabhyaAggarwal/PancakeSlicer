// A disposable cache of service responses that never change: a finished job's toolpaths and G-code
// (by job id) and project thumbnails (by version). Only a 200 whose Cache-Control the service marks
// `immutable` is kept, so a URL class listed here still caches nothing the service has not promised
// is final. User state, mutations, the app's modules and the HMR socket never reach this worker:
// browsers with static routing (Chrome 123+) send every other request straight to the network.
//
// Kill switch: open the app with ?sw=off (or set localStorage orca.sw = 'off'); src/sw.ts then
// unregisters the worker and deletes its caches. Bumping VERSION drops every older cache on activate.
const VERSION = 1;
const CACHE = `orca-immutable-v${VERSION}`;
const MAX_ENTRIES = 48;
const IMMUTABLE = [/^\/api\/jobs\/[^/]+\/(?:moves|download)$/, /^\/api\/project-thumbnails\/[^/]+$/];
const JOB = /^\/api\/jobs\/([^/]+)$/;

self.addEventListener('install', (event) => {
  self.skipWaiting();
  if (!event.addRoutes || typeof URLPattern === 'undefined') return;
  const fetchEvent = (pathname) => ({ condition: { urlPattern: new URLPattern({ pathname }) }, source: 'fetch-event' });
  // First match wins: the job and thumbnail routes reach the handler, everything else the network.
  event.waitUntil(event.addRoutes([fetchEvent('/api/jobs/*'), fetchEvent('/api/project-thumbnails/*'),
    { condition: { urlPattern: new URLPattern({ pathname: '/*' }) }, source: 'network' }]).catch(() => {}));
});

self.addEventListener('activate', (event) => {
  event.waitUntil((async () => {
    for (const name of await caches.keys()) if (name.startsWith('orca-immutable-') && name !== CACHE) await caches.delete(name);
    await self.clients.claim();
  })());
});

self.addEventListener('fetch', (event) => {
  const request = event.request;
  const url = new URL(request.url);
  if (url.origin !== self.location.origin) return;
  // A deleted job's files go with it.
  const job = request.method === 'DELETE' && JOB.exec(url.pathname);
  if (job) {
    event.waitUntil(forget(`/api/jobs/${job[1]}/`));
    return;
  }
  if (request.method !== 'GET' || request.headers.has('range') || !IMMUTABLE.some((r) => r.test(url.pathname))) return;
  event.respondWith(cacheFirst(request));
});

async function cacheFirst(request) {
  const cache = await caches.open(CACHE);
  const hit = await cache.match(request);
  if (hit) return hit;
  const response = await fetch(request);
  if (response.status === 200 && /\bimmutable\b/.test(response.headers.get('Cache-Control') ?? '')) {
    cache.put(request, response.clone()).then(() => trim(cache), () => {});
  }
  return response;
}

// Oldest first: Cache Storage keeps insertion order.
async function trim(cache) {
  const keys = await cache.keys();
  for (const key of keys.slice(0, Math.max(0, keys.length - MAX_ENTRIES))) await cache.delete(key);
}

async function forget(prefix) {
  const cache = await caches.open(CACHE);
  for (const key of await cache.keys()) if (new URL(key.url).pathname.startsWith(prefix)) await cache.delete(key);
}
