/*
 * insulin_temp - monitor temperatury insuliny
 * -------------------------------------------------------------------
 * Sprzet : Seeed XIAO nRF52840 (Plus)
 *          + DS18B20 w sondzie ze stali (D2) - sonda przy opakowaniu, kanal glowny
 *          + Adafruit TMP117 @ 0x48 (D4/D5)  - temperatura POWIETRZA, kanal pomocniczy
 * Rdzen  : "Seeed nRF52 Boards" (wersja NIE-mbed, bazujaca na Adafruit nRF52)
 * Biblioteki: Adafruit SPIFlash, OneWire
 *
 * Podzial rol miedzy czujnikami:
 *   sonda w kartoniku -> alarmy, zatrzaski i log; przybliza temperature
 *                        opakowania / insuliny, ale nie mierzy cieczy bezposrednio.
 *   TMP117 w powietrzu -> kanal pomocniczy. Nie zmienia automatycznie progow;
 *                        tylko ostrzega, gdy wybrany tryb nie pasuje do otoczenia.
 *
 * UWAGA LOW-POWER: breakout Adafruit 4821 ma stale zasilana diode ON. Sam TMP117
 * jest bardzo oszczedny w one-shot, ale dioda modulu nie jest sterowana programowo.
 * Dla dlugiego czasu pracy uzyj wersji bez stalej diody zasilania albo usun/odlacz
 * te diode na plytce po uruchomieniu i testach (najlepiej przez osobe z doswiadczeniem SMD).
 *
 * Gdy sonda przestanie odpowiadac, urzadzenie NIE przelacza alarmow na
 * powietrze. Powietrze w lodowce rutynowo schodzi ponizej 2 C i zalatwiloby
 * log falszywymi zatrzaskami. Zamiast tego zglasza awarie i przestaje
 * zatrzaskiwac - lepiej wiedziec, ze sie nie wie.
 *
 * Co robi:
 *  - mierzy temperature sondy co 60 s; DS18B20 pracuje w 11 bitach, co skraca
 *    konwersje do ~375 ms i zmniejsza pobor energii bez istotnej utraty informacji
 *  - tryb LODOWKA / POZA LODOWKA wybiera uzytkownik; TMP117 tylko wykrywa
 *    niezgodnosc trybu z otoczeniem, aby nie poluzowac progow przez awarie lodowki
 *  - alarmuje wg progow wlasciwych dla wybranego trybu
 *  - trwale zatrzaskuje krytyczne przekroczenia oraz liczniki czasu
 *  - sygnalizuje stan wbudowana dioda RGB: niebieski = zimno, czerwony = cieplo,
 *    liczba blyskow = ciezar (1 teraz, 2 bylo, 3 nieodwracalne)
 *  - rozglasza dane w formacie BTHome v2 -> Home Assistant sam je wykryje
 *  - zapisuje zdarzenia do wewnetrznej pamieci flash (przetrwa restart)
 *  - log mozna zrzucic przez BLE UART (aplikacja nRF Connect / Serial BT Terminal)
 *
 * Komendy BLE UART (wpisz i wyslij):
 *   D            zrzut logu jako CSV
 *   S            aktualny stan
 *   T<epoch>     ustaw zegar, np. T1754870400
 *   F            ustaw tryb LODOWKA
 *   O            ustaw tryb POZA LODOWKA / TRANSPORT
 *   R            zeruj liczniki i zatrzaski po wymianie insuliny
 *   E            wymaz log
 *   I            awaryjnie sformatuj pamiec trwala i uruchom ponownie (KASUJE historie)
 * -------------------------------------------------------------------
 */

#include <Arduino.h>
#include <bluefruit.h>
#include <Wire.h>
#include <OneWire.h>
#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>
#include <Adafruit_FlashTransport.h>
#include <flash/flash_nrf5x.h>   // tylko flash_nrf5x_read, do testu pustego obszaru
#include <type_traits>
#include <stddef.h>

using namespace Adafruit_LittleFS_Namespace;

// =========================== KONFIGURACJA ===========================

// --- czujnik powietrza (TMP117) ---
static const uint8_t TMP117_ADDR = 0x48;   // 0x49 jesli zmostkujesz zwore ADDR
static const uint8_t REG_TEMP    = 0x00;
static const uint8_t REG_CONFIG  = 0x01;

/*
 * OPCJA NA STALA DIODE ON MODULU TMP117.
 *
 * Breakouty w formacie STEMMA QT maja zwykle diode zasilania, ktorej nie da sie
 * wygasic programowo. Pobiera ona 1-2 mA, czyli kilkadziesiat razy wiecej niz
 * cale pozostale urzadzenie, i skraca czas pracy z miesiecy do okolo dwoch
 * tygodni. NAJPIERW ZMIERZ prad amperomierzem w szereg z bateria.
 *
 *   15-35 uA  -> nie ma problemu, zostaw ponizej -1
 *   1-2 mA    -> przelacz zasilanie modulu na pin
 *
 * Przelaczenie: przenies przewod VIN modulu z 3V3 na D1 i wpisz ponizej 1.
 * Modul pracuje wtedy ok. 250 ms raz na dwie minuty, czyli srednio < 3 uA
 * razem z dioda. Nie wymaga lutowania SMD.
 *
 * Zastrzezenie: rezystory podciagajace I2C na module sa zasilane z jego VCC,
 * wiec po wylaczeniu linie SDA/SCL zostaja bez podciagniecia. Dlatego kod
 * wylacza tez peryferium TWI (Wire.end()) na czas przerwy. Po zmianie sprawdz
 * odczyty przez kilka godzin, zanim uznasz sprawe za zamknieta.
 */
static const int8_t  TMP117_POWER_PIN = -1;   // -1 = zasilanie stale z 3V3
static const uint16_t TMP117_SETTLE_MS = 10;  // czas na start ukladu po podaniu Vcc

// --- sonda w kartoniku (DS18B20) ---
static const uint8_t PROBE_PIN     = D2;
static const uint8_t PROBE_RETRIES = 3;

// Komendy magistrali 1-Wire wg noty katalogowej DS18B20
static const uint8_t DS_CONVERT    = 0x44;
static const uint8_t DS_READ_SP    = 0xBE;
static const uint8_t DS_WRITE_SP   = 0x4E;
static const uint8_t DS_CFG_11BIT  = 0x5F;   // 11 bitow: 0,125 C; konwersja max ~375 ms
static const uint8_t DS_TH_DEFAULT = 0x4B;   // progi alarmowe ukladu, nieuzywane,
static const uint8_t DS_TL_DEFAULT = 0x46;   // ale zapis konfiguracji wymaga ich podania

// Konwersja 11-bitowa trwa maks. ok. 375 ms. Dajemy bezpieczny margines.
static const uint32_t PROBE_CONV_MS = 400;

/*
 * DS18B20 ma katalogowo dokladnosc +-0,5 C w zakresie -10..+85 C.
 * Kalibracja jednopunktowa w kapieli lodowo-wodnej moze skorygowac offset
 * w poblizu 0 C, ale NIE gwarantuje okreslonej dokladnosci w calym zakresie.
 *
 * Offsetu nie wpisuj tutaj - ustaw go komenda C z aplikacji; zostanie zapisany
 * w pamieci i przetrwa restart. Ta wartosc to tylko punkt startowy.
 */
static const float PROBE_OFFSET_DEFAULT = 0.0f;
static const char *CFG_PATH = "/cfg.txt";
static const char *STATE_A_PATH = "/stateA.bin";
static const char *STATE_B_PATH = "/stateB.bin";

// --- czasy ---
static const uint32_t SAMPLE_PERIOD_MS  = 60000UL;    // sonda co 60 s
static const uint32_t AIR_PERIOD_MS     = 120000UL;   // TMP117 co 2 min; przy M od razu
static const uint32_t BATT_PERIOD_MS    = 900000UL;   // bateria co 15 min; przy M od razu
static const uint32_t SUMMARY_PERIOD_S  = 86400UL;    // podsumowanie raz na dobe
static const uint32_t LIVE_PERIOD_MS    = 3000UL;     // odswiezanie dla telefonu

// --- progi temperatury [stopnie C] ---
static const float FREEZE_LIMIT  =  0.0f;   // ponizej = ryzyko zamarzniecia; zatrzask bezpieczeństwa
static const float FRIDGE_MIN    =  2.0f;   // dolny prog przechowywania
static const float FRIDGE_MAX    =  8.0f;   // gorny prog przechowywania
static const float OUT_WARN      = 25.0f;   // ostrzezenie w transporcie
static const float OUT_MAX       = 30.0f;   // alarm w transporcie
static const float COOK_LIMIT    = 37.0f;   // twardy prog przegrzania, odpowiednik
                                            // FREEZE_LIMIT po drugiej stronie skali

// Przegrzanie, inaczej niz zamarzniecie, nie ma jednej ostrej granicy - liczy sie
// takze czas ekspozycji. Ponizsza wartosc to NASTAWA, nie fakt kliniczny.
// Ustaw ja wg ulotki swojego preparatu, domyslna jest celowo konserwatywna.
static const uint32_t HEAT_DOSE_LIMIT_S = 60UL * 60UL; // NASTAWA: 60 min powyzej OUT_MAX

// --- kontrola zgodnosci trybu z otoczeniem (TMP117 nie zmienia trybu) ---
static const float MODE_HINT_FRIDGE = 10.0f;
static const float MODE_HINT_OUT    = 12.0f;
static const uint8_t MODE_MISMATCH_CONFIRM_N = 2;

// --- trwały stan: zapis rzadki, zdarzeniowy ---
// 1024 B to ok. 28 rekordow po 36 B. To celowo niewielki prog rotacji
// dziennika; nie jest to rozmiar fizycznego bloku flash. Dwa pliki zapewniaja
// odpornosc na utrate zasilania w trakcie przejscia na nowy dziennik.
static const uint32_t STATE_JOURNAL_MAX_BYTES = 1024;
static const uint32_t STATE_SAVE_HEAT_S = 300;      // co 5 min przy T > OUT_MAX
static const uint32_t STATE_SAVE_ACTIVE_S = 1800;   // co 30 min gdy liczniki rosna
static const uint32_t STATE_SAVE_IDLE_S = 21600;    // max co 6 h dla innych zmian

// --- sygnalizacja LED ---
/*
 * Diody na XIAO sa aktywne stanem niskim, swieca bardzo krotkimi impulsami.
 *
 * Dwie zasady kompromisu:
 *  - heartbeat musi byc na tyle czesty, zeby "brak blysku" naprawde znaczyl
 *    brak nadzoru. Przy okresie 60 s cisza przez pol minuty jest normalna
 *    i zasada traci sens, dlatego wracamy do 20 s. Koszt to ok. 1,5 uA.
 *  - alarm krytyczny wymaga dzialania, wiec przez pierwsza dobe miga czesto.
 *    Potem zwalnia, bo zatrzask moze trwac tygodniami i nie ma sensu zjadac
 *    baterii przypominaniem o czyms, co juz zostalo zauwazone.
 */
static const bool     LED_ENABLED        = true;
static const uint16_t LED_PULSE_MS       = 15;    // dlugosc jednego blysku
static const uint16_t LED_GAP_MS         = 180;   // przerwa miedzy blyskami w serii
static const uint32_t LED_PERIOD_OK_S    = 20;    // heartbeat / bateria
static const uint32_t LED_PERIOD_LIVE_S  = 15;    // aktywne przekroczenie
static const uint32_t LED_PERIOD_LATCH_S = 60;    // historia / niezgodny tryb
static const uint32_t LED_PERIOD_ERROR_S = 15;    // sonda / pamiec

// Alarm krytyczny: szybko, dopoki nie zostanie zauwazony, potem oszczednie.
static const uint32_t LED_PERIOD_CRIT_FAST_S = 3;
static const uint32_t LED_PERIOD_CRIT_SLOW_S = 30;
static const uint32_t CRIT_FAST_WINDOW_S     = 24UL * 3600UL;  // nowy zatrzask
static const uint32_t CRIT_FAST_REMIND_S     = 3600UL;         // po restarcie

// --- okno kalibracyjne ---
/*
 * Kapiel lodowa przy 0 C sama tworzy przekroczenie: w trybie LODOWKA odczyt
 * jest ponizej FRIDGE_MIN, a przy lekko ujemnej wartosci takze ponizej
 * FREEZE_LIMIT. Procedura kontrolna zasmiecalaby wiec rejestr bezpieczenstwa.
 *
 * Okno kalibracyjne wstrzymuje zatrzaski i liczenie ekspozycji. Otwiera je
 * komenda B PRZED zanurzeniem sondy, bo przekroczenie powstaje w trakcie
 * dziesieciu minut wychlodzenia, a nie w chwili nacisniecia K.
 *
 * Ochrona przed zapomnieniem: okno wygasa samo, jest widoczne na diodzie
 * i w aplikacji, a kazde otwarcie i zamkniecie trafia do logu.
 */
static const uint32_t CALIB_WINDOW_S = 1200UL;   // 20 minut

// --- bateria ---
// XIAO nRF52840 mierzy VBAT przez dzielnik 1M / 510k. Zmierz multimetrem
// i skoryguj ten mnoznik, egzemplarze roznia sie o kilka procent.
static const float VBAT_DIVIDER = 2.961f;
static const float VBAT_EMPTY   = 3.30f;
static const float VBAT_FULL    = 4.15f;

#if !defined(PIN_VBAT) || !defined(VBAT_ENABLE)
  #error "Brak PIN_VBAT/VBAT_ENABLE. Wybierz Seeed XIAO nRF52840 w pakiecie Seeed nRF52 Boards i sprawdz variant.h; nie uzywaj zgadywanych numerow pinow baterii."
#endif

// --- log ---
/*
 * Wewnetrzny system plikow Adafruita ma 28 kB, czyli siedem blokow po 4 kB,
 * z czego dwa zajmuja metadane LittleFS. Do dyspozycji zostaje piec blokow,
 * a rotacja logu potrzebuje dodatkowo wolnego miejsca na kopie.
 *
 * Budzet: log 1 blok + dwa dzienniki stanu po 1 bloku + konfiguracja 1 blok.
 * Poprzednie 12 kB logu i 4 kB dziennikow nie mieszcily sie, a objaw wystapilby
 * dopiero po tygodniach: zapelniony log blokowalby zapis stanu bezpieczenstwa.
 */
static const char *LOG_PATH        = "/insulin.csv";
static const uint32_t LOG_MAX_BYTES  = 4000;   // ~65 zdarzen, czyli tygodnie historii
static const uint32_t LOG_KEEP_BYTES = 2000;   // ile zostaje po rotacji
static uint8_t logRotateBuf[LOG_KEEP_BYTES]; // staly bufor: brak ryzyka malloc podczas rotacji

// --- BLE ---
static const char *DEVICE_NAME = "InsuTemp";
static const int8_t TX_POWER   = 8;   // dBm, maks dla nRF52840; potrzebne zeby
                                      // sygnal wyszedl z lodowki

// ============================ STAN GLOBALNY =========================

enum Mode : uint8_t { MODE_UNKNOWN = 0, MODE_FRIDGE = 1, MODE_OUT = 2 };

// Bez zapisanej konfiguracji startujemy konserwatywnie w trybie LODOWKA.
// Awaria lodowki nie moze sama poluzowac progow do 30 C.
static Mode     mode              = MODE_FRIDGE;
static uint8_t  mismatchCount     = 0;
static bool     modeMismatch      = false;

static float    lastTempC         = 0.0f;   // sonda przy opakowaniu - kanal glowny
static float    airTempC          = 0.0f;   // POWIETRZE, TMP117
static bool     airOk             = false;
static float    probeOffset       = PROBE_OFFSET_DEFAULT;
static float    minTempC          =  999.0f;
static float    maxTempC          = -999.0f;
static float    excMinTempC       =  999.0f;   // min/max w trwajacej ekskursji
static float    excMaxTempC       = -999.0f;

static bool     alarmActive       = false;
static bool     inExcursion       = false;
static bool     frozenLatch       = false;
static bool     cookedLatch       = false;
static bool     latchTooCold      = false;
static bool     latchTooWarm      = false;
static uint32_t secondsAboveMax   = 0;         // czas rzeczywisty, nie liczba probek
static float    coldScarC         =  999.0f;   // najnizsza temp. zwiazana z zatrzaskiem
static float    warmScarC         = -999.0f;   // najwyzsza temp. zwiazana z zatrzaskiem
static bool     liveTooCold       = false;     // jest za zimno TERAZ
static bool     liveTooWarm       = false;     // przekroczony twardy gorny prog TERAZ
static bool     liveWarmWarning   = false;     // tylko OUT_WARN..OUT_MAX w trybie POZA LODOWKA
static uint32_t excursionStartS   = 0;

static uint32_t secondsOutOfRange = 0;   // czas poza wybranym oknem
static uint32_t secondsOutOfFridge= 0;   // czas w trybie POZA LODOWKA
static uint32_t lastEvalS         = 0;   // baza do delta-t temperatury (tylko gdy sonda dziala)
static uint32_t lastModeAccountS  = 0;   // niezalezny zegar czasu w trybie POZA LODOWKA
static bool     modeClockStarted  = false;
static bool     stateDirty        = false;
static bool     fsOk              = false;
static bool     statePersistError = false;
static uint32_t lastStateSaveS    = 0;
static uint32_t stateSeq          = 0;
static char     activeStatePath   = 'A';
static bool     stateJournalNeedsRollover = false; // uszkodzony/urwany ogon dziennika
static uint32_t critFastUntilS    = 0;   // do kiedy alarm krytyczny miga czesto
static uint32_t calibWindowUntilS = 0;   // do kiedy zatrzaski sa wstrzymane

// --- ochrona zapisu ---
/*
 * Model zagrozenia: obcy telefon w zasiegu radia, np. na lotnisku. Ma widziec
 * odczyty i historie, ale nie moze niczego zmienic ani skasowac.
 *
 * Uprawnienie do zapisu wynika z DOWODU POSIADANIA urzadzenia, a nie ze znajomosci
 * sekretu. Powiazanie tworzymy wylacznie przy podlaczonym kablu USB. Sekretu nie ma,
 * wiec nie ma czego wyciec ani zapomniec, a kabel jest zawsze droga powrotna:
 * usuniete parowanie w telefonie, nowy telefon czy uszkodzona pamiec powiazan
 * oznaczaja tylko ponowne sparowanie przy kablu. Nie istnieje stan bez wyjscia.
 */
static bool     bondAuthorized    = false;  // trwale: powiazanie powstalo przy USB
static uint16_t activeConn        = BLE_CONN_HANDLE_INVALID;

static float    vbat              = 0.0f;
static uint8_t  battPercent       = 100;

static uint64_t uptimeMs          = 0;
static uint32_t lastMillis        = 0;
static uint32_t epochBase         = 0;   // 0 = zegar nieustawiony
static uint32_t lastSampleMs      = 0;
static uint32_t lastAirMs         = 0;
static uint32_t lastBattMs        = 0;
static uint32_t lastSummaryS      = 0;
static uint8_t  packetId          = 0;
static bool     probeOk          = false;

BLEUart bleuart;
Adafruit_FlashTransport_QSPI flashTransport;

OneWire oneWire(PROBE_PIN);

// ============================== CZAS ================================

static void tickClock() {
  uint32_t now = millis();
  uptimeMs += (uint32_t)(now - lastMillis);   // poprawnie znosi przekrecenie
  lastMillis = now;
}

static uint32_t uptimeSec() { return (uint32_t)(uptimeMs / 1000ULL); }

// Znacznik czasu do logu: epoch jesli zegar ustawiony, inaczej "u<sekundy>"
static void stampNow(char *buf, size_t len) {
  if (epochBase) snprintf(buf, len, "%lu", (unsigned long)(epochBase + uptimeSec()));
  else           snprintf(buf, len, "u%lu", (unsigned long)uptimeSec());
}

// ============================== TMP117 ==============================

static bool tmpWrite16(uint8_t reg, uint16_t val) {
  Wire.beginTransmission(TMP117_ADDR);
  Wire.write(reg);
  Wire.write((uint8_t)(val >> 8));
  Wire.write((uint8_t)(val & 0xFF));
  return Wire.endTransmission() == 0;
}

static bool tmpRead16(uint8_t reg, uint16_t &val) {
  Wire.beginTransmission(TMP117_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)TMP117_ADDR, (uint8_t)2) != 2) return false;
  val  = (uint16_t)Wire.read() << 8;
  val |= (uint16_t)Wire.read();
  return true;
}

/*
 * Jeden pomiar w trybie one-shot.
 * Config = 0x0C20: MOD[11:10]=11 (one-shot), AVG[6:5]=01 (usrednianie z 8 probek).
 * Konwersja trwa ~124 ms, potem uklad sam wraca do shutdown (setki nA).
 * I2C wlaczamy tylko na czas pomiaru - peryferium TWIM pobiera prad gdy aktywne.
 */
static bool readAirSensor(float &out) {
  if (TMP117_POWER_PIN >= 0) {
    pinMode((uint8_t)TMP117_POWER_PIN, OUTPUT);
    digitalWrite((uint8_t)TMP117_POWER_PIN, HIGH);
    delay(TMP117_SETTLE_MS);
  }

  Wire.begin();
  Wire.setClock(400000);

  bool ok = false;
  if (tmpWrite16(REG_CONFIG, 0x0C20)) {
    delay(200);                       // CPU spi przez ten czas
    uint16_t cfg = 0, raw = 0;
    if (tmpRead16(REG_CONFIG, cfg) && (cfg & (1u << 13))) {   // DATA_READY
      if (tmpRead16(REG_TEMP, raw)) {
        out = (int16_t)raw * 0.0078125f;
        ok = true;
      }
    }
  }

  Wire.end();

  if (TMP117_POWER_PIN >= 0) {
    // Kolejnosc ma znaczenie: najpierw zwolnij magistrale, potem odetnij Vcc,
    // zeby nie zasilac modulu przez rezystory podciagajace.
    digitalWrite((uint8_t)TMP117_POWER_PIN, LOW);
  }

  return ok;
}

// ============================== DS18B20 =============================

/*
 * Sonda obslugiwana bezposrednio, bez biblioteki DallasTemperature.
 *
 * Powod jest konkretny: DallasTemperature adresuje uklad przez przeszukanie
 * magistrali (search ROM), czyli 64 operacje bitowe o krytycznym czasie, przy
 * kazdym odczycie. Kazda z nich blokuje przerwania, a obok pracuje stos BLE.
 * Przy jednym ukladzie na magistrali mozna uzyc adresowania SKIP ROM, ktore
 * wymaga jednego bajtu zamiast szescdziesieciu czterech. Mniej okazji do
 * kolizji, mniej pradu, o jedna biblioteke mniej do zainstalowania.
 *
 * Kazdy odczyt jest weryfikowany suma kontrolna CRC8 z noty katalogowej,
 * wiec przekrecony bit zostanie wylapany, a nie zapisany do logu jako pomiar.
 */

// Ustawia 11 bitow rozdzielczosci (0,125 C; krotsza konwersja). WRITE SCRATCHPAD
// nie wykonuje COPY SCRATCHPAD do EEPROM, wiec po restarcie/zasileniu samej sondy
// uklad moze wrocic do 12 bitow. Dlatego konfigurujemy go PRZED kazda konwersja.
// Koszt to kilka bajtow 1-Wire raz na minute, a odzyskanie po zaniku zasilania jest pewne.
static bool probeConfigure() {
  if (!oneWire.reset()) return false;
  oneWire.skip();
  oneWire.write(DS_WRITE_SP);
  oneWire.write(DS_TH_DEFAULT);
  oneWire.write(DS_TL_DEFAULT);
  oneWire.write(DS_CFG_11BIT);
  return true;
}

static bool probeStartConversion() {
  if (!oneWire.reset()) return false;   // brak impulsu obecnosci = brak sondy
  oneWire.skip();
  oneWire.write(DS_CONVERT);
  return true;
}

static bool probeReadOnce(float &out) {
  if (!oneWire.reset()) return false;
  oneWire.skip();
  oneWire.write(DS_READ_SP);

  uint8_t sp[9];
  for (uint8_t i = 0; i < 9; i++) sp[i] = oneWire.read();

  if (OneWire::crc8(sp, 8) != sp[8]) return false;      // przekrecony odczyt

  int16_t raw = ((int16_t)sp[1] << 8) | sp[0];

  if (raw == 0x0550) return false;   // 85,00 C = wartosc po wlaczeniu zasilania,
                                     // czyli konwersja jeszcze sie nie odbyla

  // Bajt 4 to konfiguracja. Przy nizszej rozdzielczosci najmlodsze bity
  // wyniku sa nieokreslone - zerujemy je, zeby nie udawac dokladnosci.
  switch ((sp[4] >> 5) & 0x03) {
    case 0: raw &= ~0x07; break;     //  9 bitow
    case 1: raw &= ~0x03; break;     // 10 bitow
    case 2: raw &= ~0x01; break;     // 11 bitow
    default: break;                  // 12 bitow, pelna rozdzielczosc
  }

  float t = raw * 0.0625f;
  if (t < -55.0f || t > 125.0f) return false;            // poza zakresem ukladu

  out = t + probeOffset;
  return true;
}

static bool probeRead(float &out) {
  for (uint8_t i = 0; i < PROBE_RETRIES; i++) {
    if (probeReadOnce(out)) return true;
    delay(50);
  }
  return false;
}

// ============================= BATERIA ==============================

static void readBattery() {
  // Seeed ostrzega, aby podczas ladowania nie ustawic READ_BAT_ENABLE/P0.14 HIGH.
  // Zostawiamy wiec dzielnik wlaczony (LOW). Koszt to kilka uA, ale unikamy
  // ryzyka przekroczenia napiecia na wejsciu ADC przy podlaczeniu USB.
  pinMode(VBAT_ENABLE, OUTPUT);
  digitalWrite(VBAT_ENABLE, LOW);
  analogReference(AR_INTERNAL_3_0);
  analogReadResolution(12);
  delay(3);

  uint32_t acc = 0;
  for (uint8_t i = 0; i < 8; i++) acc += analogRead(PIN_VBAT);

  vbat = (acc / 8.0f) * (3.0f / 4096.0f) * VBAT_DIVIDER;
  float p = (vbat - VBAT_EMPTY) / (VBAT_FULL - VBAT_EMPTY) * 100.0f;
  battPercent = (uint8_t)constrain(p, 0.0f, 100.0f);
}

// =============================== LOG ================================

static uint32_t logSize() {
  if (!fsOk) return 0;
  File f(InternalFS);
  if (!f.open(LOG_PATH, FILE_O_READ)) return 0;
  uint32_t s = f.size();
  f.close();
  return s;
}

/*
 * Log jest niezalezny od dziennika stanu bezpieczenstwa. Przy przekroczeniu
 * limitu zachowujemy najnowsza czesc i odcinamy stara, aby nie zapelnic FS.
 */
static void rotateLogIfNeeded() {
  if (!fsOk) return;
  uint32_t sz = logSize();
  if (sz < LOG_MAX_BYTES) return;

  uint8_t *buf = logRotateBuf;
  int n = 0;
  File f(InternalFS);
  if (f.open(LOG_PATH, FILE_O_READ)) {
    f.seek(sz - LOG_KEEP_BYTES);
    n = f.read(buf, LOG_KEEP_BYTES);
    f.close();
  }
  // FILE_O_WRITE w tej implementacji LittleFS dopisuje do istniejacego pliku.
  // Jezeli usuniecie starego logu sie nie uda, nie wolno "rotowac" przez append,
  // bo plik tylko by rosl i mogl wypelnic system plikow.
  if (InternalFS.exists(LOG_PATH)) {
    if (!InternalFS.remove(LOG_PATH) || InternalFS.exists(LOG_PATH)) return;
  }

  if (n > 0) {
    int start = 0;                       // pomin urwana pierwsza linie
    while (start < n && buf[start] != '\n') start++;
    if (start < n) start++;

    File g(InternalFS);
    if (g.open(LOG_PATH, FILE_O_WRITE)) {
      g.write((const uint8_t *)"#,ROTACJA,0,starsze wpisy odciete\n", 34);
      g.write(buf + start, n - start);
      g.close();
    }
  }
}

static void appendLog(const char *line) {
  if (!fsOk) return;
  rotateLogIfNeeded();
  File f(InternalFS);
  if (!f.open(LOG_PATH, FILE_O_WRITE)) return;   // FILE_O_WRITE = dopisywanie
  f.write((const uint8_t *)line, strlen(line));
  f.close();
}

static void sanitizeCsvField(const char *src, char *dst, size_t n) {
  if (!n) return;
  size_t j = 0;
  for (size_t i = 0; src && src[i] && j + 1 < n; i++) {
    char c = src[i];
    if (c == ',' || c == '\n' || c == '\r' || c == '"') c = ';';
    dst[j++] = c;
  }
  dst[j] = 0;
}

static void logEvent(const char *type, const char *detail) {
  char ts[24], safe[112], line[180];
  stampNow(ts, sizeof(ts));
  sanitizeCsvField(detail, safe, sizeof(safe));
  snprintf(line, sizeof(line), "%s,%s,%.3f,%s\n", ts, type, lastTempC, safe);
  appendLog(line);
}

static void loadLegacyConfig() {
  // Migracja z wersji <= v3: dawniej offset byl w /cfg.txt. Nowe rekordy stanu
  // przechowuja go razem z CRC i dziennikiem, wiec tego pliku juz nie nadpisujemy.
  if (!fsOk) return;
  File f(InternalFS);
  if (!f.open(CFG_PATH, FILE_O_READ)) return;
  char b[32] = {0};
  int n = f.read(b, sizeof(b) - 1);
  f.close();
  if (n > 0) {
    float v = atof(b);
    if (v > -10.0f && v < 10.0f) probeOffset = v;
  }
}

// ======================== TRWALY STAN BEZPIECZENSTWA ========================

struct __attribute__((packed)) StateRecord {
  uint32_t magic;
  uint16_t version;
  uint16_t size;
  uint32_t seq;
  uint8_t mode;
  uint8_t flags;
  uint16_t reserved;
  uint32_t secondsAboveMax;
  uint32_t secondsOutOfRange;
  uint32_t secondsOutOfFridge;
  int16_t coldScarCenti;
  int16_t warmScarCenti;
  uint32_t crc;
};
static_assert(sizeof(StateRecord) == 36, "Nieoczekiwany rozmiar StateRecord");

static const uint32_t STATE_MAGIC = 0x49535432UL; // "IST2"
static const uint16_t STATE_VERSION = 2;
static const uint8_t STATE_FLAG_FROZEN       = 0x01;
static const uint8_t STATE_FLAG_COOKED       = 0x02;
static const uint8_t STATE_FLAG_COLD_LATCH   = 0x04;
static const uint8_t STATE_FLAG_WARM_LATCH   = 0x08;
static const uint8_t STATE_FLAG_OFFSET_VALID = 0x10; // reserved = offset w 0,001 C
static const uint8_t STATE_FLAG_BOND_OK      = 0x20; // powiazanie utworzone przy USB

static uint32_t crc32Bytes(const uint8_t *data, size_t len) {
  uint32_t crc = 0xFFFFFFFFUL;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (uint8_t b = 0; b < 8; b++)
      crc = (crc >> 1) ^ (0xEDB88320UL & (uint32_t)-(int32_t)(crc & 1));
  }
  return ~crc;
}

static bool validStateRecord(const StateRecord &r) {
  if (r.magic != STATE_MAGIC || r.version != STATE_VERSION || r.size != sizeof(StateRecord)) return false;
  if (r.mode != MODE_FRIDGE && r.mode != MODE_OUT) return false;
  uint32_t got = crc32Bytes((const uint8_t *)&r, offsetof(StateRecord, crc));
  return got == r.crc;
}

static bool scanStateFile(const char *path, StateRecord &best) {
  if (!fsOk) return false;
  File f(InternalFS);
  if (!f.open(path, FILE_O_READ)) return false;
  bool found = false;
  bool invalidRecord = false;
  uint32_t bytesRead = 0;
  StateRecord r;
  int n;
  while ((n = f.read((uint8_t *)&r, sizeof(r))) > 0) {
    bytesRead += (uint32_t)n;
    if (n != (int)sizeof(r)) { invalidRecord = true; break; }
    if (validStateRecord(r)) {
      if (!found || r.seq > best.seq) { best = r; found = true; }
    } else {
      invalidRecord = true;
    }
  }
  uint32_t total = f.size();
  f.close();
  if (invalidRecord || bytesRead != total) stateJournalNeedsRollover = true;
  return found;
}

static uint32_t stateFileSize(const char *path) {
  if (!fsOk) return 0;
  File f(InternalFS);
  if (!f.open(path, FILE_O_READ)) return 0;
  uint32_t n = f.size();
  f.close();
  return n;
}

static void applyState(const StateRecord &r) {
  mode = (Mode)r.mode;
  frozenLatch  = r.flags & STATE_FLAG_FROZEN;
  cookedLatch  = r.flags & STATE_FLAG_COOKED;
  bondAuthorized = r.flags & STATE_FLAG_BOND_OK;
  latchTooCold = r.flags & STATE_FLAG_COLD_LATCH;
  latchTooWarm = r.flags & STATE_FLAG_WARM_LATCH;
  // W rekordach ze starszej wersji bit nie byl ustawiony; wtedy zachowujemy
  // offset wczytany z dawnego /cfg.txt. Rozmiar rekordu i wersja pozostaja zgodne.
  if (r.flags & STATE_FLAG_OFFSET_VALID) probeOffset = (int16_t)r.reserved / 1000.0f;
  secondsAboveMax    = r.secondsAboveMax;
  secondsOutOfRange  = r.secondsOutOfRange;
  secondsOutOfFridge = r.secondsOutOfFridge;
  coldScarC = r.coldScarCenti == 32767 ? 999.0f : r.coldScarCenti / 100.0f;
  warmScarC = r.warmScarCenti == -32768 ? -999.0f : r.warmScarCenti / 100.0f;
  stateSeq = r.seq;
}

static void loadPersistentState() {
  if (!fsOk) return;
  StateRecord a = {}, b = {};
  bool okA = scanStateFile(STATE_A_PATH, a);
  bool okB = scanStateFile(STATE_B_PATH, b);
  if (!okA && !okB) {
    // Brak plikow = pierwszy start. Niepuste, ale bez ani jednego poprawnego
    // rekordu = utrata wiarygodnej historii i musi byc widocznym bledem.
    if (stateFileSize(STATE_A_PATH) || stateFileSize(STATE_B_PATH) || stateJournalNeedsRollover)
      statePersistError = true;
    return;
  }

  if (okA && (!okB || a.seq >= b.seq)) { applyState(a); activeStatePath = 'A'; }
  else                                  { applyState(b); activeStatePath = 'B'; }
}

static StateRecord makeStateRecord() {
  StateRecord r = {};
  r.magic = STATE_MAGIC;
  r.version = STATE_VERSION;
  r.size = sizeof(StateRecord);
  r.seq = ++stateSeq;
  r.mode = (uint8_t)mode;
  r.flags = (frozenLatch ? STATE_FLAG_FROZEN : 0) |
            (cookedLatch ? STATE_FLAG_COOKED : 0) |
            (latchTooCold ? STATE_FLAG_COLD_LATCH : 0) |
            (latchTooWarm ? STATE_FLAG_WARM_LATCH : 0) |
            (bondAuthorized ? STATE_FLAG_BOND_OK : 0) |
            STATE_FLAG_OFFSET_VALID;
  r.reserved = (uint16_t)(int16_t)lroundf(constrain(probeOffset, -10.0f, 10.0f) * 1000.0f);
  r.secondsAboveMax = secondsAboveMax;
  r.secondsOutOfRange = secondsOutOfRange;
  r.secondsOutOfFridge = secondsOutOfFridge;
  r.coldScarCenti = coldScarC > 900.0f ? 32767 : (int16_t)lroundf(constrain(coldScarC, -327.0f, 327.0f) * 100.0f);
  r.warmScarCenti = warmScarC < -900.0f ? -32768 : (int16_t)lroundf(constrain(warmScarC, -327.0f, 327.0f) * 100.0f);
  r.crc = crc32Bytes((const uint8_t *)&r, offsetof(StateRecord, crc));
  return r;
}

static bool appendStateRecord(bool forceRollover = false) {
  if (!fsOk) return false;
  const char *cur = activeStatePath == 'A' ? STATE_A_PATH : STATE_B_PATH;
  const char *other = activeStatePath == 'A' ? STATE_B_PATH : STATE_A_PATH;
  uint32_t sz = stateFileSize(cur);
  StateRecord r = makeStateRecord();

  if (forceRollover || stateJournalNeedsRollover || sz + sizeof(r) > STATE_JOURNAL_MAX_BYTES) {
    // Najpierw zapisujemy swiezy rekord do drugiego pliku. Stary pozostaje,
    // wiec utrata zasilania w trakcie rotacji nie kasuje ostatniego stanu.
    // FILE_O_WRITE dopisuje do istniejacego pliku, dlatego usuniecie celu
    // rotacji musi byc potwierdzone. W przeciwnym razie swiezy rekord moglby
    // trafic za uszkodzony ogon poprzedniego dziennika.
    if (InternalFS.exists(other)) {
      if (!InternalFS.remove(other) || InternalFS.exists(other)) {
        stateSeq--;
        stateJournalNeedsRollover = true;
        return false;
      }
    }
    File g(InternalFS);
    if (!g.open(other, FILE_O_WRITE)) { stateSeq--; return false; }
    int n = g.write((const uint8_t *)&r, sizeof(r));
    g.close();
    if (n != (int)sizeof(r)) { stateSeq--; stateJournalNeedsRollover = true; return false; }
    activeStatePath = activeStatePath == 'A' ? 'B' : 'A';
    stateJournalNeedsRollover = false;
  } else {
    File f(InternalFS);
    if (!f.open(cur, FILE_O_WRITE)) { stateSeq--; return false; }
    int n = f.write((const uint8_t *)&r, sizeof(r));
    f.close();
    if (n != (int)sizeof(r)) { stateSeq--; stateJournalNeedsRollover = true; return false; }
  }

  stateDirty = false;
  lastStateSaveS = uptimeSec();
  return true;
}

static void markStateDirty() { stateDirty = true; }

static void persistStateNow() {
  markStateDirty();
  if (!appendStateRecord(false)) statePersistError = true;
}

static void persistStateIfDue(bool heatActive, bool counterActive) {
  if (!stateDirty) return;
  uint32_t age = uptimeSec() - lastStateSaveS;
  uint32_t due = heatActive ? STATE_SAVE_HEAT_S
                : counterActive ? STATE_SAVE_ACTIVE_S : STATE_SAVE_IDLE_S;
  if (age >= due && !appendStateRecord(false)) statePersistError = true;
}

// ============================ ROZGLASZANIE ==========================

/*
 * BTHome v2. Home Assistant wykryje urzadzenie automatycznie, bez integracji
 * i bez YAML-a. Potrzebny tylko zasieg BLE: adapter w hoscie albo proxy ESPHome.
 *
 * Obiekty w rosnacej kolejnosci ID: 0x00 packet id, 0x01 bateria %,
 *          0x02 temperatura (int16, x0,01), 0x0F alarm, 0x26 problem,
 *          0x3D licznik minut poza zakresem.
 */
static void updateAdvertising() {
  uint8_t sd[28];
  uint8_t i = 0;

  sd[i++] = 0xD2; sd[i++] = 0xFC;        // UUID16 0xFCD2, little-endian
  sd[i++] = 0x40;                        // BTHome v2, bez szyfrowania

  // BTHome wymaga rosnacej kolejnosci ID obiektow.
  sd[i++] = 0x00; sd[i++] = packetId++;
  sd[i++] = 0x01; sd[i++] = battPercent;

  if (probeOk) {
    int16_t t = (int16_t)lroundf(constrain(lastTempC, -320.0f, 320.0f) * 100.0f);
    sd[i++] = 0x02; sd[i++] = (uint8_t)(t & 0xFF); sd[i++] = (uint8_t)((t >> 8) & 0xFF);
  }

  sd[i++] = 0x0F;
  sd[i++] = (alarmActive || frozenLatch || cookedLatch || latchTooCold || latchTooWarm) ? 1 : 0;

  sd[i++] = 0x26; // problem: sonda / tryb / pamiec trwala
  sd[i++] = (!probeOk || modeMismatch || statePersistError) ? 1 : 0;

  uint32_t outMin32 = secondsOutOfRange / 60UL;
  uint16_t m = outMin32 > 65535UL ? 65535 : (uint16_t)outMin32;
  sd[i++] = 0x3D; sd[i++] = (uint8_t)(m & 0xFF); sd[i++] = (uint8_t)((m >> 8) & 0xFF);

  Bluefruit.Advertising.stop();
  Bluefruit.Advertising.clearData();
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAG_LE_GENERAL_DISC_MODE |
                                 BLE_GAP_ADV_FLAG_BR_EDR_NOT_SUPPORTED);
  Bluefruit.Advertising.addData(BLE_GAP_AD_TYPE_SERVICE_DATA, sd, i);
  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(6400, 6400);   // 4 s
  Bluefruit.Advertising.setFastTimeout(0);
  Bluefruit.Advertising.start(0);
}

// ============================ SYGNALIZACJA ==========================

/*
 * Kodowanie ortogonalne:
 *   KOLOR  = kierunek bledu   NIEBIESKI = za zimno, CZERWONY = za cieplo
 *   LICZBA = ciezar gatunkowy 1 = teraz, 2 = bylo, 3 = nieodwracalne
 *
 * 3 x niebieski  wykryto ryzyko zamarzniecia - sprawdz zalecenia producenta
 * 3 x czerwony   krytyczne przekroczenie ciepla - sprawdz zalecenia producenta
 * 3 x magenta    awaria czujnika, dane niewiarygodne
 * 2 x niebieski  bylo za zimno (zatrzask, ale nie zamarzla)
 * 2 x czerwony   bylo za cieplo (zatrzask)
 * 1 x niebieski  jest za zimno w tej chwili
 * 1 x czerwony   jest za cieplo w tej chwili
 * 1 x zolty      w normie, bateria ponizej 15 %
 * 1 x zielony    w normie
 *
 * Liczba blyskow niesie pelna informacje o ciezarze bez rozrozniania barw -
 * istotne, bo ok. 8 % mezczyzn ma zaburzenia rozpoznawania kolorow.
 */

static void pulse(uint8_t pinA, uint8_t pinB, uint8_t count, uint16_t gapMs) {
  for (uint8_t i = 0; i < count; i++) {
    digitalWrite(pinA, LOW);                       // aktywne stanem niskim
    if (pinB != 0xFF) digitalWrite(pinB, LOW);
    delay(LED_PULSE_MS);
    digitalWrite(pinA, HIGH);
    if (pinB != 0xFF) digitalWrite(pinB, HIGH);
    if (i + 1 < count) delay(gapMs);
  }
}

static void ledService() {
  if (!LED_ENABLED) return;

  static uint32_t lastBlinkS = 0;

  uint8_t  pinA = LED_GREEN, pinB = 0xFF, count = 1;
  uint32_t periodS = LED_PERIOD_OK_S;
  uint16_t gap = LED_GAP_MS;

  // Alarm krytyczny miga czesto, dopoki okno przypomnienia nie wygasnie.
  const uint32_t critPeriod = (uptimeSec() < critFastUntilS)
                              ? LED_PERIOD_CRIT_FAST_S : LED_PERIOD_CRIT_SLOW_S;

  if (frozenLatch) {
    // Nieodwracalne, kierunek zimny
    pinA = LED_BLUE; count = 3; periodS = critPeriod; gap = 80;
  } else if (cookedLatch) {
    // Nieodwracalne, kierunek cieply
    pinA = LED_RED;  count = 3; periodS = critPeriod; gap = 80;
  } else if (!probeOk || statePersistError) {
    // Magenta: brak wiarygodnego kanalu glownego albo blad pamieci trwalej.
    pinA = LED_RED; pinB = LED_BLUE; count = 3; periodS = LED_PERIOD_ERROR_S;
  } else if (liveTooCold) {
    // Aktualne przekroczenie ma pierwszenstwo przed pamiecia tego samego zdarzenia.
    pinA = LED_BLUE; count = 1; periodS = LED_PERIOD_LIVE_S;
  } else if (liveTooWarm || liveWarmWarning) {
    pinA = LED_RED;  count = 1; periodS = LED_PERIOD_LIVE_S;
  } else if (latchTooCold) {
    pinA = LED_BLUE; count = 2; periodS = LED_PERIOD_LATCH_S;
  } else if (latchTooWarm) {
    pinA = LED_RED;  count = 2; periodS = LED_PERIOD_LATCH_S;
  } else if (modeMismatch) {
    // Zolty podwojny: sprawdz, czy wybrany tryb odpowiada sytuacji.
    pinA = LED_RED; pinB = LED_GREEN; count = 2; periodS = LED_PERIOD_LATCH_S;
  } else if (battPercent < 15) {
    // Zolty = wszystko w normie, ale konczy sie bateria
    pinA = LED_RED; pinB = LED_GREEN; count = 1; periodS = LED_PERIOD_OK_S;
  }

  uint32_t nowS = uptimeSec();
  if (nowS - lastBlinkS < periodS) return;
  lastBlinkS = nowS;

  pulse(pinA, pinB, count, gap);
}

// =========================== LOGIKA ALARMU ==========================

// Czas w trybie POZA LODOWKA jest cecha trybu, nie czujnika. Musi rosnac nawet
// wtedy, gdy DS18B20 chwilowo nie odpowiada. Rozdzielamy go od delta-t temperatury.
static void evaluateAlarms(float t, uint32_t deltaS);

static bool calibWindowActive() {
  return calibWindowUntilS && uptimeSec() < calibWindowUntilS;
}

static void accountModeTime(uint32_t nowS) {
  if (!modeClockStarted) {
    lastModeAccountS = nowS;
    modeClockStarted = true;
    return;
  }
  uint32_t deltaS = nowS - lastModeAccountS;
  lastModeAccountS = nowS;
  if (mode == MODE_OUT && deltaS) {
    secondsOutOfFridge = (UINT32_MAX - secondsOutOfFridge < deltaS)
                         ? UINT32_MAX : secondsOutOfFridge + deltaS;
    markStateDirty();
  }
}

static void setMode(Mode newMode, const char *source) {
  if (newMode != MODE_FRIDGE && newMode != MODE_OUT) return;
  if (mode == newMode) return;
  uint32_t nowS = uptimeSec();
  accountModeTime(nowS);             // dopisz czas starego trybu do chwili zmiany
  mode = newMode;
  mismatchCount = 0;
  modeMismatch = false;
  lastEvalS = nowS;       // nie przypisuj czasu sprzed zmiany do nowego trybu
  char d[64];
  snprintf(d, sizeof(d), "%s;%s", newMode == MODE_FRIDGE ? "lodowka" : "poza_lodowka", source);
  logEvent("MODE_SET", d);
  persistStateNow();

  // Biezacy odczyt ma od razu odpowiadac nowym progom. deltaS=0 oznacza,
  // ze zmiana trybu nie dopisuje ani sekundy ekspozycji.
  if (probeOk) evaluateAlarms(lastTempC, 0);
  else alarmActive = true;
}

static void updateModeMismatch(float air) {
  Mode suggested = MODE_UNKNOWN;
  if (air <= MODE_HINT_FRIDGE) suggested = MODE_FRIDGE;
  else if (air >= MODE_HINT_OUT) suggested = MODE_OUT;

  bool mismatchNow = suggested != MODE_UNKNOWN && suggested != mode;
  bool old = modeMismatch;
  if (mismatchNow) {
    if (mismatchCount < 255) mismatchCount++;
    if (mismatchCount >= MODE_MISMATCH_CONFIRM_N) modeMismatch = true;
  } else {
    mismatchCount = 0;
    modeMismatch = false;
  }

  if (modeMismatch != old) {
    logEvent(modeMismatch ? "MODE_MISMATCH" : "MODE_MATCH",
             modeMismatch ? "otoczenie nie pasuje do wybranego trybu" : "otoczenie znow pasuje");
  }
}

static void evaluateAlarms(float t, uint32_t deltaS) {
  bool criticalChanged = false;
  bool latchChanged = false;

  /*
   * Okno kalibracyjne: nadal mierzymy i pokazujemy stan biezacy, ale nie
   * zapisujemy niczego do pamieci przekroczen. Inaczej sama kontrola jakosci
   * sondy podwazalaby wiarygodnosc rejestru, ktory ma chronic insuline.
   */
  if (calibWindowActive()) {
    float loC = mode == MODE_OUT ? -100.0f : FRIDGE_MIN;
    float hiC = mode == MODE_OUT ? OUT_MAX : FRIDGE_MAX;
    liveTooCold = t < loC;
    liveTooWarm = t > hiC;
    liveWarmWarning = (mode == MODE_OUT && t > OUT_WARN && t <= OUT_MAX);
    alarmActive = frozenLatch || cookedLatch || latchTooCold || latchTooWarm;
    return;
  }

  // T < 0 C oznacza wykrycie ryzyka zamarzniecia przy tej sondzie;
  // nie jest bezposrednim dowodem krystalizacji cieczy.
  if (t < FREEZE_LIMIT && !frozenLatch) {
    frozenLatch = true;
    coldScarC = t;
    criticalChanged = true;
    critFastUntilS = uptimeSec() + CRIT_FAST_WINDOW_S;
    logEvent("FREEZE_RISK", "temperatura sondy ponizej ustawionego progu 0 C");
  }

  if (t > OUT_MAX && deltaS) {
    uint32_t old = secondsAboveMax;
    secondsAboveMax = (UINT32_MAX - secondsAboveMax < deltaS) ? UINT32_MAX : secondsAboveMax + deltaS;
    if (secondsAboveMax != old) markStateDirty();
  }

  if (!cookedLatch) {
    if (t > COOK_LIMIT) {
      cookedLatch = true;
      warmScarC = t;
      criticalChanged = true;
      critFastUntilS = uptimeSec() + CRIT_FAST_WINDOW_S;
      logEvent("HEAT_CRITICAL", "przekroczono ustawiony prog wysokiej temperatury");
    } else if (secondsAboveMax >= HEAT_DOSE_LIMIT_S) {
      cookedLatch = true;
      if (warmScarC < t) warmScarC = t;
      criticalChanged = true;
      critFastUntilS = uptimeSec() + CRIT_FAST_WINDOW_S;
      char d[64];
      snprintf(d, sizeof(d), "czas=%lus powyzej %.1fC",
               (unsigned long)secondsAboveMax, OUT_MAX);
      logEvent("HEAT_DOSE", d);
    }
  }

  float lo = mode == MODE_OUT ? -100.0f : FRIDGE_MIN;
  float hi = mode == MODE_OUT ? OUT_MAX : FRIDGE_MAX;
  liveTooCold = t < lo;
  liveTooWarm = t > hi;
  liveWarmWarning = (mode == MODE_OUT && t > OUT_WARN && t <= OUT_MAX);

  if (liveTooCold) {
    if (!latchTooCold) latchChanged = true;
    latchTooCold = true;
    if (coldScarC > t) coldScarC = t;
  }
  if (liveTooWarm) {
    if (!latchTooWarm) latchChanged = true;
    latchTooWarm = true;
    if (warmScarC < t) warmScarC = t;
  }

  bool bad = liveTooCold || liveTooWarm;
  if (bad && deltaS) {
    secondsOutOfRange = (UINT32_MAX - secondsOutOfRange < deltaS) ? UINT32_MAX : secondsOutOfRange + deltaS;
    markStateDirty();
  }

  if (bad) {
    if (excMinTempC > t) excMinTempC = t;
    if (excMaxTempC < t) excMaxTempC = t;
    if (!inExcursion) {
      inExcursion = true;
      excursionStartS = uptimeSec();
      excMinTempC = excMaxTempC = t;
      logEvent("EXC_START", mode == MODE_OUT ? "poza_lodowka" : "lodowka");
    }
  } else if (inExcursion) {
    inExcursion = false;
    char detail[96];
    snprintf(detail, sizeof(detail), "czas=%lus;min=%.2f;max=%.2f;powietrze=%.2f",
             (unsigned long)(uptimeSec() - excursionStartS),
             excMinTempC, excMaxTempC, airTempC);
    logEvent("EXC_END", detail);
  }

  if (latchChanged) markStateDirty();
  if (criticalChanged || latchChanged) persistStateNow();

  alarmActive = inExcursion || cookedLatch || frozenLatch ||
                (mode == MODE_OUT && t > OUT_WARN) || modeMismatch;

}

// ============================== POMIAR ==============================

static void doSample(bool forceAux = false) {
  lastSampleMs = millis();

  uint32_t nowS = uptimeSec();
  accountModeTime(nowS);

  // Konfiguruj PRZED konwersja. Gdy sama sonda straci zasilanie, wraca do
  // 12 bitow (do 750 ms); bez tego 400 ms oczekiwania mogloby uniemozliwic odzyskanie.
  bool configured = probeConfigure();
  uint32_t convStart = millis();
  bool started = configured && probeStartConversion();

  // Kanaly pomocnicze probkujemy rzadziej, bo nie napedzaja krytycznych alarmow.
  bool sampleAir = forceAux || (uint32_t)(millis() - lastAirMs) >= AIR_PERIOD_MS || lastAirMs == 0;
  if (sampleAir) {
    float a;
    bool aOk = readAirSensor(a);
    if (aOk) airTempC = a;
    if (aOk != airOk) {
      logEvent(aOk ? "AIR_OK" : "AIR_FAIL", aOk ? "TMP117 odpowiada" : "brak odpowiedzi I2C");
    }
    airOk = aOk;
    lastAirMs = millis();
    if (airOk) {
      updateModeMismatch(airTempC);
    } else {
      // Nie trzymaj ostrzezenia o trybie na podstawie starego odczytu powietrza.
      mismatchCount = 0;
      modeMismatch = false;
    }
  }

  if (started) {
    while ((uint32_t)(millis() - convStart) < PROBE_CONV_MS) delay(10);
  }

  float t;
  bool ok = started && probeRead(t);
  bool oldProbeOk = probeOk;
  probeOk = ok;
  if (probeOk) lastTempC = t;
  if (ok != oldProbeOk) {
    logEvent(ok ? "PROBE_OK" : "PROBE_FAIL",
             ok ? "sonda odpowiada" : "brak odpowiedzi 1-Wire; temperatura w kolumnie jest ostatnim poprawnym odczytem");
  }

  if (probeOk) {
    if (t < minTempC) minTempC = t;
    if (t > maxTempC) maxTempC = t;

    uint32_t deltaS = lastEvalS ? nowS - lastEvalS : 0;
    // Po bardzo dlugiej luce (np. debug) nie dopisuj nieznanego czasu do jednej probki.
    if (deltaS > 3 * (SAMPLE_PERIOD_MS / 1000UL)) deltaS = SAMPLE_PERIOD_MS / 1000UL;
    lastEvalS = nowS;
    evaluateAlarms(t, deltaS);
  } else {
    // Czas bez sondy jest nieznany. Aktualizujemy baze, aby po odzyskaniu sondy
    // nie przypisac calej przerwy do pierwszej temperatury.
    lastEvalS = nowS;
    liveTooCold = liveTooWarm = liveWarmWarning = false;
    alarmActive = true;
  }

  bool sampleBatt = forceAux || (uint32_t)(millis() - lastBattMs) >= BATT_PERIOD_MS || lastBattMs == 0;
  if (sampleBatt) {
    readBattery();
    lastBattMs = millis();
    static bool warned = false;
    if (battPercent < 15 && !warned) { warned = true; logEvent("BATT_LOW", ""); }
    if (battPercent >= 20) warned = false;
  }

  if (uptimeSec() - lastSummaryS >= SUMMARY_PERIOD_S) {
    lastSummaryS = uptimeSec();
    char d[160];
    if (minTempC > 900.0f || maxTempC < -900.0f) {
      snprintf(d, sizeof(d),
               "min=NA;max=NA;powietrze=%.2f;pozaOknem=%lumin;pozaLodowka=%lumin;bat=%.2fV",
               airTempC, (unsigned long)(secondsOutOfRange / 60UL),
               (unsigned long)(secondsOutOfFridge / 60UL), vbat);
    } else {
      snprintf(d, sizeof(d),
               "min=%.2f;max=%.2f;powietrze=%.2f;pozaOknem=%lumin;pozaLodowka=%lumin;bat=%.2fV",
               minTempC, maxTempC, airTempC,
               (unsigned long)(secondsOutOfRange / 60UL),
               (unsigned long)(secondsOutOfFridge / 60UL), vbat);
    }
    logEvent("DAILY", d);
    minTempC = 999.0f;
    maxTempC = -999.0f;
  }

  // Checkpoint licznikow jest niezalezny od sprawnosci sondy glownej.
  persistStateIfDue(probeOk && lastTempC > OUT_MAX,
                    mode == MODE_OUT || (probeOk && (liveTooCold || liveTooWarm)));

  updateAdvertising();
}

// ============================== KOMENDY =============================

static void sendLine(const char *s) { bleuart.write((const uint8_t *)s, strlen(s)); }

/*
 * Linia maszynowa dla aplikacji na telefonie. Wysylana co LIVE_PERIOD_MS
 * w trakcie polaczenia oraz na zadanie (komenda P). Kolejnosc pol jest
 * czescia kontraktu z aplikacja - dopisuj nowe wylacznie na koncu.
 * Pola 28..33 niosa aktualne progi, a pole 34 sygnalizuje blad pamieci trwalej.
 */
static void sendLive() {
  char b[520];
  float mn = (minTempC > 900.0f) ? lastTempC : minTempC;
  float mx = (maxTempC < -900.0f) ? lastTempC : maxTempC;
  uint32_t minOutRange = secondsOutOfRange / 60UL;
  uint32_t minHeat = secondsAboveMax / 60UL;
  uint32_t minHeatLimit = HEAT_DOSE_LIMIT_S / 60UL;
  uint32_t minOutside = secondsOutOfFridge / 60UL;
  snprintf(b, sizeof(b),
    "#L,%.3f,%u,%u,%u,%u,%u,%u,%u,%.3f,%lu,%lu,%lu,%lu,%lu,%u,%lu,%u,%.2f,%.2f,"
    "%.3f,%u,%.3f,%.2f,%.2f,%u,%lu,%lu,%lu,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%u,%u,%u,%u,%u\n",
    lastTempC,
    (unsigned)mode,
    alarmActive ? 1u : 0u,
    frozenLatch ? 1u : 0u,
    cookedLatch ? 1u : 0u,
    latchTooCold ? 1u : 0u,
    latchTooWarm ? 1u : 0u,
    battPercent, vbat,
    (unsigned long)minOutRange,
    (unsigned long)minHeat,
    (unsigned long)minHeatLimit,
    (unsigned long)minOutside,
    (unsigned long)uptimeSec(),
    probeOk ? 1u : 0u,
    (unsigned long)logSize(),
    epochBase ? 1u : 0u,
    mn, mx,
    airTempC, airOk ? 1u : 0u, probeOffset,
    coldScarC, warmScarC, modeMismatch ? 1u : 0u,
    (unsigned long)secondsOutOfRange,
    (unsigned long)secondsAboveMax,
    (unsigned long)secondsOutOfFridge,
    FREEZE_LIMIT, FRIDGE_MIN, FRIDGE_MAX, OUT_WARN, OUT_MAX, COOK_LIMIT,
    statePersistError ? 1u : 0u,
    calibWindowActive() ? 1u : 0u,
    usbPowerPresent() ? 1u : 0u,
    bondAuthorized ? 1u : 0u,
    writeAllowed() ? 1u : 0u);
  sendLine(b);
}

static void dumpLog() {
  sendLine("#B\n");                       // znacznik poczatku dla aplikacji
  if (!fsOk) { sendLine("#E\n"); return; }
  File f(InternalFS);
  if (f.open(LOG_PATH, FILE_O_READ)) {
    uint8_t buf[64];
    int n;
    while ((n = f.read(buf, sizeof(buf))) > 0) {
      bleuart.write(buf, n);
      delay(20);                          // nie zalewaj bufora BLE
    }
    f.close();
  }
  sendLine("#E\n");                       // znacznik konca
}

static void sendStatus() {
  char b[520];
  const char *m = mode == MODE_FRIDGE ? "LODOWKA" : "POZA LODOWKA";
  snprintf(b, sizeof(b),
    "sonda=%.3f C (%s)\npowietrze=%.3f C (TMP117 %s)\n"
    "offset sondy=%+.3f C\ntryb=%s%s\nmin=%.2f max=%.2f\n"
    "alarm=%d\nryzyko zamarzniecia=%d  krytyczne cieplo=%d\n"
    "zatrzask: bylo za zimno=%d, bylo za cieplo=%d\n"
    "czas poza oknem=%lus\nczas powyzej %.1fC=%lus (limit %lus)\n"
    "czas w trybie poza lodowka=%lus\n"
    "bateria=%.3f V (%u%%)\nuptime=%lu s\nzegar=%s\n",
    lastTempC, probeOk ? "ok" : "BLAD",
    airTempC, airOk ? "ok" : "BLAD", probeOffset,
    m, modeMismatch ? " [SPRAWDZ TRYB]" : "", minTempC, maxTempC,
    alarmActive ? 1 : 0, frozenLatch ? 1 : 0, cookedLatch ? 1 : 0,
    latchTooCold ? 1 : 0, latchTooWarm ? 1 : 0,
    (unsigned long)secondsOutOfRange, OUT_MAX,
    (unsigned long)secondsAboveMax, (unsigned long)HEAT_DOSE_LIMIT_S,
    (unsigned long)secondsOutOfFridge,
    vbat, battPercent, (unsigned long)uptimeSec(),
    epochBase ? "ustawiony" : "NIEUSTAWIONY");
  sendLine(b);
}

/*
 * Podzial komend. Odczyt jest jawny, bo nie szkodzi: obcy moze zobaczyc
 * temperature i historie. Zmiana stanu wymaga powiazania utworzonego przy USB.
 *
 * Bare "C" tylko podaje aktualna poprawke, wiec jest odczytem. "C<wartosc>"
 * ja zmienia, wiec juz nie.
 */
static bool commandIsReadOnly(const char *cmd) {
  switch (cmd[0]) {
    case 'P': case 'p':
    case 'S': case 's':
    case 'D': case 'd':
      return true;
    case 'C': case 'c':
      return cmd[1] == 0;
    default:
      return false;
  }
}

static void handleCommand(char *cmd) {
  if (!commandIsReadOnly(cmd) && !writeAllowed()) {
    if (!bondAuthorized) {
      sendLine("blad: brak sparowania. Podlacz USB do czujnika i polacz sie ponownie, "
               "zeby sparowac. Odczyt dziala bez parowania.\n");
    } else {
      sendLine("blad: polaczenie nieszyfrowane. Sprawdz, czy telefon nadal ma "
               "zapisane parowanie z czujnikiem.\n");
    }
    logEvent("CMD_DENIED", cmd);
    return;
  }

  switch (cmd[0]) {
    case 'D': case 'd': dumpLog();    break;
    case 'S': case 's': sendStatus(); break;
    case 'P': case 'p': sendLive();   break;   // odczyt biezacych pol

    case 'M': case 'm':                        // wymus swiezy pomiar wszystkich kanalow
      doSample(true);
      sendLive();
      break;

    case 'F': case 'f':
      setMode(MODE_FRIDGE, "BLE");
      updateAdvertising();
      sendLine("tryb LODOWKA\n");
      break;

    case 'O': case 'o':
      setMode(MODE_OUT, "BLE");
      updateAdvertising();
      sendLine("tryb POZA LODOWKA\n");
      break;

    case 'T': case 't': {
      uint32_t e = strtoul(cmd + 1, nullptr, 10);
      if (e > 1000000000UL) {
        epochBase = e - uptimeSec();
        logEvent("CLOCK_SET", "");
        sendLine("zegar ustawiony\n");
      } else sendLine("blad: uzyj T<epoch>\n");
      break;
    }

    case 'C': case 'c': {
      /*
       * Reczne ustawienie poprawki, np. C-0.24. Zera nie traktujemy
       * specjalnie, wiec C0 czysci kalibracje - do tego sluzy.
       * Kalibracja automatyczna to osobna komenda K.
       */
      if (cmd[1] == 0) {
        char b[64];
        snprintf(b, sizeof(b), "offset sondy=%+.3f C\n", probeOffset);
        sendLine(b);
        break;
      }
      char *end = nullptr;
      float arg = strtof(cmd + 1, &end);
      while (end && (*end == ' ' || *end == '\t')) end++;
      if (end == cmd + 1 || (end && *end != 0) || arg != arg) {
        sendLine("blad: uzyj np. C-0.24 albo C0\n");
        break;
      }
      if (arg < -10.0f || arg > 10.0f) { sendLine("blad: zakres +-10 C\n"); break; }

      float surowy = lastTempC - probeOffset;
      probeOffset = arg;
      if (probeOk) {
        lastTempC = surowy + probeOffset;
        // Zmiana kalibracji moze przeniesc biezacy odczyt przez prog alarmowy.
        // Oceniamy go od razu, ale z delta=0, wiec nie dopisujemy fikcyjnego czasu.
        evaluateAlarms(lastTempC, 0);
      }
      persistStateNow();
      updateAdvertising();

      char b[96];
      snprintf(b, sizeof(b), "offset ustawiony recznie=%+.3f C%s\n", probeOffset,
               statePersistError ? " [BLAD ZAPISU]" : "");
      sendLine(b);
      logEvent("CALIB", b);
      break;
    }

    case 'B': case 'b': {
      /*
       * Otwarcie okna kalibracyjnego. Wykonaj PRZED zanurzeniem sondy, bo
       * przekroczenie powstaje w trakcie dziesieciu minut wychlodzenia,
       * a nie w chwili nacisniecia K.
       */
      calibWindowUntilS = uptimeSec() + CALIB_WINDOW_S;
      char b[120];
      snprintf(b, sizeof(b),
               "okno kalibracyjne otwarte na %lu min: zatrzaski wstrzymane\n",
               (unsigned long)(CALIB_WINDOW_S / 60UL));
      sendLine(b);
      logEvent("CALIB_START", "zatrzaski wstrzymane na czas kapieli lodowej");
      break;
    }

    case 'K': case 'k': {
      /*
       * Kalibracja jednopunktowa w poprawnie przygotowanej kapieli lod-woda,
       * ktora daje punkt odniesienia bliski 0 C. To korekta offsetu, nie
       * gwarancja dokladnosci setnych stopnia w calym zakresie.
       */
      if (!probeOk) { sendLine("blad: sonda nie odpowiada\n"); break; }

      float surowy = lastTempC - probeOffset;
      if (surowy < -3.0f || surowy > 3.0f) {
        sendLine("blad: odczyt daleko od 0 C, sonda nie jest w lodzie\n");
        break;
      }

      bool oknoBylo = calibWindowActive();

      probeOffset = -surowy;
      lastTempC = surowy + probeOffset;

      // Kalibracja konczy procedure, wiec zamykamy okno i wracamy do ochrony.
      calibWindowUntilS = 0;

      // Jak przy recznym C: aktualizujemy stan biezacy bez doliczania czasu.
      evaluateAlarms(lastTempC, 0);
      persistStateNow();
      updateAdvertising();

      char b[160];
      snprintf(b, sizeof(b), "skalibrowano w lodzie, offset=%+.3f C%s%s\n", probeOffset,
               oknoBylo ? "; okno kalibracyjne zamkniete"
                        : "; UWAGA: okno bylo zamkniete, sprawdz pamiec przekroczen",
               statePersistError ? " [BLAD ZAPISU]" : "");
      sendLine(b);
      logEvent("CALIB", b);
      break;
    }

    case 'R': case 'r':
      frozenLatch = cookedLatch = latchTooCold = latchTooWarm = false;
      secondsAboveMax = 0;
      secondsOutOfRange = 0;
      secondsOutOfFridge = 0;
      coldScarC = 999.0f;
      warmScarC = -999.0f;
      inExcursion = false;
      liveTooCold = liveTooWarm = liveWarmWarning = false;
      minTempC = 999.0f;
      maxTempC = -999.0f;
      lastEvalS = uptimeSec();
      lastModeAccountS = uptimeSec();
      modeClockStarted = true;
      critFastUntilS = 0;
      calibWindowUntilS = 0;
      logEvent("RESET", "nowa insulina / reczny reset pamieci");
      if (probeOk) evaluateAlarms(lastTempC, 0);
      else alarmActive = true;
      persistStateNow();
      updateAdvertising();
      sendLine("pamiec przekroczen i liczniki wyzerowane; biezacy pomiar oceniony ponownie\n");
      break;

    case 'I': case 'i':
      // Operacja ratunkowa i destrukcyjna. Nigdy nie wykonujemy jej automatycznie
      // po bledzie montowania, bo to mogloby skasowac odzyskiwalna historie.
      /*
       * Token jest wymagany, bo BLE nie jest tu uwierzytelnione: kazdy telefon
       * w zasiegu moze sie polaczyc i wyslac komende. Jedna litera nie moze
       * kasowac calej historii.
       */
      if (strcmp(cmd, "I!KASUJ") != 0) {
        sendLine("blad: formatowanie wymaga potwierdzenia. Wyslij I!KASUJ\n");
        break;
      }
      if (fsOk) InternalFS.end();
      if (!InternalFS.format()) {
        statePersistError = true;
        sendLine("blad: format pamieci nie udal sie\n");
        break;
      }
      sendLine("pamiec sformatowana; restart\n");
      delay(100);
      NVIC_SystemReset();
      break;

    case 'E': case 'e':
      if (!fsOk) { sendLine("blad: pamiec trwala niedostepna\n"); break; }
      if (InternalFS.exists(LOG_PATH) &&
          (!InternalFS.remove(LOG_PATH) || InternalFS.exists(LOG_PATH))) {
        sendLine("blad: nie udalo sie wymazac logu\n");
        break;
      }
      logEvent("LOG_ERASED", "");
      sendLine("log wymazany\n");
      break;

    default:
      sendLine("komendy: P M D S F O B K C<offset> T<epoch> R E I!KASUJ\n");
  }
  if (cmd[0] == 'R' || cmd[0] == 'r' || cmd[0] == 'T' || cmd[0] == 't' ||
      cmd[0] == 'E' || cmd[0] == 'e' || cmd[0] == 'I' || cmd[0] == 'i' ||
      cmd[0] == 'C' || cmd[0] == 'c' || cmd[0] == 'B' || cmd[0] == 'b' ||
      cmd[0] == 'K' || cmd[0] == 'k' || cmd[0] == 'F' || cmd[0] == 'f' ||
      cmd[0] == 'O' || cmd[0] == 'o') {
    sendLive();                    // aplikacja od razu widzi skutek
  }
}

static void serviceUart() {
  static char buf[32];
  static uint8_t len = 0;
  while (bleuart.available()) {
    char c = (char)bleuart.read();
    if (c == '\n' || c == '\r') {
      if (len) { buf[len] = 0; handleCommand(buf); len = 0; }
    } else if (len < sizeof(buf) - 1) {
      buf[len++] = c;
    }
  }
}

// =============================== SETUP ==============================

/*
 * InternalFS.begin() w bibliotece Adafruit automatycznie formatuje obszar, gdy
 * montowanie sie nie uda. Dla rejestru bezpieczenstwa to zbyt agresywne:
 * uszkodzony, ale potencjalnie odzyskiwalny dziennik nie moze zniknac sam.
 *
 * Dlatego wywolujemy begin() klasy bazowej, ktore tylko montuje. Format wolno
 * wykonac automatycznie jedynie na calkowicie pustym, fabrycznie skasowanym
 * obszarze, czyli przy pierwszym uruchomieniu nowej plytki.
 *
 * WAZNE: ponizsze rzutowanie zadziala tylko wtedy, gdy Adafruit_LittleFS nie ma
 * metod wirtualnych. Gdyby begin() bylo wirtualne, wywolalaby sie wersja
 * pochodna z autoformatem, kod skompilowalby sie bez ostrzezenia, a ochrona
 * zniknelaby PO CICHU. To najgorszy mozliwy tryb awarii, wiec zamieniamy go
 * na blad kompilacji.
 *
 * Jesli ten static_assert nie przejdzie, nie obchodz go. Trzeba wtedy zamiast
 * rzutowania uzyc innej drogi montowania bez autoformatu.
 */
static_assert(!std::is_polymorphic<Adafruit_LittleFS>::value,
              "Adafruit_LittleFS ma metody wirtualne: rzutowanie na klase bazowa "
              "nie ominie autoformatu w InternalFileSystem::begin(). Potrzebne "
              "inne rozwiazanie, patrz komentarz powyzej.");

/*
 * Test pustego obszaru. Wylacznie ODCZYT, nigdy zapis ani kasowanie.
 *
 * Adres jest zaszyty, bo biblioteka nie udostepnia granic obszaru. Jest to
 * jednak bezpieczne: gdyby stala kiedys przestala odpowiadac rzeczywistemu
 * ukladowi pamieci, jedynym skutkiem bedzie zla ODPOWIEDZ, a nie uszkodzenie.
 * Obszar programu nie sklada sie z samych 0xFF, wiec bledny adres da "nie jest
 * pusty", czyli odmowe autoformatu, czyli kierunek ostrozny.
 *
 * Sam format wykonuje InternalFS.format(), ktory uzywa wlasnych, poprawnych
 * granic biblioteki. Nie kasujemy stron recznie: obszar bezposrednio powyzej
 * zajmuje bootloader, a pomylka o jeden region konczylaby sie plytka do
 * odzyskiwania sprzetowego. Zysk z recznego kasowania nie usprawiedliwia
 * takiego ryzyka.
 */
#if defined(NRF52840_XXAA)
static const uint32_t INTERNAL_FS_ADDR  = 0xED000UL;
static const uint32_t INTERNAL_FS_BYTES = 7UL * FLASH_NRF52_PAGE_SIZE;
#endif

static bool internalFsLooksBlank() {
#if defined(NRF52840_XXAA)
  uint8_t buf[32];
  for (uint32_t off = 0; off < INTERNAL_FS_BYTES; off += sizeof(buf)) {
    if (flash_nrf5x_read(buf, INTERNAL_FS_ADDR + off, sizeof(buf)) <= 0) return false;
    for (uint8_t b : buf) if (b != 0xFF) return false;
  }
  return true;
#else
  return false;
#endif
}

static bool beginInternalFsSafely() {
  Adafruit_LittleFS &baseFs = static_cast<Adafruit_LittleFS &>(InternalFS);
  if (baseFs.begin()) return true;

  // Na calkowicie pustej pamieci nie ma historii do utracenia: to pierwszy start.
  if (!internalFsLooksBlank()) return false;
  if (!InternalFS.format()) return false;
  return baseFs.begin();
}

static void powerDownExternalFlash() {
  // KRYTYCZNE. XIAO nRF52840 ma pamiec QSPI 2 MB, ktora po starcie pobiera
  // kilkaset uA. Komenda 0xB9 (deep power-down) zbija to do pojedynczych uA.
  // Bez tego bateria padnie w kilka dni zamiast w kilka miesiecy.
  flashTransport.begin();
  flashTransport.runCommand(0xB9);
  delayMicroseconds(5);
  flashTransport.end();
}

static void ledsOff() {
  // Diody na XIAO sa aktywne stanem niskim - po starcie swieca.
  pinMode(LED_RED, OUTPUT);   digitalWrite(LED_RED, HIGH);
  pinMode(LED_GREEN, OUTPUT); digitalWrite(LED_GREEN, HIGH);
  pinMode(LED_BLUE, OUTPUT);  digitalWrite(LED_BLUE, HIGH);
}

// Flagi ustawiane w kontekscie callbacku, obslugiwane w petli.
// W samym callbacku nie wolno sie blokowac.
static volatile bool identifyPending = false;
static volatile bool livePending     = false;

/*
 * Wykrywanie napiecia na USB. Przy wlaczonym SoftDevice rejestry POWER naleza
 * do niego, wiec pytamy przez wywolanie systemowe, a bezposredni odczyt zostaje
 * jako awaryjny.
 *
 * Swiadomie sprawdzamy samo VBUS, nie enumeracje przez hosta, zeby parowanie
 * dzialalo takze z ladowarki albo powerbanku. Konsekwencja: ladowanie w miejscu
 * publicznym otwiera okno na nowe powiazanie. Jest to opisane w instrukcji.
 */
static bool usbPowerPresent() {
  uint32_t status = 0;
  if (sd_power_usbregstatus_get(&status) == NRF_SUCCESS) {
    return (status & POWER_USBREGSTATUS_VBUSDETECT_Msk) != 0;
  }
  return (NRF_POWER->USBREGSTATUS & POWER_USBREGSTATUS_VBUSDETECT_Msk) != 0;
}

// Zapis dozwolony tylko dla powiazania utworzonego przy USB, na zaszyfrowanym
// polaczeniu. Odczyt pozostaje otwarty dla wszystkich.
static bool writeAllowed() {
  if (!bondAuthorized) return false;
  BLEConnection *c = Bluefruit.Connection(activeConn);
  return c && c->connected() && c->secured();
}

static void onConnect(uint16_t conn_handle) {
  activeConn      = conn_handle;
  identifyPending = true;   // potwierdz, ze rozmawiasz z TYM urzadzeniem
  livePending     = true;

  // O parowanie prosimy tylko przy kablu i tylko gdy powiazania jeszcze nie ma.
  if (usbPowerPresent() && !bondAuthorized) {
    BLEConnection *c = Bluefruit.Connection(conn_handle);
    if (c) c->requestPairing();
  }
}

static void onDisconnect(uint16_t, uint8_t) {
  activeConn = BLE_CONN_HANDLE_INVALID;
  // advertising wraca sam, restartOnDisconnect(true)
}

/*
 * Uzgodnienia parowania nie da sie odrzucic z poziomu tej biblioteki, ale da sie
 * je uniewaznic natychmiast po zakonczeniu. Kierunek jest zamkniety: bez kabla
 * powiazanie nie zostaje, a uprawnienie do zapisu gasnie.
 *
 * Skutek uboczny, swiadomie przyjety: ktos w zasiegu moze wymusic parowanie i tym
 * samym uniewaznic Twoje, zmuszajac Cie do ponownego sparowania przy kablu. Nie
 * zyskuje przy tym zadnego dostepu, a zdarzenie trafia do logu.
 */
static void onPairComplete(uint16_t conn_handle, uint8_t auth_status) {
  if (auth_status != BLE_GAP_SEC_STATUS_SUCCESS) {
    logEvent("PAIR_FAIL", "uzgodnienie parowania nie doszlo do skutku");
    return;
  }

  if (usbPowerPresent()) {
    bondAuthorized = true;
    markStateDirty();
    persistStateNow();
    logEvent("PAIRED", "powiazanie utworzone przy podlaczonym USB");
    return;
  }

  bondAuthorized = false;
  markStateDirty();
  persistStateNow();
  Bluefruit.Periph.clearBonds();
  logEvent("PAIR_REJECTED", "parowanie bez USB; powiazania wyczyszczone");

  BLEConnection *c = Bluefruit.Connection(conn_handle);
  if (c) c->disconnect();
}

void setup() {
  ledsOff();
  powerDownExternalFlash();

  lastMillis = millis();
  fsOk = beginInternalFsSafely();
  statePersistError = !fsOk;
  loadLegacyConfig();
  loadPersistentState();
  lastStateSaveS = uptimeSec();
  lastModeAccountS = uptimeSec();
  modeClockStarted = true;

  // Po restarcie lub wymianie baterii zatrzask krytyczny wczytany z pamieci
  // dostaje krotkie okno szybkiego migania, zebys go nie przegapil.
  if (frozenLatch || cookedLatch) critFastUntilS = uptimeSec() + CRIT_FAST_REMIND_S;

  probeConfigure();          // konfiguracja startowa; doSample powtarza ja przed konwersja

  Bluefruit.begin();
  Bluefruit.setTxPower(TX_POWER);
  Bluefruit.setName(DEVICE_NAME);
  Bluefruit.autoConnLed(false);          // dioda BLE zjada prad
  Bluefruit.Periph.setConnectCallback(onConnect);
  Bluefruit.Periph.setDisconnectCallback(onDisconnect);

  // Just Works: brak PIN-u, brak sekretu do przechowywania. Uprawnienie daje
  // fizyczne podlaczenie kabla, sprawdzane w onPairComplete().
  Bluefruit.Security.setIOCaps(false, false, false);
  Bluefruit.Security.setMITM(false);
  Bluefruit.Security.setPairCompleteCallback(onPairComplete);
  bleuart.begin();

  Bluefruit.ScanResponse.addName();

  // BOOT idzie do logu przed pierwszym pomiarem, zeby wpisy o stanie czujnikow
  // wypadly po nim, a nie przed nim.
  logEvent("BOOT", "start urzadzenia");

  lastSummaryS = uptimeSec();
  doSample(true);             // pierwszy pomiar: wszystkie kanaly
  if (probeOk) minTempC = maxTempC = lastTempC;

  lastSampleMs = millis();
}

// =============================== LOOP ===============================

void loop() {
  tickClock();

  if ((uint32_t)(millis() - lastSampleMs) >= SAMPLE_PERIOD_MS) {
    doSample();
    livePending = true;
  }

  ledService();

  if (Bluefruit.connected()) {
    if (identifyPending) {
      identifyPending = false;
      pulse(LED_GREEN, 0xFF, 2, 120);   // "tak, to ja"
    }

    static uint32_t lastLiveMs = 0;
    if (livePending || (uint32_t)(millis() - lastLiveMs) >= LIVE_PERIOD_MS) {
      lastLiveMs = millis();
      livePending = false;
      sendLive();
    }

    serviceUart();
    delay(50);
  } else {
    livePending = false;
    delay(1000);      // CPU spi, FreeRTOS w trybie tickless
  }
}
