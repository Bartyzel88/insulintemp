# InsuTemp — instrukcja od zera

Monitor temperatury insuliny. Instrukcja napisana przy założeniu, że **nigdy wcześniej nie lutowałeś** i nie pisałeś programów na mikrokontrolery. Nic tu nie jest trudne, ale kolejność ma znaczenie i kilka rzeczy da się zepsuć nieodwracalnie, więc czytaj po kolei.

Realny czas: **jeden wieczór** na naukę lutowania, montaż i oprogramowanie, jeśli nic nie pójdzie nie tak. Potem około 15 minut na kontrolę/kalibrację i co najmniej kilka dni obserwacji porównawczej, zanim zaczniesz polegać na wskazaniach urządzenia jako dodatkowym ostrzeżeniu.

---

## Spis treści

1. [Co budujesz](#1-co-budujesz)
2. [Zakupy](#2-zakupy)
3. [Zanim weźmiesz lutownicę](#3-zanim-weźmiesz-lutownicę)
4. [Montaż](#4-montaż)
5. [Oprogramowanie na czujnik](#5-oprogramowanie-na-czujnik)
6. [Aplikacja na telefon](#6-aplikacja-na-telefon)
7. [Kalibracja i testy](#7-kalibracja-i-testy)
8. [Zabezpieczenie i tydzień próbny](#8-zabezpieczenie-i-tydzień-próbny)
9. [Codzienne używanie](#9-codzienne-używanie)
10. [Gdy coś nie działa](#10-gdy-coś-nie-działa)
11. [Zastrzeżenie](#11-zastrzeżenie)

---

## 1. Co budujesz

Małe urządzenie na baterii, które leży w lodówce obok insuliny i pilnuje jej temperatury. Ma dwa czujniki:

| Czujnik | Gdzie siedzi | Do czego służy |
|---|---|---|
| **Sonda DS18B20** — stalowa rurka na kablu | wsunięta do kartonika przy opakowaniu | mierzy temperaturę metalowej sondy; jest to przybliżenie temperatury produktu, a nie bezpośredni pomiar cieczy |
| **TMP117** — mała płytka | swobodnie w powietrzu | mierzy temperaturę otoczenia i pomaga ocenić, czy wybrany tryb ma sens |

Dwa czujniki dają więcej kontekstu, ale **nie potrafią jednoznacznie rozróżnić awarii lodówki od wyjęcia całego zestawu z lodówki** — w obu sytuacjach oba czujniki mogą się nagrzewać. Dlatego tryb pracy ustawiasz jawnie w aplikacji: **Lodówka** albo **Poza lodówką**. TMP117 może ostrzec o podejrzanej kombinacji, ale nigdy sam nie zmienia progów alarmowych.

**Najważniejsza cecha:** przekroczenia i liczniki są zapisywane do pamięci nieulotnej. Krytyczne zatrzaski zapisują się natychmiast; liczniki są zapisywane rzadziej, aby oszczędzać energię i pamięć flash. Zatrzask zdejmujesz ręcznie dopiero wtedy, gdy świadomie rozpoczynasz monitorowanie nowego opakowania.

Wyniki odczytujesz na dwa sposoby: **dioda** mruga kolorem, gdy otwierasz lodówkę, a **aplikacja na telefonie** pokazuje szczegóły i historię przez Bluetooth.

---

## 2. Zakupy

### 2.1 Botland — elementy

| # | Produkt | Symbol | Szt. | Orientacyjnie |
|---|---|---|---|---|
| 1 | Seeed Xiao nRF52840 Plus | 26201 | 1 | ~60 zł |
| 2 | TMP117 — czujnik temperatury I2C, STEMMA QT (Adafruit 4821) | 18997 | 1 | ~80 zł |
| 3 | **Sonda wodoodporna DS18B20, 1 m, przewód silikonowy** | 10938 | 1 | ~25 zł |
| 4 | Akumulator Li-Pol Akyga 470 mAh 1S 3,7 V, przewody 50 mm | 6049 | 1 | ~30 zł |
| 5 | Zestaw przewodów 30 AWG, 5 szpul, justPi | 21083 | 1 | ~22 zł |
| 6 | Zestaw koszulek termokurczliwych, 100 szt. | 5375 | 1 | ~18 zł |
| 7 | Rezystor 4,7 kΩ — kup zestaw rezystorów, przyda się | — | 1 | ~15 zł |
| 8 | Lakier ochronny do płytek PVB 60, 50 ml | — | 1 | ~30 zł |

> **Pozycja 3:** wybierz wersję z **przewodem silikonowym**, nie PVC. Silikon nie sztywnieje i nie pęka przy zginaniu w zimnie, a ten kabel będzie wyciągany z kartonika przy każdej wymianie opakowania.

> **Pozycja 2 — ważne dla baterii:** sam układ TMP117 jest bardzo energooszczędny, ale płytka Adafruit 4821 ma na schemacie stale zasilaną diodę **ON**. Firmware nie może jej wyłączyć. Jeżeli priorytetem jest długi czas pracy, potraktuj jako docelowy wariant albo moduł TMP117 **bez stałej diody zasilania**, albo po pełnym uruchomieniu zleć osobie mającej doświadczenie w SMD usunięcie/odłączenie samej diody ON lub jej rezystora szeregowego. Urządzenie będzie działało z niemodyfikowanym Adafruit 4821, ale czas pracy na baterii może być wyraźnie krótszy. Nie przecinaj ścieżek „na oko”.

> **Pozycja 4:** to ogniwo ma gołe przewody, więc nic nie obcinasz, i ma wbudowane zabezpieczenie PCM. To jest wymóg, nie preferencja — płytka XIAO ma ładowarkę, ale nie ma ochrony przed nadmiernym rozładowaniem.

### 2.2 Botland — narzędzia, jeśli nie masz

| Produkt | Po co |
|---|---|
| **Stacja lutownicza z regulacją temperatury**, grot ok. 1 mm | Zwykła lutownica bez regulacji też zadziała, ale ze stacją jest znacznie łatwiej i trudniej coś przegrzać |
| **Cyna 60/40 z topnikiem, 0,5–0,7 mm** | Weź ołowiową, nie bezołowiową. Ołowiowa topi się niżej i wybacza więcej błędów. Po pracy umyj ręce |
| **Topnik w żelu** | Sprawia, że cyna sama płynie tam, gdzie trzeba. Największa pojedyncza pomoc dla początkującego |
| **Trzecia ręka** — podstawka z krokodylkami | Bez tego lutujesz dwoma rękami trzy rzeczy naraz. Nie da się |
| **Szczypce boczne** (cążki) | Do przycinania przewodów |
| **Ściągacz izolacji** | Można nożykiem, ale łatwo przeciąć żyłę |
| **Multimetr** | Do sprawdzenia polaryzacji baterii. **Ten jeden pomiar może uratować płytkę za 60 zł** |
| **Pistolet do kleju na gorąco** | Odciążenie mechaniczne pól baterii, krok 4.6. Zamiennie taśma kaptonowa, ale klej jest wyraźnie lepszy |
| **Plecionka rozlutownicza** | Do ściągnięcia nadmiaru cyny, gdy zrobisz mostek. Kosztuje kilka złotych i raz na pewno się przyda |
| **Izopropanol + wacik** | Do zmycia topnika po lutowaniu |

### 2.3 Poza Botlandem

| Produkt | Gdzie |
|---|---|
| Kabel USB-C (do danych, nie samo ładowanie) | masz w domu |
| Lód i kubek | do kalibracji |
| Telefon z Androidem, Chrome, Bluetooth | masz |

---

## 3. Zanim weźmiesz lutownicę

**Przeczytaj ten rozdział, nawet jeśli chcesz od razu zacząć.** Zajmie ci 5 minut i oszczędzi wieczór.

### 3.1 Bezpieczeństwo

- Grot ma około **350 °C**. Dotknięcie to natychmiastowe poparzenie. Odkładaj lutownicę zawsze na podstawkę, nigdy na blat.
- **Wietrz pomieszczenie.** Dym z topnika drażni drogi oddechowe.
- **Nigdy nie lutuj przy podłączonej baterii LiPo.** Zwarcie może ją zapalić. Bateria wchodzi dopiero na końcu, w kroku 4.6.

### 3.2 Jak się lutuje

Najczęstszy błąd początkującego: przykładanie cyny do grotu i przenoszenie kropli na złącze. To daje tak zwany zimny lut, który wygląda jak połączenie, ale nim nie jest, i psuje się po tygodniu.

Poprawnie:

1. **Ustaw stację na 350 °C.**
2. **Ocynuj grot** — dotknij go cyną, aż pokryje się srebrną warstwą. Rób to co kilka minut.
3. **Podgrzej złącze, nie cynę.** Dotknij grotem jednocześnie przewodu i pola lutowniczego, policz „raz, dwa".
4. **Dotknij cyną złącza**, nie grotu. Cyna wciągnie się sama, bo gorący metal ją przyciąga.
5. **Zabierz cynę, potem grot.** Nie ruszaj przez 2 sekundy, aż zastygnie.

**Dobry lut** wygląda jak błyszczący stożek wtopiony w pole. **Zły lut** to matowa kulka leżąca na wierzchu. Jeśli widzisz kulkę, dodaj topnika i podgrzej ponownie.

### 3.3 Poćwicz na złomie

Weź 20 cm przewodu z zestawu, potnij na kawałki i połącz je ze sobą pięć razy. To jest cały trening, którego potrzebujesz. Piąty lut będzie wyraźnie lepszy od pierwszego, a chcesz, żeby pierwszy lut na płytce za 80 zł był tym piątym.

### 3.4 Zdejmowanie izolacji z cienkiego drutu

To pierwsza rzecz, która cię zaskoczy. Przewód 30 AWG z zestawu jest bardzo cienki, a **większość ściągaczy izolacji ma najmniejsze gniazdo dopiero na 20 AWG** i po prostu przetnie taką żyłkę na pół.

Trzy sposoby, od najlepszego:

1. **Ściągacz z gniazdem 30 AWG.** Jeśli kupujesz nowy, sprawdź, czy skala schodzi tak nisko. Wtedy jest to kwestia jednego ruchu.
2. **Skalpel albo nożyk, techniką rolowania.** Połóż przewód na twardym blacie, przyłóż ostrze **prostopadle** i obracaj przewód pod ostrzem, naciskając bardzo lekko. Nacinasz izolację po okręgu, nie próbujesz jej przeciąć. Potem izolacja zsuwa się paznokciem. Jeśli w środku widzisz nacięty, srebrny miedziany rdzeń — naciskałeś za mocno, obetnij i zrób jeszcze raz.
3. **Grubszy przewód.** Jeśli walka z 30 AWG cię zniechęci, kup linkę silikonową 26 albo 28 AWG. Jest wyraźnie łatwiejsza w obróbce, dobrze wchodzi w otwory XIAO, a jedyny koszt to nieco większa objętość wiązki. Dla pierwszego projektu to rozsądny wybór.

### 3.5 Ocynuj końcówki

Po zdjęciu 3 mm izolacji dotknij końcówki grotem i podaj cynę. Drut pokryje się cyną i zesztywnieje, dzięki czemu łatwo wchodzi w otwór i nie strzępi się. Rób to z każdym przewodem, zawsze, przed włożeniem gdziekolwiek.

---

## 4. Montaż

> **Zasada nadrzędna: sprawdzasz po każdym kroku.** Jeśli coś nie działa, wiesz dokładnie co, bo poprzedni krok był dobry. Bateria i lakier idą na końcu, bo to najtrudniej odwrócić.

### 4.1 Zapoznaj się z płytką

Weź XIAO do ręki. Z jednej strony gniazdo USB-C, obok mały przycisk reset. Po bokach dwa rzędy otworów z opisami: `3V3`, `GND`, `D0`, `D1`, `D2` i tak dalej. Z tyłu dwa pola opisane `BAT+` i `BAT−`.

Będziesz używał pięciu otworów: **3V3, GND, D2, D4, D5**. Do jednego otworu wejdą czasem dwa przewody — to normalne.

### 4.2 Podłącz TMP117 (cztery przewody)

Potnij cztery przewody po 6 cm, najlepiej w różnych kolorach. Ocynuj oba końce każdego.

```
TMP117 (mała płytka)          XIAO nRF52840 Plus
   VIN  ──────────────────────  3V3      (czerwony)
   GND  ──────────────────────  GND      (czarny)
   SDA  ──────────────────────  D4       (zielony)
   SCL  ──────────────────────  D5       (żółty)
```

Procedura dla każdego przewodu:

1. Nasuń na przewód 1 cm koszulki termokurczliwej i **zsuń ją daleko na środek przewodu**, żeby nie skurczyła się przedwcześnie od ciepła lutownicy.
2. Zdejmij 3 mm izolacji, ocynuj końcówkę.
3. Włóż końcówkę w otwór na płytce TMP117, zalutuj, odetnij wystający nadmiar.
4. To samo z drugim końcem w odpowiednim otworze XIAO.
5. Zsuń koszulkę wzdłuż przewodu tak, żeby dobiła do płytki, i podgrzej ją, trzymając lutownicę **obok**, nie dotykając.

> **Co ta koszulka właściwie robi.** Nie osłania samego lutu, bo lut jest po drugiej stronie płytki i nie da się na niego nic nasunąć. Koszulka zakrywa odcinek goły przewodu przy płytce i usztywnia miejsce, w którym przewód najbardziej się zgina. To ochrona przed złamaniem żyły, nie przed zwarciem. Przy lutowaniu w otwór jest opcjonalna, ale warto, bo te cztery przewody będą się ruszać przy każdym przekładaniu urządzenia.

Zwory `ADDR` z tyłu TMP117 **nie ruszaj**. Płytka ma już wszystko, czego potrzebuje do pomiaru.

> **Wariant niskopoborowy:** podczas pierwszego uruchomienia zostaw moduł bez zmian, żeby łatwo diagnozować montaż. Dopiero po przejściu wszystkich testów usuń problem stałej diody ON zgodnie z uwagą w rozdziale 2.1 albo użyj płytki TMP117 bez takiej diody. Zaklejenie diody nie zmniejsza jej poboru prądu.

**✅ Punkt kontrolny:** pociągnij delikatnie każdy przewód. Żaden nie może się ruszyć. Obejrzyj luty pod dobrym światłem — mają błyszczeć.

### 4.3 Podłącz sondę (trzy przewody plus rezystor)

Sonda ma trzy żyły: czerwoną, czarną i żółtą (czasem białą lub niebieską).

```
Sonda DS18B20                 XIAO nRF52840 Plus
   czerwony ─────────────────── 3V3
   czarny   ─────────────────── GND
   żółty    ─────────────────── D2
                                 │
                            rezystor 4,7 kΩ
                                 │
                                3V3
```

Kabel sondy ma metr długości, czyli znacznie więcej, niż potrzebujesz. **Nie skracaj go** — łatwiej zwinąć nadmiar w pętlę i spiąć opaską niż potem żałować. Zwis schowasz w lodówce za kartonikiem.

**Rezystor 4,7 kΩ jest obowiązkowy.** Bez niego sonda nie odezwie się w ogóle. Rozpoznasz go po paskach: **żółty, fioletowy, czerwony**, plus złoty lub srebrny na końcu. Rezystor nie ma kierunku, można go wlutować dowolną stroną.

Najprościej tak:

1. Przytnij nóżki rezystora do **1 cm** każda.
2. **Nasuń na obie nóżki krótkie koszulki, po 6 mm**, dosuwając je do korpusu. Teraz, nie później — po zalutowaniu obu końców nie da się już niczego nasunąć.
3. Przylutuj jedną nóżkę do otworu `D2`, razem z żółtym przewodem sondy.
4. Drugą do `3V3`, razem z czerwonymi przewodami.
5. Podgrzej koszulki.

Gołe nóżki rezystora są jedynym miejscem w tym urządzeniu, gdzie naprawdę grozi zwarcie, bo biegną w powietrzu między dwoma punktami płytki. Dlatego tu koszulka nie jest ozdobą, tylko ma konkretne zadanie.

**✅ Punkt kontrolny:** w otworze `3V3` masz teraz trzy rzeczy: przewód od TMP117, czerwony od sondy i nóżkę rezystora. W `D2` dwie: żółty od sondy i drugą nóżkę rezystora. To poprawne.

### 4.4 Pierwsze uruchomienie

Podłącz XIAO kablem USB-C do komputera.

**✅ Punkt kontrolny:** w komputerze pojawia się nowy port szeregowy. Na Windowsie zobaczysz go w Menedżerze urządzeń, na Linuksie jako `/dev/ttyACM0`, ale najprościej sprawdzić w Arduino IDE w **Narzędzia → Port**. Nie polegaj na tym, czy zapaliła się jakaś dioda — świecenie zależy od tego, co producent wgrał fabrycznie, i bywa różne.

**Teraz przejdź do rozdziału 5 i wgraj oprogramowanie.** Wróć tutaj, gdy czujnik zacznie mrugać co kilkanaście sekund.

### 4.5 Sprawdź, czy oba czujniki żyją

Po wgraniu oprogramowania zainstaluj aplikację (rozdział 6) i połącz się z czujnikiem. Aplikacja pokaże temperaturę insuliny, a pod spodem temperaturę powietrza.

**✅ Punkt kontrolny:** oba odczyty po ustabilizowaniu pokazują zbliżoną temperaturę pokoju. Nie wymagaj konkretnej różnicy co do kilku dziesiątych stopnia — czujniki mają inną konstrukcję i bezwładność cieplną.

Chwyć teraz stalową końcówkę sondy w dłoń. Odczyt sondy zacznie rosnąć po kolejnym zaplanowanym pomiarze (maksymalnie około minuty) i z pewnym opóźnieniem termicznym. Jeśli nie chcesz czekać, naciśnij **Zmierz teraz**.

Jeśli któryś czujnik nie odpowiada, zajrzyj do rozdziału 10. **Nie idź dalej, dopóki oba nie działają.**

### 4.6 Bateria

> **To jedyny nieodwracalny krok w całym projekcie.** Bateria podłączona odwrotnie niszczy płytkę natychmiast. Zwarcie między polami zapala ogniwo Li-Pol.

**Ustal, który przewód to plus**

1. **Odłącz USB.**
2. Ustaw multimetr na pomiar napięcia stałego, zakres 20 V.
3. Dotknij czerwoną sondą jednego przewodu baterii, czarną drugiego. Jeśli wyświetli **około +3,8 V**, czerwona sonda dotyka plusa. Jeśli pokaże wartość **z minusem**, sondy są odwrotnie.
4. **Zanotuj to na kartce.** Nie ufaj kolorom przewodów, sprawdź.

**Przygotuj pola, zanim podłączysz cokolwiek**

Pola `BAT+` i `BAT−` z tyłu XIAO leżą **blisko siebie**. Kropla cyny, która połączy je w mostek, zwiera baterię, a to przy Li-Pol jest realnie groźne. Dlatego mostek wyłapujesz teraz, gdy bateria jest jeszcze odłączona i nic nie może się stać.

5. Nałóż na każde pole maleńką kroplę cyny, osobno. Dwie sekundy grotem na pole, nie więcej.
6. **Obejrzyj przerwę między polami pod dobrym światłem.** Musi być widoczna goła płytka. Jeśli cyna łączy pola, dotknij mostka czystym grotem i ściągnij nadmiar, albo użyj plecionki rozlutowniczej.
7. Skróć przewody baterii do 3 cm i ocynuj końcówki.

   > **Przycinaj po jednym.** Jeśli przetniesz oba naraz jednymi cążkami, metalowe ostrze zewrze bieguny ogniwa. To najczęstszy sposób, w jaki początkujący niszczą baterię Li-Pol, i jedyny w tym projekcie, który może się skończyć ogniem. Przytnij jeden przewód, odsuń go na bok, potem drugi.

**Przylutuj**

8. Przyłóż ocynowaną końcówkę do ocynowanego pola i dotknij grotem na dwie sekundy. Cyna z obu stron połączy się sama, nie dodawaj nowej.
9. Powtórz dla drugiego przewodu.
10. Obejrzyj przerwę jeszcze raz.

**Zabezpiecz mechanicznie**

11. Nałóż **kroplę kleju na gorąco** na oba pola razem z pierwszymi 5 mm przewodów.

Nie chodzi o izolację, a o odciążenie. To najsłabsze mechanicznie miejsce w całym urządzeniu: pola `BAT` są małe i przy szarpnięciu przewodu odrywają się od płytki razem ze ścieżką, co jest naprawą praktycznie niewykonalną. Klej przenosi siłę na powierzchnię płytki zamiast na lut.

Jeśli nie masz pistoletu do kleju, przyklej przewody taśmą kaptonową do płytki dwa centymetry od lutów, tak żeby żadne szarpnięcie nie dochodziło do pól.

**✅ Punkt kontrolny:** czujnik zaczyna mrugać bez podłączonego USB.

### 4.7 Gdzie co ma leżeć

**Sondę wsuń do kartonika z insuliną, na głębokość co najmniej 3–4 cm.** Nie sam czubek — stal i kabel przewodzą ciepło, więc przy płytkim wsunięciu odczyt zostanie ściągnięty w stronę powietrza. Ułóż ją tak, żeby dotykała fiolek albo blistra.

**Płytkę zostaw swobodnie w powietrzu**, obok kartonika. Nie owijaj jej niczym i nie wkładaj do pudełka — jej zadaniem jest mierzyć lodówkę.

Umieść zestaw w miejscu zalecanym dla leku i unikaj bezpośredniego kontaktu z elementem chłodzącym/tylną ścianką. Rozkład temperatury zależy od konstrukcji lodówki, dlatego w tygodniu próbnym sprawdź log w kilku położeniach.

---

## 5. Oprogramowanie na czujnik

### 5.1 Zainstaluj Arduino IDE

Pobierz **Arduino IDE 2.x** ze strony arduino.cc, zainstaluj, uruchom.

### 5.2 Dodaj obsługę płytki XIAO

Arduino domyślnie nie zna tej płytki. Trzeba mu ją pokazać.

1. **Plik → Ustawienia**.
2. W polu **Dodatkowe adresy URL menedżera płytek** wklej:
   ```
   https://files.seeedstudio.com/arduino/package_seeeduino_boards_index.json
   ```
   Jeśli adres nie zadziała, aktualny znajdziesz na `wiki.seeedstudio.com/XIAO_BLE`.
3. **OK**.
4. Kliknij ikonę **Menedżer płytek** (druga od góry w lewym pasku), wpisz `seeed nrf52`.
5. Zainstaluj **„Seeed nRF52 Boards"**.

> **Uwaga, to jest częsty błąd.** Na liście będą dwie pozycje. Potrzebujesz tej **bez** słowa **mbed**. Ten projekt jest przygotowany dla wariantu **bez mbed**. Nie zakładaj zgodności z rdzeniem mbed bez ponownej kompilacji i testu poboru prądu.

6. **Narzędzia → Płytka → Seeed nRF52 Boards → Seeed XIAO nRF52840 Plus**. Jeśli nie ma wersji Plus, wybierz zwykłą **Seeed XIAO nRF52840** — działa tak samo.

### 5.3 Zainstaluj dwie biblioteki

Kliknij ikonę **Menedżer bibliotek** (trzecia od góry) i zainstaluj po kolei:

| Biblioteka | Autor | Do czego |
|---|---|---|
| **Adafruit SPIFlash** | Adafruit | Pozwala firmware zarządzać zewnętrzną pamięcią QSPI i przełączać ją w tryb oszczędzania energii |
| **OneWire** | Paul Stoffregen | Obsługa sondy |

Przy instalacji może zapytać o dodatkowe zależności — zgódź się.

> Spotkasz w internecie poradniki, które do DS18B20 każą instalować także bibliotekę **DallasTemperature**. Tutaj nie jest potrzebna. Program rozmawia z sondą bezpośrednio, bo tak jest mniej operacji na magistrali, a to ważne, gdy obok pracuje Bluetooth.

### 5.4 Wgraj program

1. Utwórz folder o nazwie **`insulin_temp`** i wrzuć do niego plik **`insulin_temp.ino`**. Nazwa folderu musi być identyczna jak nazwa pliku, Arduino tego wymaga.
2. Otwórz plik podwójnym kliknięciem.
3. Podłącz XIAO kablem USB-C.
4. **Narzędzia → Port** — wybierz ten, który się pojawił po podłączeniu.
5. Kliknij strzałkę **Wgraj** (druga ikona u góry).

Kompilacja przy pierwszym razie trwa 1–2 minuty. Na dole zobaczysz zielony komunikat o powodzeniu.

**Jeśli port się nie pojawia:** naciśnij **szybko dwa razy** przycisk reset obok gniazda USB. Komputer wykryje dysk `XIAO-SENSE` — to tryb awaryjny do wgrywania. Wybierz port ponownie i wgraj.

**✅ Punkt kontrolny:** po wgraniu dioda RGB wykonuje krótki błysk. W stanie prawidłowym zielony heartbeat pojawia się mniej więcej raz na minutę, więc nie oczekuj ciągłego świecenia.

---

## 6. Aplikacja na telefon

Aplikacja łączy się z czujnikiem przez Bluetooth. Musi być udostępniona przez internet po HTTPS — przeglądarki nie pozwalają stronie otwartej z pliku korzystać z Bluetooth. Najprostszy darmowy sposób to GitHub Pages.

### 6.1 Załóż konto GitHub

Wejdź na `github.com`, kliknij **Sign up**, podaj adres e-mail i hasło, potwierdź e-mail. Nic więcej nie musisz konfigurować.

### 6.2 Utwórz repozytorium

1. Kliknij **+** w prawym górnym rogu → **New repository**.
2. **Repository name:** `insutemp`
3. Widoczność: **Public**.

   > Publiczne repozytorium jest najprostszą opcją dla GitHub Pages. Kod tej aplikacji **nie wysyła danych pomiarowych na własny serwer**; pomiary są pobierane z urządzenia przez Bluetooth. Samo wejście na stronę GitHub Pages jest jednak zwykłym ruchem sieciowym do GitHuba i podlega jego zasadom prywatności.

4. **Create repository**.

### 6.3 Wgraj pliki

Na pustej stronie repozytorium kliknij link **uploading an existing file**. Przeciągnij **wszystkie osiem plików** naraz:

```
index.html      manifest.json      sw.js
icon-192.png    icon-512.png       icon-512-maskable.png
apple-touch-icon.png               favicon-32.png
```

Kliknij **Commit changes**.

**✅ Punkt kontrolny:** na stronie repozytorium widzisz dokładnie osiem plików. Ikony i manifest są dołączone po to, aby instalacja PWA była możliwie zgodna z różnymi wersjami Androida/Chrome.

### 6.4 Włącz publikowanie

1. Zakładka **Settings** (u góry, po prawej).
2. W lewym menu **Pages**.
3. **Source:** `Deploy from a branch`
4. **Branch:** `main`, folder `/ (root)`
5. **Save**.

Poczekaj 1–3 minuty. Odśwież stronę — na górze pojawi się twój adres:

```
https://TWOJA-NAZWA.github.io/insutemp/
```

### 6.5 Zainstaluj na Androidzie

1. Otwórz opublikowany adres w **Chrome** na telefonie.
2. Włącz **Bluetooth**.
3. W górnej listwie pojawi się przycisk **Zainstaluj**. Naciśnij go i potwierdź.

   > Jeśli przycisk się nie pojawia, odśwież stronę raz jeszcze — potrzebny jest moment na przygotowanie w tle. Na dole ekranu startowego są cztery odczyty diagnostyczne, które pokażą, co jeszcze nie jest gotowe.

4. Na ekranie głównym pojawi się ikona InsuTemp. Otwórz ją — aplikacja uruchomi się bez paska przeglądarki.

> Sposób instalacji zależy od wersji Chrome. Jeśli aplikacja oferuje własny przycisk **Zainstaluj**, użyj go. Jeżeli Chrome pokazuje opcję instalacji w menu, ona również może być prawidłowa; po instalacji sprawdź, czy InsuTemp uruchamia się jako osobne okno.

### 6.6 Połącz się z czujnikiem

Naciśnij **Połącz**, wybierz z listy **InsuTemp**. Czujnik mrugnie dwa razy na zielono, potwierdzając, że to on.

> **Jeśli lista urządzeń jest pusta:** Android wymaga od przeglądarki uprawnienia do wyszukiwania urządzeń w pobliżu. Wejdź w **Ustawienia → Aplikacje → Chrome → Uprawnienia** i włącz **Urządzenia w pobliżu**, a na starszych Androidach także **Lokalizacja**. To wymóg systemu, nie aplikacji.

**✅ Punkt kontrolny:** widzisz temperaturę sondy przy opakowaniu i pod spodem temperaturę powietrza. Wróć teraz do kroku 4.5.

---

## 7. Kalibracja i testy

### 7.1 Kontrola i kalibracja sondy

DS18B20 ma katalogowo dokładność **±0,5 °C** w zakresie −10…+85 °C. Firmware pracuje w rozdzielczości 11-bitowej, czyli krok odczytu wynosi **0,125 °C**. Jednopunktowa kalibracja w pobliżu 0 °C może skorygować offset konkretnej sondy, ale **nie gwarantuje dokładności ±0,05 °C ani stałej poprawki w całym zakresie 2–8 °C**.

Kalibracja jest zalecana jako kontrola jakości sondy, ale traktuj ją jako korektę offsetu, nie certyfikację termometru.

1. Przygotuj wysokie naczynie z dużą ilością **drobno pokruszonego lodu** i tylko taką ilością zimnej wody, aby wypełnić szczeliny. Wymieszaj i odczekaj kilka minut. Dobrze przygotowana kąpiel lodowo-wodna utrzymuje temperaturę blisko 0 °C, o ile jednocześnie obecne są lód i woda.
2. Zanurz metalową część sondy dostatecznie głęboko i nie dotykaj nią dna ani ścianek. **Rób to tylko wtedy, gdy producent konkretnej sondy deklaruje ją jako wodoodporną.** Nie zanurzaj połączeń przewodów, XIAO ani TMP117.
3. Odczekaj około 10 minut, delikatnie mieszając kąpiel co jakiś czas.
4. W aplikacji naciśnij **Kalibruj w lodzie**. Aplikacja pokaże odczyt surowy i proponowany offset. Zapisz go tylko wtedy, gdy warunki kąpieli są stabilne.

Poprawka zapisuje się w pamięci nieulotnej urządzenia i przetrwa restart oraz odłączenie baterii.

**✅ Punkt kontrolny:** po kalibracji odczyt powinien pozostać blisko 0 °C i zmieniać się skokami po 0,125 °C. Nie oczekuj wyniku 0,00 ±0,05 °C — taka deklaracja byłaby dokładniejsza niż specyfikacja samego DS18B20.

Jeżeli odczyt jest niestabilny o wiele dziesiątych stopnia mimo ustabilizowanej kąpieli, najpierw sprawdź połączenia, zasilanie, głębokość zanurzenia i jakość samej kąpieli. Sam taki objaw nie dowodzi, że układ jest podróbką.

### 7.2 Sprawdzenie TMP117

TMP117 jest fabrycznie kalibrowanym czujnikiem i w tym projekcie nie ma dla niego ręcznego offsetu. Połóż oba czujniki obok siebie na co najmniej 30 minut i sprawdź, czy wskazania są rozsądnie zbliżone. Ze względu na różne obudowy i bezwładność termiczną kilka dziesiątych stopnia różnicy nie jest samo w sobie alarmem; duża i trwała rozbieżność wymaga sprawdzenia montażu.

### 7.3 Test zasięgu

Włóż urządzenie do lodówki, zamknij drzwi, stań tam, gdzie zwykle stoisz, spróbuj się połączyć.

Lodówka jest metalową skrzynią i Bluetooth często z niej nie wychodzi. **Jeśli połączenie nie działa, to nie awaria** — po prostu odczyt będzie możliwy dopiero po otwarciu drzwi. Dioda działa niezależnie i to ona jest podstawowym kanałem.

---

## 8. Zabezpieczenie i tydzień próbny

Rób to po udanej kalibracji. Urządzenie jest już wtedy w pełni sprawdzone, więc zabezpieczasz coś, co działa.

> **Dlaczego zabezpieczenie idzie przed tygodniem próbnym, a nie po.** Wilgoć w lodówce działa od pierwszego dnia, więc tydzień bez lakieru byłby tygodniem niepotrzebnego ryzyka. Lakier PVB da się przelutować i zmyć rozpuszczalnikiem, więc nie zamyka drogi do poprawek.

### 8.1 Lakier ochronny

Cykle temperaturowe skraplają wodę na płytkach. Wilgoć na połączeniach powoduje zawieszki, których nie zdiagnozujesz. **Ten krok nie jest opcjonalny.**

Zaklej taśmą przed lakierowaniem:

- gniazdo USB-C,
- pola `BAT+` i `BAT−` z przylutowanymi przewodami,
- **korpus czujnika TMP117** — mały czarny prostopadłościan,
- diodę RGB, jeśli zostawiasz nad nią okienko.

Nałóż cienką warstwę pędzelkiem na obie strony obu płytek. Susz zgodnie z opisem na opakowaniu. Sondy nie lakieruj — jest ze stali.

### 8.2 Obudowa

Najprościej: gruba koszulka termokurczliwa obejmująca płytkę XIAO razem z baterią. Ogniwo 30 × 30 mm jest szersze od płytki, więc ono wyznacza rozmiar. Płytka TMP117 zostaje na zewnątrz, na swoich przewodach, bo ma mierzyć powietrze.

Zanim zaciśniesz koszulkę, zaplanuj **dwa wycięcia**:

| Wycięcie | Dlaczego |
|---|---|
| **Gniazdo USB-C** | Bez tego nie naładujesz baterii ani nie wgrasz poprawki. Najczęściej pomijane, a zamurowuje urządzenie na dobre |
| **Dioda RGB** | Koszulka jest nieprzezroczysta, a dioda to twój podstawowy kanał odczytu |

Oba te elementy są po tej samej stronie płytki, blisko krawędzi, więc jedno szersze wycięcie na tym boku załatwia sprawę. Nad samą diodą możesz kropnąć klej na gorąco — zadziała jednocześnie jako prowadnica światła i uszczelnienie.

**✅ Punkt kontrolny:** podłącz USB-C przez wycięcie. Wchodzi bez naciągania koszulki i ładowanie startuje.

### 8.3 Tydzień próbny

Zanim zaczniesz polegać na urządzeniu, zostaw je na tydzień w lodówce obok insuliny i zaglądaj codziennie na diodę, a raz pobierz log.

Zobaczysz, jak zachowuje się twoja konkretna lodówka. Sprawdź, czy odczyty są stabilne, czy nie ma okresowych błędów sondy oraz czy po restarcie nadal pozostają wcześniejsze zatrzaski i liczniki. Jeśli pojawiają się alerty „za zimno", nie zakładaj od razu winy sondy — porównaj położenie, log i rzeczywistą temperaturę lodówki.

Przetestuj też ręcznie oba tryby. **Lodówka** powinien być używany podczas przechowywania chłodniczego; **Poza lodówką** włącz świadomie na czas użytkowania/transportu zgodnie z zasadami konkretnego preparatu. TMP117 może wyświetlić ostrzeżenie „sprawdź tryb”, ale nie zmieni go automatycznie.

---

## 9. Codzienne używanie

### 9.1 Dioda

Kolor mówi przede wszystkim **kierunek problemu**, a liczba mrugnięć jego **priorytet**. Heartbeat jest celowo rzadki, żeby nie marnować baterii.

| Sygnał | Znaczenie |
|---|---|
| zielony ×1 około raz/min | brak aktywnego ostrzeżenia |
| żółty ×1 około raz/min | bateria poniżej 15% |
| żółty ×2 około raz/min | TMP117 wskazuje, że warto sprawdzić ręcznie wybrany tryb |
| niebieski ×1 co ok. 15 s | aktualnie za zimno dla wybranego trybu |
| czerwony ×1 co ok. 15 s | aktualnie za ciepło dla wybranego trybu |
| niebieski ×2 około raz/min | wcześniej wystąpiło przekroczenie dolnego progu |
| czerwony ×2 około raz/min | wcześniej wystąpiło przekroczenie górnego progu |
| niebieski ×3 co ok. 30 s | wykryto temperaturę poniżej `FREEZE_LIMIT` — ryzyko zamarznięcia |
| czerwony ×3 co ok. 30 s | osiągnięto krytyczny próg cieplny ustawiony w firmware |
| magenta ×3 co ok. 15 s | brak wiarygodnego odczytu sondy głównej **lub błąd pamięci trwałej** |

Jeżeli przez kilka minut nie widzisz żadnego błysku, sprawdź aplikację lub podłącz USB. Brak LED może oznaczać rozładowanie, ale sam w sobie nie jest jednoznaczną diagnozą.

### 9.2 Aplikacja i wybór trybu

Po połączeniu aplikacja pokazuje, jaki tryb jest aktywny. **To użytkownik wybiera tryb; firmware nie przełącza go automatycznie.**

- **Lodówka** — progi `FRIDGE_MIN` / `FRIDGE_MAX`.
- **Poza lodówką** — progi `OUT_WARN` / `OUT_MAX` oraz licznik czasu poza lodówką.

Raz w tygodniu otwórz aplikację i naciśnij **Pobierz log**. Zegar czujnika ustawia się przy połączeniu, więc późniejsze zdarzenia mogą dostać datę kalendarzową.

Po utracie Bluetooth aplikacja oznacza odczyt jako **nieaktualny** i nie pozostawia zielonego werdyktu opartego na starej temperaturze.

**Zeruj pamięć** dopiero po świadomym rozpoczęciu monitorowania nowego opakowania. Reset kasuje zatrzaski, liczniki i „blizny” temperatury.

### 9.3 Ładowanie

Gdy pojawi się ostrzeżenie niskiej baterii, naładuj urządzenie przez USB-C. Dla ogniwa 470 mAh pełne ładowanie może trwać **kilka godzin**; nie zakładaj stałego czasu 2–3 h. Zakończenie ładowania oceniaj zgodnie ze wskazaniem układu ładowarki/płytki.

> **Nie ładuj zimnego ani wilgotnego urządzenia.** Wyjmij je z lodówki, pozwól mu osiągnąć temperaturę pokojową i całkowicie wyschnąć z kondensacji. Dopuszczalny zakres temperatury ładowania zależy od zastosowanego ogniwa Li‑Po — sprawdź jego kartę/specyfikację.

### 9.4 Progi

Wszystkie progi są na górze pliku `insulin_temp.ino` i możesz je zmienić. **Nie są uniwersalną specyfikacją wszystkich insulin.** Ustawienia muszą odpowiadać ulotce konkretnego preparatu i sposobowi użycia.

| Stała | Domyślnie | Znaczenie w tym projekcie |
|---|---:|---|
| `FREEZE_LIMIT` | 0 °C | próg zatrzasku „ryzyko zamarznięcia” |
| `FRIDGE_MIN` / `FRIDGE_MAX` | 2 / 8 °C | domyślne okno trybu lodówkowego |
| `OUT_WARN` / `OUT_MAX` | 25 / 30 °C | ostrzeżenie/alarm w trybie poza lodówką |
| `COOK_LIMIT` | 37 °C | projektowy próg krytycznego ciepła |
| `HEAT_DOSE_LIMIT_S` | 3600 s | projektowy skumulowany czas powyżej `OUT_MAX` |

`COOK_LIMIT` i `HEAT_DOSE_LIMIT_S` są nastawami projektu, a nie medycznie uniwersalnymi granicami. Różne preparaty mają różne dopuszczalne temperatury i okresy użytkowania poza lodówką.

### 9.5 Pamięć trwała a zużycie energii

Firmware **nie zapisuje flash przy każdym pomiarze**. To celowy kompromis między trwałością danych, zużyciem pamięci i poborem energii:

- nowy krytyczny zatrzask lub zmiana trybu → zapis natychmiast,
- aktywne grzanie powyżej `OUT_MAX` → checkpoint maksymalnie co około 5 min,
- zwykłe rosnące liczniki → checkpoint maksymalnie co około 30 min,
- pozostałe zmiany → rzadki zapis, maksymalnie co około 6 h, jeśli istnieje coś do zapisania,
- stan jest dziennikowany naprzemiennie w dwóch plikach z CRC, aby ograniczyć skutki przerwania zasilania podczas zapisu.
- jeżeli inicjalizacja lub zapis pamięci trwałej zawiedzie, aplikacja zgłasza **Błąd pamięci trwałej**, a LED miga magenta; taki błąd pozostaje widoczny do restartu, żeby nie zniknął po pojedynczym udanym zapisie.

Oznacza to, że reset/odłączenie zasilania **nie kasuje zatrzasków**, ale przy nagłej utracie zasilania ostatnie kilka minut aktywnego licznika ciepła lub do około 30 minut zwykłego licznika może jeszcze nie być zapisane. To świadomy wybór oszczędzający flash i energię.

Pozostałe ustawienia oszczędzania energii w tej wersji: DS18B20 pracuje w 11 bitach (krótsza konwersja), TMP117 jest odczytywany co 2 min w trybie one-shot, bateria co 15 min, reklama BLE co około 4 s, a zielony LED tylko raz na minutę. Także historyczne alarmy nie migają już co kilka sekund — są przypominane raz na minutę; aktywne przekroczenia i awarie częściej. Zewnętrzna pamięć QSPI XIAO jest po starcie przełączana w deep power-down.

**Czy trwały zapis mocno zwiększa pobór energii? — nie przy tej strategii.** Zapis flash występuje krótko i rzadko; krytyczny zatrzask zapisuje się od razu, ale zwykłe liczniki są checkpointowane okresowo. W codziennej pracy większe znaczenie mają urządzenia pobierające prąd bez przerwy. Dlatego w wariancie długiego czasu pracy szczególnie ważna jest uwaga o stale świecącej diodzie ON na breakoutcie Adafruit TMP117.

Nie próbujemy oszczędzać energii kosztem utraty informacji o alarmie: trwałość krytycznych zatrzasków ma pierwszeństwo przed pojedynczym krótkim zapisem flash.

## 10. Gdy coś nie działa

### Problemy z montażem

| Objaw | Przyczyna |
|---|---|
| Komputer nie widzi portu | Dwuklik przycisku reset, wejście w tryb awaryjny. Sprawdź też, czy kabel USB przesyła dane, a nie tylko ładuje |
| Kompilacja kończy się błędem o `PIN_VBAT` | Wybrana zła płytka albo pakiet **mbed** zamiast zwykłego |
| Kompilacja: `OneWire.h: No such file` | Nie zainstalowana biblioteka OneWire |
| Magenta ×3, brak odczytu insuliny | Sonda nie odpowiada: brak rezystora 4,7 kΩ, zimny lut na `D2`, albo pomylone żyły |
| Odczyt insuliny pokazuje `--,--` | Sonda nie odpowiada albo każdy odczyt nie przechodzi sumy kontrolnej. Przyczyny jak wyżej |
| W logu pojawia się `PROBE_FAIL` | Program ponawia odczyt, ale powtarzające się błędy trzeba sprawdzić: rezystor 4,7 kΩ, luty, przewód, zasilanie i samą sondę |
| Powietrze bez odczytu, sonda działa | Zimny lut na `D4`/`D5`, zamienione miejscami, albo ruszona zwora `ADDR` |
| Nic nie działa po dolutowaniu baterii | Odwrotna polaryzacja. Płytka do wymiany |

### Problemy z aplikacją

| Objaw | Przyczyna |
|---|---|
| Ikona otwiera stronę z paskiem adresu | Zainstalowana zakładka zamiast aplikacji. Usuń i użyj przycisku **Zainstaluj** |
| Brak przycisku **Zainstaluj** | Sprawdź HTTPS, `manifest.json`, service worker, ikony i czy Chrome uznał stronę za instalowalną; interfejs instalacji różni się między wersjami |
| Lista urządzeń pusta przy łączeniu | Chrome nie ma uprawnienia **Urządzenia w pobliżu** |
| Poprawka w kodzie niewidoczna | Nie podniesiony `VERSION` w `sw.js` |
| Aplikacja nie widzi czujnika w lodówce | Normalne, metal ekranuje. Otwórz drzwi |

### Problemy z pomiarem

| Objaw | Przyczyna |
|---|---|
| Codzienny fałszywy alarm „za zimno" | Sonda wsunięta zbyt płytko, mierzy powietrze |
| Odczyt zmienia się bardzo wolno | Sonda owinięta zbyt grubo albo zaklejona |
| Odczyt wyraźnie pływa w stabilnych warunkach | Sprawdź montaż, jakość połączeń, zasilanie i samą sondę; niestabilność nie ma jednej przyczyny |
| Bateria pada w kilka dni | Sprawdź stan ogniwa, częste połączenie BLE, stałą diodę ON na module TMP117, zwarcia/upływy po wilgoci oraz czy zewnętrzna QSPI przechodzi w tryb uśpienia |
| Aplikacja pokazuje „Błąd pamięci trwałej” / magenta ×3 | Nie zeruj historii i nie polegaj na trwałości alarmów. Podłącz USB, zrestartuj urządzenie i sprawdź log/Serial. Jeżeli błąd wraca, przeprogramuj płytkę lub sprawdź system plików; przed dalszym użyciem wykonaj pełny test z rozdziału 8 |

### Komendy awaryjne

Jeśli aplikacja zawiedzie, możesz rozmawiać z czujnikiem przez aplikację **nRF Connect** (Google Play), zakładka Nordic UART Service.

| Komenda | Działanie |
|---|---|
| `S` | Pełny stan, po ludzku |
| `M` | Zmierz teraz, nie czekaj do końca minuty |
| `F` | Ustaw tryb **Lodówka** |
| `O` | Ustaw tryb **Poza lodówką** |
| `D` | Zrzuć log |
| `K` | **K**alibruj w lodzie. Sonda musi być zanurzona |
| `C` | Podaj aktualną poprawkę kalibracji |
| `C-0.24` | Wpisz poprawkę ręcznie. `C0` czyści kalibrację |
| `T1754870400` | Ustaw zegar na podaną liczbę sekund epoki Unix |
| `R` | Zeruj zatrzaski i liczniki |
| `E` | Wymaż log |

---

## 11. Źródła techniczne

Projekt został zweryfikowany względem dokumentacji producentów i specyfikacji protokołu: Seeed XIAO nRF52840, Nordic Semiconductor nRF52840 Product Specification, Analog Devices DS18B20, Texas Instruments TMP117, dokumentacji i schematu Adafruit TMP117 4821 oraz BTHome v2. Przed zmianą wersji płytki, rdzenia Arduino albo modułu czujnika sprawdź aktualne wydania tych dokumentów.

---

## 12. Zastrzeżenie

**To urządzenie domowej roboty, nie wyrób medyczny ani certyfikowany rejestrator temperatury.** Zatrzask znaczy „sonda wykryła przekroczenie ustawionego progu", a nie „insulina na pewno straciła skuteczność". Sonda nie mierzy bezpośrednio cieczy wewnątrz wkładu/fiolki, a odpowiedź produktu na temperaturę zależy od preparatu, czasu i warunków.

Traktuj to jako wczesne ostrzeganie, a nie podstawę decyzji „wyrzucić czy zostawić". Przy realnym przekroczeniu sprawdź ulotkę preparatu albo zapytaj farmaceuty. Tolerancje różnią się między insulinami, a zamarznięcie i przegrzanie mają zupełnie inne konsekwencje.

Nie rezygnuj z dotychczasowych nawyków sprawdzania insuliny dlatego, że masz czujnik. On ma je uzupełniać, nie zastępować.
