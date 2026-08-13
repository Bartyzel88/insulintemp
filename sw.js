/*
 * Service worker InsuTemp.
 *
 * Strategia:
 *   dokument HTML  -> najpierw siec, cache jako awaryjny
 *   lokalne zasoby -> najpierw cache
 *
 * Aplikacja nie korzysta z zewnetrznych fontow ani bibliotek CDN, dlatego po
 * pierwszym poprawnym zaladowaniu cala powloka aplikacji moze dzialac offline.
 *
 * Po kazdej zmianie plikow PODNIES numer wersji ponizej, aby uniewaznic stary cache.
 */

const VERSION = "insutemp-v11";

const SHELL = [
  "./",
  "./index.html",
  "./manifest.json",
  "./icon-192.png",
  "./icon-512.png",
  "./icon-512-maskable.png",
  "./apple-touch-icon.png",
  "./favicon-32.png"
];

self.addEventListener("install", event => {
  event.waitUntil(
    caches.open(VERSION)
      .then(cache => Promise.allSettled(SHELL.map(url => cache.add(url))))
      .then(() => self.skipWaiting())
  );
});

self.addEventListener("activate", event => {
  event.waitUntil(
    caches.keys()
      .then(keys => Promise.all(
        keys.filter(k => k !== VERSION).map(k => caches.delete(k))
      ))
      .then(() => self.clients.claim())
  );
});

self.addEventListener("fetch", event => {
  const req = event.request;
  if (req.method !== "GET") return;

  const url = new URL(req.url);
  if (url.origin !== self.location.origin) return;

  // Dokument: najpierw siec. Swieza wersja wygrywa, cache ratuje offline.
  if (req.mode === "navigate" || req.destination === "document") {
    event.respondWith(
      fetch(req)
        .then(res => {
          if (res.ok) caches.open(VERSION).then(c => c.put("./index.html", res.clone()));
          return res;
        })
        .catch(() => caches.match("./index.html").then(hit => hit || caches.match("./")))
    );
    return;
  }

  // Lokalne zasoby statyczne: najpierw cache.
  event.respondWith(
    caches.match(req).then(hit => {
      if (hit) return hit;
      return fetch(req).then(res => {
        if (res.ok) caches.open(VERSION).then(c => c.put(req, res.clone()));
        return res;
      });
    })
  );
});
