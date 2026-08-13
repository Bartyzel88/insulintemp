# InsuTemp — zawartość paczki

**Wersja v3 z 2026-08-13.** Baza to v2 (niskopoborowa) plus trzy poprawki po przeglądzie:
częstszy heartbeat diody, eskalacja alarmu krytycznego i zmniejszone limity pamięci trwałej.

> **Pliki aplikacji się nie zmieniły.** Jeśli masz już wgraną v2 na GitHub Pages,
> nie musisz nic tam wgrywać ani podnosić `VERSION` w `sw.js`. Zmieniły się wyłącznie
> `insulin_temp.ino` i `INSTRUKCJA.md`.
 Paczka zawiera **11 plików**: instrukcję, ten spis zawartości, firmware oraz 8 plików aplikacji PWA.

Zacznij od **INSTRUKCJA.md**.

---

## Gdzie co trafia

### Na komputer, do Arduino IDE

Folder szkicu musi nazywać się `insulin_temp`, tak samo jak plik `.ino`:

```text
insulin_temp/
└── insulin_temp.ino
```

### Do repozytorium GitHub Pages

Skopiuj **wszystkie osiem plików** z folderu `github/` płasko do katalogu publikowanego przez GitHub Pages:

```text
index.html
manifest.json
sw.js
icon-192.png
icon-512.png
icon-512-maskable.png
apple-touch-icon.png
favicon-32.png
```

Nie każda z ikon jest bezwzględnym warunkiem instalacji PWA na każdej wersji Chrome, ale komplet zapewnia poprawne odwołania z `index.html`/manifestu i najlepszą zgodność z Androidem oraz skrótami na ekranie głównym.

### Tylko dla Ciebie

`INSTRUKCJA.md` i `ZAWARTOSC.md` nie muszą trafiać ani do Arduino IDE, ani do GitHub Pages.

---

## Lista kontrolna plików

| Plik | Dokąd | Bajtów | SHA-256 (pierwsze 12 znaków) |
|---|---|---:|---|
| `INSTRUKCJA.md` | dokument | 41 453 | `09487f1fb320` |
| `insulin_temp.ino` | Arduino | 49 028 | `b4577052533e` |
| `index.html` | GitHub | 31 239 | `00455f53f329` |
| `manifest.json` | GitHub | 878 | `12ab7c9590ec` |
| `sw.js` | GitHub | 1 994 | `1ca89aad5a94` |
| `icon-192.png` | GitHub | 5 910 | `294282d292a6` |
| `icon-512.png` | GitHub | 15 440 | `c542fd4e967d` |
| `icon-512-maskable.png` | GitHub | 8 778 | `88a0991793a2` |
| `apple-touch-icon.png` | GitHub | 5 188 | `4eb876a49868` |
| `favicon-32.png` | GitHub | 1 188 | `e4edb94bc509` |

`ZAWARTOSC.md` celowo nie ma własnego hasha w tej tabeli — wpisanie jego sumy zmieniłoby sam plik i tym samym jego sumę.

Przykładowa weryfikacja:

```bash
# Linux / macOS
sha256sum INSTRUKCJA.md insulin_temp/insulin_temp.ino github/*

# Windows PowerShell
Get-FileHash .\INSTRUKCJA.md, .\insulin_temp\insulin_temp.ino, .\github\* -Algorithm SHA256
```

---

## Do czego służy każdy plik

| Plik | Rola |
|---|---|
| `INSTRUKCJA.md` | Pełna, poprawiona instrukcja montażu, kalibracji, testowania, używania i oszczędzania energii. |
| `ZAWARTOSC.md` | Ten spis plików i sum kontrolnych. |
| `insulin_temp.ino` | Firmware czujnika: pomiary, ręczny tryb przechowywania, alarmy, pamięć trwała, BLE/BTHome i log. |
| `index.html` | Aplikacja Web Bluetooth/PWA; pokazuje stan, tryb, ostrzeżenia, historię i dane diagnostyczne. |
| `manifest.json` | Metadane PWA. |
| `sw.js` | Obsługa offline i cache. Aktualna wersja cache: **insutemp-v8**. |
| `icon-192.png` | Ikona PWA 192×192. |
| `icon-512.png` | Ikona PWA 512×512. |
| `icon-512-maskable.png` | Ikona maskowalna 512×512 dla Androida. |
| `apple-touch-icon.png` | Ikona skrótu dla urządzeń Apple/przeglądarek, które jej używają. |
| `favicon-32.png` | Favicon 32×32. |

---

## Najważniejsze różnice tej wersji

- krytyczne alarmy i tryb są rzeczywiście zapisywane do pamięci nieulotnej;
- liczniki bazują na **rzeczywistym czasie**, a nie liczbie próbek;
- zapis flash jest zdarzeniowy/checkpointowany, a nie wykonywany co minutę;
- awaria lodówki nie może automatycznie przełączyć urządzenia w łagodniejszy tryb transportu;
- błąd pamięci trwałej jest jawnie sygnalizowany w aplikacji i LED;
- zmniejszono pobór energii: krótsza konwersja DS18B20, rzadszy TMP117 i pomiar baterii, wolniejsze advertising BLE, bardzo rzadkie błyski historycznych alarmów oraz deep power-down zewnętrznej QSPI;
- poprawiono BTHome, obsługę starego odczytu po rozłączeniu, CSV oraz komunikaty dotyczące dokładności i przechowywania insuliny;
- frontend nie korzysta z zewnętrznych fontów/CDN.

---

## Po własnych zmianach

Po każdej zmianie plików aplikacji podnieś `VERSION` na górze `github/sw.js` (np. `insutemp-v8` → `insutemp-v9`), żeby stary cache został unieważniony. Zmiana `insulin_temp.ino` wymaga ponownego wgrania firmware przez Arduino IDE.

**To urządzenie domowej roboty, nie wyrób medyczny ani certyfikowany rejestrator temperatury.** Szczegóły i ograniczenia są w `INSTRUKCJA.md`.
