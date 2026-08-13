/*
 * Service worker InsuTemp.
 *
 * Strategia jest celowo mieszana, bo dwa cele sie tu gryza: aplikacja ma
 * dzialac bez sieci przy lodowce, ale poprawka w kodzie ma byc widoczna od razu,
 * a nie po tygodniu.
 *
 *   dokument HTML  -> najpierw siec, cache jako awaryjny
 *                     (poprawki widoczne natychmiast, offline nadal dziala)
 *   ikony, manifest -> najpierw cache
 *   fonty Google    -> najpierw cache, dociagane w tle po pierwszym uzyciu
 *
 * Po kazdej zmianie plikow PODNIES numer wersji ponizej. Bez tego stare
 * zasoby zostana w cache.
 */

const VERSION = "insutemp-v1";

const SHELL = [
  "./",
  "./index.html",
  "./manifest.webmanifest",
  "./icon-192.png",
  "./icon-512.png",
  "./icon-512-maskable.png",
  "./apple-touch-icon.png",
  "./favicon-32.png"
];

self.addEventListener("install", event => {
  event.waitUntil(
    caches.open(VERSION)
      // addAll przerywa calosc przy jednym bledzie, wiec kazdy zasob osobno
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
  const isFont = url.hostname === "fonts.googleapis.com" ||
                 url.hostname === "fonts.gstatic.com";

  // Dokument: najpierw siec. Swieza wersja wygrywa, cache ratuje offline.
  if (req.mode === "navigate" || req.destination === "document") {
    event.respondWith(
      fetch(req)
        .then(res => {
          caches.open(VERSION).then(c => c.put("./index.html", res.clone()));
          return res;
        })
        .catch(() => caches.match("./index.html")
                       .then(hit => hit || caches.match("./")))
    );
    return;
  }

  // Reszta: najpierw cache, w tle uzupelniaj.
  if (url.origin === self.location.origin || isFont) {
    event.respondWith(
      caches.match(req).then(hit => {
        if (hit) return hit;
        return fetch(req).then(res => {
          // Odpowiedzi cross-origin sa nieprzezroczyste, ale daja sie zapisac.
          if (res.ok || res.type === "opaque") {
            caches.open(VERSION).then(c => c.put(req, res.clone()));
          }
          return res;
        });
      })
    );
  }
});
