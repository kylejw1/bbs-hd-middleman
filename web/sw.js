// Service worker for the hosted dashboard.
//
// This exists for exactly one reason: Web Bluetooth needs a secure context, and
// a secure context needs HTTPS, so the Bluetooth-capable copy of the dashboard
// lives on GitHub Pages rather than on the ESP32. Caching the shell means that
// copy still opens with no network at all -- which is the whole point, since the
// bike is usually nowhere near an access point.
//
// The API never goes through here: on the hosted copy every /api/* call is a BLE
// GATT request, not a fetch. This worker only serves the app shell.
//
// Strategy: cache-first with a background refresh (stale-while-revalidate), so
// launching offline is instant but a newer build is still picked up whenever the
// device does have connectivity.

const CACHE_NAME = 'bbshd-shell-v1';

const SHELL = [
  './',
  './index.html',
  './manifest.webmanifest',
  './icons/icon-192.png',
  './icons/icon-512.png',
  './icons/icon-maskable-512.png',
];

self.addEventListener('install', (event) => {
  event.waitUntil(
    caches.open(CACHE_NAME)
      .then((cache) => cache.addAll(SHELL))
      .then(() => self.skipWaiting())
  );
});

self.addEventListener('activate', (event) => {
  event.waitUntil(
    caches.keys()
      .then((keys) => Promise.all(keys.map((key) => {
        if (key !== CACHE_NAME) return caches.delete(key);
        return undefined;
      })))
      .then(() => self.clients.claim())
  );
});

self.addEventListener('fetch', (event) => {
  const request = event.request;

  // Never interfere with anything that is not a plain same-origin GET.
  if (request.method !== 'GET') return;
  const url = new URL(request.url);
  if (url.origin !== self.location.origin) return;

  event.respondWith((async () => {
    const cache = await caches.open(CACHE_NAME);
    const cached = await cache.match(request);

    const refresh = fetch(request).then((response) => {
      if (response && response.ok) {
        cache.put(request, response.clone());
      }
      return response;
    }).catch(() => null);

    if (cached) {
      event.waitUntil(refresh);
      return cached;
    }

    const fresh = await refresh;
    if (fresh) return fresh;

    // Offline and never cached: fall back to the shell so a deep link still
    // opens the app, which then reconnects over Bluetooth.
    const shell = await cache.match('./index.html');
    if (shell) return shell;

    return new Response('Offline and no cached dashboard available.', {
      status: 503,
      headers: { 'Content-Type': 'text/plain' },
    });
  })());
});
