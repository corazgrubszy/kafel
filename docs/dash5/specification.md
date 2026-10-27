# DASH/5 — dane, kompozycje i statyczne SVG

Status: **kontrakt DASH/5, implementacja lokalna**, 27 października 2026.
Przygotowany do migracji; ta zmiana nie wdraża aplikacji na CM4.
DASH/5 zastępuje DASH/4 w całości: bez negocjacji starszej wersji, adapterów,
starych rodzajów `text` / `metric` / `metric-meter` ani podwójnego publikowania.

## 1. Cel i podział odpowiedzialności

Usługa publikuje kompletny zestaw dużych kafli, ich stan i dane.
Osobno dostarcza niezmienne szablony JSON i statyczne SVG. Dashboard sprawdza
zestaw, przygotowuje zasoby i atomowo przyjmuje nową wersję. Usługa nie rysuje
bufora pikseli, nie otwiera framebufferów i nie wysyła klatek animacji.

| Właściciel | Odpowiedzialność |
|---|---|
| Usługa | Pobieranie danych, retry, świeżość, opisy, stan i uwaga; wybór szablonu oraz ilustracji |
| DASH/5 | Transport, wersje snapshotów, potwierdzenia, identyfikatory i integralność zasobów |
| Kompozycja UI | Tło, tekst, liczby, pozycje i kompozycja statycznych SVG |
| Dashboard | Zegar, CPU/RAM/eMMC, gesty, kolejność stron, alarmy/offline, cache i planowanie klatek |
| Pogoda | Interpretacja WMO i `is_day`; renderer nie zna tych pojęć |

Na ekranie CM4 240×320 duży obszar 240×240 staje się poziomym pagerem:
strona systemowa, następnie duże kafle usług. Dolne 80 px pozostaje obszarem
CPU/RAM/eMMC, z dotychczasową geometrią i odstępami. Rozmiar 240×240 jest
zewnętrznym obszarem kompozycji, nie dodatkowym kwadratem wewnątrz marginesów.
Usługa nie zmienia nawigacji, pozycji zegara ani układu dolnych metryk.

Pierwszy profil jest celowo ograniczony do kompozycji 240×240. Inny ekran
wymaga jawnie zgodnego profilu; nie ma niejawnego rozciągania tekstu i grafik.

## 2. Transport i ramki

Lokalne `AF_UNIX / SOCK_STREAM`, domyślnie
`/tmp/kafel-<uid>/dashboard.sock`. Ścieżka jest konfigurowalna po obu stronach.
Katalog: 0700; gniazdo: 0600. Dashboard sprawdza UID peer credentials.
Ochrona katalogu, blokada jednej instancji i odmowa zastępowania obcych plików
lub symlinków są obowiązkowe. Jest to transport zaufanych procesów jednego
użytkownika; UID nie uwierzytelnia prawa do konkretnego `service_id`.
Nie otwieramy portu TCP, nie dodajemy HTTP ani TLS do komunikacji lokalnej.

Po połączeniu klient wysyła dokładnie 7 bajtów `DASH/5\n`, następnie `hello`.
Po sprawdzeniu prefiksu i `hello` serwer odpowiada takim samym prefiksem oraz
`ready` albo `rejected`. Błędny prefiks zamyka połączenie bez negocjacji.

Każdy komunikat JSON to:

```text
4 bajty unsigned big-endian długości N | dokładnie N bajtów JSON UTF-8
```

N wynosi 1–65536. JSON nie ma BOM ani kończącego NUL. Transport musi obsługiwać
częściowe odczyty/zapisy oraz kilka ramek w jednym odczycie; jedno `recv()` nie
jest jedną wiadomością. Dodatkowy LF nie stanowi separatora ramek.
Nieznane pola, duplikaty kluczy po dekodowaniu escape'ów, błędne UTF-8,
niepoprawne typy, NaN/Inf i przekroczenie limitu odrzucają wiadomość.
Odrzucony JSON nie zmienia aktywnego zestawu ani heartbeat.
Nazwy, tytuły, komunikaty i tekstowe wartości danych nie zawierają znaków
sterujących ASCII, DEL, NUL ani separatorów wierszy U+2028/U+2029.
Whitespace poza stringami JSON jest dozwolony.
Nazwy, tytuły, komunikaty i tekstowe wartości danych nie zawierają znaków
sterujących ASCII, DEL, NUL ani separatorów wierszy U+2028/U+2029.
Whitespace poza stringami JSON jest dozwolony.
Nazwy, tytuły, komunikaty i tekstowe wartości danych nie zawierają znaków
sterujących ASCII, DEL, NUL ani separatorów wierszy U+2028/U+2029.
Whitespace poza stringami JSON jest dozwolony.

Wyjątek: po ramce `put` następuje dokładnie zadeklarowana liczba **surowych
bajtów zasobu**, bez kolejnej długości, base64 ani JSON escaping. Dopiero po
nich zaczyna się następna ramka JSON. Parser połączenia ma jawny stan
`JSON header → JSON body → resource body → JSON header`.

Klient wykonuje jedną operację naraz: wysyła całą operację i czeka na odpowiedź.
`put` i jego body są jedną operacją. Nie ma pipeliningu ani przeplatania plików.
EOF w środku ramki/body porzuca niepełną operację. Znany, błędny komunikat
może dostać odpowiedź i pozostawić połączenie; utrata framingu lub deadline
zamyka je. Niekompletne body nie jest początkiem kolejnego JSON. Jeśli ramka put ma
niepoprawne metadane (nie można bezpiecznie ustalić długości lub ją odrzucamy),
odbiorca zamyka połączenie po NACK; nie próbuje traktować body jako ramki. Jeśli ramka put ma
niepoprawne metadane (nie można bezpiecznie ustalić długości lub ją odrzucamy),
odbiorca zamyka połączenie po NACK; nie próbuje traktować body jako ramki. Jeśli ramka put ma
niepoprawne metadane (nie można bezpiecznie ustalić długości lub ją odrzucamy),
odbiorca zamyka połączenie po NACK; nie próbuje traktować body jako ramki.

Odczyt/zapis są nieblokujące. Hello musi zakończyć się do 3 s od accept,
także jeśli klient nie wysłał żadnego bajtu. Limit ukończenia rozpoczętej ramki/body: 3 s
według zegara monotonicznego, bez przedłużania pojedynczymi bajtami. Bezczynne,
poprawnie zestawione połączenie nie ma tego deadline'u; obowiązuje TTL usługi.

## 3. Sesja i identyfikatory

[Kompletny hello.json](weather/hello.json):

```json
{"type":"hello","service_id":"kafel-weather","name":"Pogoda","instance":"5e2416f4f2164c75bf13b45170d265ef"}
```

- ID usługi, kafla, stylu, pola i fontu: 1–32 bajty `[a-z0-9._-]`.
- `name` i `title`: 1–48 bajtów zwykłego UTF-8.
- `instance`: 32 małe cyfry hex, nowa losowa wartość po każdym starcie producenta.
- `revision`: liczba całkowita 1–9007199254740991, rosnąca w obrębie instancji.
- Tożsamość kafla: `(service_id, tile.id)`, a nie jego tytuł czy szablon.

Odpowiedź `ready` zawiera `receiver_instance` (32 cyfry hex, nowe przy każdym
starcie dashboardu), `canvas` oraz pełny obiekt `limits` z tabeli w rozdziale 10.
[Pełny przykład ready.json](weather/ready.json) określa również limity głębokości
przez `json_depth: {"transport":16,"template":32}`.
Producent nie polega na obecności cache po ponownym połączeniu.

Jedno aktywne połączenie na `service_id`. Kolejne otrzymuje `service_busy`;
nie przejmuje kafli innego procesu. `hello` nie przyjmuje treści i nie odnawia
TTL. Po rozłączeniu można ponowić `hello` z tą samą instancją i rewizją.
Nowa instancja rozpoczyna numerację od 1; poprzedni zestaw pozostaje aktywny
do przyjęcia pierwszego poprawnego snapshotu nowej instancji.

## 4. Zasoby: adresowanie zawartością

ID zasobu: `sha256:` i 64 małe cyfry hex. Hash obejmuje dokładne bajty pliku,
wraz z whitespace i końcowym LF, przed jakimkolwiek parsowaniem. Nie ma
kanonikalizacji JSON/SVG, nazw plików, katalogów, URL ani dostępu do plików hosta.

Dwa typy MIME:

- `application/vnd.kafel.tile+json` — szablon kompozycji;
- `image/svg+xml` — przygotowany, statyczny SVG.

Ramka `put` ma dokładnie `type`, `id`, `media_type`, `bytes`, np. plik
[put-illustration.json](weather/put-illustration.json). Zaraz po niej nadawca
wysyła `bytes` bajtów z [illustration.svg](weather/illustration.svg).
Odbiorca najpierw sprawdza długość i hash, potem typ oraz profil dokumentu.
Odpowiada `{"type":"stored","id":"sha256:…"}` albo
`{"type":"rejected","operation":"put","code":"invalid_svg"}`.

`put` nigdy nie zmienia widocznego kafla ani TTL. Zasób jest niezmienny.
Ponowne poprawne przesłanie tego samego ID jest idempotentne; odbiorca musi
zużyć i sprawdzić całe zapowiedziane body, nawet jeśli zna ID.
Konflikt typu, długości lub hasha jest błędem, nie nadpisaniem.
Rozłączenie nie publikuje niepełnego zasobu.

Zasoby wskazane przez aktywny snapshot są przypięte do czasu jego zastąpienia.
Pozostałe mogą zostać usunięte z ograniczonego cache LRU, także po poprawnym
`put`. `publish` ponownie sprawdza komplet zależności; klient potrafi obsłużyć
`missing_resources` więcej niż raz. Nie ma trwałego cache na dysku ani wymogu
trzymania wszystkich 40 ikon Pogody na dashboardzie. Usługa przechowuje
własne oryginały i potrafi je ponownie przesłać.

## 5. Publikacja: zawsze pełny snapshot usługi

[Pełny, wysyłalny przykład](weather/publish.json) ma postać:

```json
{
  "type": "publish",
  "revision": 1,
  "tiles": [{
    "id": "weather",
    "title": "Pogoda",
    "state": "ok",
    "attention": false,
    "freshness": "fresh",
    "observed_at": 1793098800,
    "message": "",
    "content": {
      "template": "sha256:15db1ffb4c7f084d6032912306c05d3845a92c9986a1771b612bd79782485451",
      "data": {
        "temperature": 13,
        "apparent_temperature": 11,
        "wind_ms": 5,
        "precipitation_mm": 0,
        "description": "Zachmurzenie",
        "illustration": "sha256:2a4c9b95edf5bddd37c1f8c68efc23917cce3242041d0ba4d9f3b570174f1a8d"
      }
    }
  }]
}
```

Wszystkie pola rekordu kafla są obowiązkowe. `content` jest albo obiektem
`template, data`, albo `null` w stanie braku danych. Wartości `data` są tylko
liczbami lub stringami; bez dowolnych poddrzew JSON, funkcji i wyrażeń.
Wszystkie pola zadeklarowane w szablonie muszą wystąpić dokładnie raz.
Dodatkowe pola danych są błędem. Zmiana ilustracji jest zmianą wartości pola
`illustration`, czyli zwykłego, typowanego odwołania do zasobu.

Poza `type` nadawca nie powtarza `service_id` ani `instance`: połączenie jest
już związane z nimi przez `hello`. Nie ma patchy, indeksowych aktualizacji,
zależności od poprzedniej wartości ani mapowania WMO po stronie odbiorcy.

Pominięty kafel znika. `tiles: []` usuwa wszystkie kafle usługi i ich przypięcia
zasobów; sama działająca usługa może nadal wysyłać heartbeat. Zduplikowane ID,
brak miejsca lub błąd jednego kafla odrzuca cały snapshot.

Odbiorca sprawdza schemat, limity rejestru, zależności, geometrię, typy danych,
glyphy i budżet przygotowania. Następnie przygotowuje kompletną prezentację
poza ścieżką wyświetlania. Dopiero udane zatwierdzenie zmienia cały zestaw,
aktualną rewizję oraz czas heartbeat. Klatka nie miesza starych napisów z nową
ilustracją. Awaria przygotowania zostawia poprzedni zestaw i jego zegary.
Odpowiedź `accepted` oznacza przyjęcie gotowego modelu do wyświetlenia,
nie potwierdzenie fizycznego skanowania pikseli przez LCD.

Brakujące zależności zwracamy jako `missing_resources` z listą hashy.
Jeśli brakuje samego szablonu, kolejna próba po jego przesłaniu może dopiero
wykryć brak jego SVG. Klient obsługuje taki sam cykl w obu przypadkach.
Po przesłaniu brakujących zasobów ponawia **ten sam snapshot z tą samą rewizją**.

Rewizja mniejsza od ostatniej przyjętej: `stale_revision`. Równa jest poprawna
wyłącznie przy identycznych bajtach JSON ostatniego przyjętego `publish`;
wtedy ponawiamy ACK i odnawiamy TTL bez przebudowy prezentacji. Ta sama rewizja
z inną zawartością: `revision_conflict`. Zmiana samych whitespace wymaga
nowej rewizji. Producent przechowuje ostatnią zakodowaną wiadomość do retry.
Żadna odrzucona rewizja nie zużywa numeru.

## 6. Szablon `kafel.tile/1`

[Kompletny template.json](weather/template.json) zachowuje zatwierdzoną
kompozycję Pogody: IBM Plex Sans, Rosé Pine Moon, dużą temperaturę,
odczuwalną, wiatr i opady, bez linii rozdzielających. Poszerza wcześniejsze
studium o jawne zasoby i politykę przepełnienia tekstu. To schemat obsługiwany przez kompilator dużych kafli; wcześniejszy parser
studium UI jest jego wewnętrznym modelem kompozycji, nie protokołem usług.

Wymagane sekcje: `schema`, `canvas`, `fonts`, `styles`, `backdrop`, `fields`,
`images`, `texts`, `metrics`. Tablice mogą być puste poza `fonts`; `styles` jest niepustą mapą.
`fields` może być puste w całkowicie statycznym szablonie. Brak sekcji nie
włącza domyślnego zachowania. Nieznana sekcja jest błędem.

Szablon ma stałą geometrię. Współrzędne należą do szablonu, nie do wiadomości
z pomiarami. Nie dodajemy drzewa zależności, CSS, flow layout ani instrukcji
rysowania sterowanych dowolnymi danymi. Układ zależny od danych usługa może
wyrazić przez wybór innego niezmiennego szablonu.

| Sekcja / element | Kontrakt |
|---|---|
| `canvas` | Dokładnie `[240,240]` w pierwszym profilu |
| `fonts` | Lokalne aliasy `id`, zaufany `face` i jawny `repertoire`; bez pliku/URL fontu |
| `styles` | Mapa ID → `font, size, color`; opcjonalny `tracking` |
| `backdrop` | Obecne parametry tła: baza, glow, ukośne pasy, promień narożnika |
| `fields` | Unikalne nazwy i `format`: `text`, `integer`, `general`, `svg` |
| `images` | `at`, `size`, `clip`, `source`, `fit: "contain"` |
| `texts` | `at`, `clip`, `style`, `content`, `overflow: "ellipsis"`, `"clip"` lub `"marquee"` |
| `metrics` | `at`, `clip`, `sizes`, `fit_width`, `content`, `unit`, `unit_style`, `unit_y`, `unit_gap` |

Fonty hosta: `ibm-plex-sans/regular` i `ibm-plex-sans/bold`, dostarczane
z dashboardem wraz z licencją. Nieznany `face` odrzuca snapshot; nie ma cichej
zamiany kroju. Repertoire to zbiór znaków Unicode (powtórzenia są deduplikowane); przygotowanie
obejmuje także cyfrę stopnia, minus, jednostki i glyph `…` potrzebny ellipsis. Każdy `repertoire` musi go
zawierać, jeśli dany font jest używany przez element z ellipsis.
Wszystkie stałe i dynamiczne napisy muszą używać zadeklarowanych glyphów.
Brak glyphu odrzuca aktualizację; nie wyświetlamy po cichu znaku zapytania.

`content` jest dosłownym tekstem albo obiektem `field, format` z opcjonalnymi
`prefix, suffix`. Wiązanie jest bezpośrednie: żadnych ścieżek JSON, interpolacji
kodem, konwersji jednostek i formuł. `source` obrazu jest obiektem `field`;
pole musi mieć format `svg`. Nie jest tekstem nazwy ikony ani ścieżką pliku.
Kolor to wyłącznie `#RRGGBB`; przezroczystość ilustracji należy do SVG.

Dla pól liczbowych wymagane skończone `min, max`. `integer` przyjmuje również
ułamkowy pomiar i służy prezentacji zaokrąglonej do najbliższej liczby całkowitej,
połówki od zera. `general` pokazuje najwyżej jedną cyfrę po kropce, bez `.0`;
bez notacji wykładniczej, locale i ujemnego zera. `prefix/suffix` nie zmieniają
typu ani wartości pomiaru. Dla `text` wymagane `max_bytes`; dla `svg` brak tych
ograniczników, obowiązuje ID zasobu i jego typ MIME.

Wymiar fontu jest w jednostkach używanych przez obecny `TextFont` (pikselowa
skala stb TrueType), nie gwarantowaną wysokością widocznej litery. Rozmiary,
pozycje i wzorcowy wygląd Pogody pochodzą z zatwierdzonego renderera.
Tekst jest pojedynczym wierszem. `ellipsis` wybiera najdłuższy prefiks Unicode,
który mieści się razem z `…`; bez dzielenia UTF-8. `clip` jest świadomym,
prostokątnym przycięciem. Jawne `marquee` przewija tylko tekst wystający poza prawą
krawędź `clip`: pauza 1200 ms → ruch 24 px/s → pauza 1200 ms → powrót.
Krótki tekst pozostaje statyczny. Przygotowanie tworzy maskę całego napisu;
UI przesuwa ją bez ponownej rasteryzacji ani alokacji. Każdy wiersz ma własny
zakres ruchu. Czas zaczyna się od nowa po otwarciu strony lub zmianie jej zawartości.
Ukryte strony i pauzy nie wymuszają rysowania; `poll` czeka na pierwszą zmianę piksela.
Metryka wybiera pierwszy mieszczący się styl z `sizes`; jeśli żaden nie mieści
liczby i jednostki, odrzuca aktualizację. Nie obcina wyniku pomiaru.

Kolejność malowania: tło → obrazy w kolejności tablicy → teksty → metryki →
przewijane warstwy tekstu → niezależne oznaczenia dashboardu. `clip` ogranicza dany element i zawsze jest
przecinany z granicami canvas. SVG zachowuje aspect ratio i przezroczystość.
Całość respektuje zaokrąglenie krawędzi kafla. Obraz i przewijany tekst są składane
przed wspólną maską panelu, z marginesem 4 px. Efektywny promień jest ograniczony
do połowy panelu 232×232. Brak dowolnego z-indexu.
Pola danych nie mogą modyfikować rozmiarów, pozycji, fontów, kolorów ani shaderów.
Zmiana kolorystyki grafiki oznacza inny SVG lub szablon.

## 7. Profil SVG

Statyczny, samodzielny dokument zgodny z ograniczonym profilem NanoSVG:
`svg, g, defs, path, rect, circle, ellipse, line, polyline, polygon,
linearGradient, radialGradient, stop`. Dopuszczamy pełny, obsługiwany zestaw
poleceń ścieżek, transformacje NanoSVG, fill/stroke, opacity i gradienty.
SVG ma jawne, dodatnie wymiary albo poprawny `viewBox` pozwalający je wyznaczyć.
Profil dopuszcza identyfikatory, przestrzeń nazw SVG, geometrię odpowiednich
prymitywów, `d`, `points`, `transform`, fill/stroke i ich opacity, fill-rule,
stroke-width/linecap/linejoin/miterlimit oraz parametry i stopnie gradientów.
Nie obejmuje pozostałych rozszerzeń SVG tylko dlatego, że XML daje się odczytać;
nieznany atrybut o znaczeniu prezentacyjnym jest błędem. Komentarze są pomijane.

Odrzucamy: maski, clipping SVG, filtry, `use`, `image`, `text`, CSS/style,
script, animacje, event handlers, DOCTYPE, encje i wszystkie referencje
zewnętrzne. Lokalne `url(#id)` mogą wskazywać wyłącznie poprawne gradienty;
cykle i nierozwiązane referencje są błędem. Nie ma HTTP ani odczytu plików
w trakcie parsowania. Nieobsługiwana cecha odrzuca zasób zamiast cicho znikać.
`clip` elementu kompozycji jest odrębny od nieobsługiwanego `clipPath` SVG.

Meteocons Flat przechodzą obecną konwersję offline: nieprzezroczyste maski
alfa zostają przecięte do zwykłych ścieżek. Dashboard dostaje gotowy dokument,
bez zależności od Python, PathOps ani nazw Meteocons. Informacje licencyjne
pozostają w dystrybucji usługi. Hash dotyczy przygotowanej wersji SVG.

## 8. Stan, uwaga i świeżość

| Sytuacja | Rekord i zachowanie |
|---|---|
| Poprawne dane | `state: ok`, `freshness: fresh`, `content` wymagane |
| Nieaktualne, użyteczne dane | `state: warning`, `freshness: stale`, ostatni poprawny `content`, niepuste `message` |
| Brak użytecznych danych | `state: error`, `freshness: missing`, `content: null`, niepuste `message` |
| Inne ostrzeżenie | `state: warning`, `freshness: fresh`, wymagany `content` i `message` |
| Uwaga domenowa | `attention: true`, niezależnie od pozostałych pól, np. odbiór dziś/jutro |
| Brak heartbeat | `offline` wyliczony przez dashboard; nie jest wartością wysyłaną przez usługę |

To kompletny zbiór dozwolonych kombinacji `state/freshness/content`.
`message` ma 0–96 bajtów UTF-8; dla `ok` musi być puste. `observed_at` to
całkowity Unix UTC 0–253402300799 albo `null` przy `missing`. Jest metadanymi
czasu źródła, nie zegarem heartbeat. Ocena aktualności należy do producenta;
dashboard nie odgaduje, jak często powinna zmieniać się pogoda czy harmonogram.

`attention` pozostaje metadanymi snapshotu; bieżąca kompozycja nie dodaje gwiazdek.
Warning/error/offline są sygnalizowane globalnie przez stan OK / UWAGA / BŁĄD / BRAK.
Usługa nie może ukryć oznaczenia awarii swoim szablonem.
Przy warning dashboard nakłada kontrastowe `UWAGA` na
prawy górny obszar `[160,8,72,24]`.
Projektant szablonu nie umieszcza tam krytycznej wartości. Error/offline
zastępują kompozycję standardowym panelem dashboardu: tytuł, stan i krótki opis,
a nie starą temperaturą udającą aktualny pomiar. To zachowanie jest niezależne
od obecności cache i poprawności własnej grafiki usługi.

Błędna aktualizacja protokołu nie jest tym samym co opublikowany `error`.
Zostawia poprzedni poprawny zestaw, zapisuje ograniczoną diagnostykę i nie
odnawia TTL. Gdy zabraknie poprawnego heartbeat, pokaże się offline.

## 9. Heartbeat, reconnect i nawigacja

Zalecany heartbeat: 5 s. Klient z zaakceptowanym zestawem wysyła
`{"type":"ping","revision":1}`. Dashboard akceptuje tylko rewizję aktywną
w tej samej instancji usługi i odpowiada
`{"type":"accepted","operation":"ping","revision":1}`.
Sam ping nie ma danych i nie odrysowuje ekranu. Dla nowego odbiornika bez
przyjętego zestawu zwracane jest `no_snapshot`; potrzebny jest pełny publish.
Również przyjęty publish lub identyczne retry odnawia heartbeat.

Offline następuje przy `elapsed >= 15 s` od ostatniego przyjętego publish/ping,
według monotonicznego zegara odbiorcy. Częściowe ramki, `hello`, upload i NACK
nie przedłużają dostępności. Po disconnect TTL nadal biegnie; znane kafle
pozostają w rejestrze. Wpis offline zajmuje miejsce do wycofania kafli przez
właściciela (`publish` z pustą tablicą) lub restartu dashboardu; brak cichego
usuwania awarii po upływie czasu.

Po reconnect producent wykonuje `hello`, próbuje publish i uzupełnia wskazane
braki. Retry po problemach transportu: 1, 2, 5 s, potem maksymalnie 5 s;
nie jest to harmonogram retry Open-Meteo. Po `limit_exceeded` lub błędnym
szablonie nie wysyła co klatkę tej samej niepoprawnej wiadomości. Przy dłuższym
przygotowaniu nowej wersji nadal publikuje heartbeat ostatniej przyjętej.
Przed operacją przygotowania nowego zestawu może odnowić stary pingiem.

Kolejność usług: pierwsze udane przyjęcie; kafli: tablica `tiles` w snapshotcie.
Aktualizacja treści zachowuje aktywne ID i gest. Zmiana samego SVG lub szablonu
nie zmienia tożsamości. Usunięcie widocznego ID wybiera najbliższy pozostały
indeks; gdy brak usług, zostaje strona systemowa. Zmiana struktury kończy gest
przed wyrównaniem. Jeden gest dużych stron kończy się najwyżej na sąsiedniej
stronie; uwaga, awaria i offline nie zmieniają kolejności nawigacji.
Nie zmieniamy strony podczas czytania ani w czasie gestu.

## 10. Limity pierwszego profilu

KiB = 1024 B, MiB = 1048576 B. To limity przyjęcia, nie obietnica stałego RSS.
Klucze z pierwszej kolumny są nazwami w `ready.limits`. Dashboard nie może ich
po cichu obniżyć względem tego profilu; niewystarczające zasoby dają NACK.

| Klucz | Wartość |
|---|---:|
| `services` | 7 aktywnych/zarejestrowanych usług |
| `tiles_total` | 13 kafli usług łącznie |
| `tiles_per_service` | 13 |
| `json_bytes` | 65536 B na ramkę, łącznie z publish |
| `json_depth` | 16 dla transportu, 32 dla szablonu |
| `template_bytes` | 32768 B |
| `svg_bytes` | 65536 B |
| `resources_per_service` | 64, aktywne i staging razem, deduplikacja po ID |
| `resource_bytes_per_service` | 1048576 B surowych zasobów, aktywne i staging razem |
| `resource_bytes_total` | 4194304 B surowych zasobów łącznie |
| `fields_per_tile` | 32 |
| `text_bytes` | 128 B maksymalnie; mniejszy limit z deklaracji pola jest wiążący |
| `fonts_per_template` | 4 aliasy dwóch dopuszczonych krojów |
| `glyphs_per_font` | 512 różnych znaków; repertoire maksymalnie 2048 B |
| `styles_per_template` | 16, maksymalny rozmiar fontu 128 px |
| `font_sizes_per_template` | 8 różnych par face/size |
| `elements_per_template` | 64 łącznie: images + texts + metrics |
| `images_per_template` | 4 |
| `svg_depth` | 32 |
| `svg_shapes` | 1024 |
| `svg_points` | 65536 |
| `svg_intrinsic_extent` | 4096 na oś, współrzędne skończone, bezwzględnie do 10000000 |
| `glyph_cache_bytes` | 8388608 B łącznie na przygotowane bitmapy glyphów |
| `rgba_scratch_bytes` | 460800 B, dwie robocze powierzchnie RGBA 240×240 |
| `rgb_card_cache_bytes` | 2246400 B, najwyżej 13 niezmiennych obrazów RGB 240×240 |
| `corner_cache_bytes` | 174928 B, maski 13 kafli; efektywny promień maksymalnie 116 px dla panelu 232×232 |
| `marquee_coverage_bytes` | 262144 B na kafel; maska nie szersza niż 4096 px, uwzględniana w budżecie kandydata |
| `candidate_bytes` | 8388608 B na jedną przygotowywaną transakcję, poza zasobami już ujętymi wyżej |
| `preparing_snapshots` | 1 naraz |
| `pending_connections` | 2 niezidentyfikowane połączenia, poza 7 sesjami usług |
| `operation_timeout_ms` | 3000 dla rozpoczętej ramki/body oraz oczekiwania klienta na odpowiedź |
| `prepare_timeout_ms` | 2000 — miękki budżet przygotowania kandydackiego snapshotu |
| `heartbeat_ms` | 5000 |
| `offline_ms` | 15000 |
| `animation_fps` | 25, animacje dashboardu |

Surowe szablony i SVG aktywnych kafli pozostają przypięte. Kompilator przygotowuje
niezmienne RGB przy aktualizacji danych, poza wątkiem UI; nie rasteryzujemy
ponownie grafik w kolejnych klatkach. Pamięć aktywnych RGB jest ograniczona
limitem 13 kafli: 2,15 MiB. To korekta pierwotnego projektu trzech RGBA,
wybrana aby każdy gest mógł natychmiast wyświetlić sąsiednią stronę.

Jeden asynchroniczny slot kompilacji nie blokuje I/O heartbeat. Nie ma
nieograniczonej kolejki prac. Staging nie omija limitów zasobów. Przygotowanie
rezerwuje 2 MiB na ograniczone dokumenty, pliki fontów, RGBA i scratch;
pozostały budżet kandydata pomniejszony o jego RGB jest limitem faktycznych
masek glyphów sprawdzanym przed każdą alokacją maski. Fonty i bitmapy SVG
mogą pozostać w ograniczonym cache przygotowania. LRU fontów i bitmap SVG
mieści do 32 wpisów i 8 MiB buforów; łączny limit masek znaków nadal obowiązuje.
Dla przepełnionych pól `marquee` zostają
wyłącznie ograniczone maski pokrycia; przy 13 kaflach zajmą najwyżej 3,25 MiB.
Budżet kandydata odejmuje RGB, narożniki i maski już przygotowanych stron.
Przekroczenie limitu lub terminu przygotowania porzuca kandydata, nie aktywny
widok. `prepare_timeout_ms` jest miękkim budżetem: czas od rozpoczęcia
przygotowania snapshotu sprawdzamy po każdym kompletnie przygotowanym kaflu,
także pobranym z cache. Dokładnie 2000 ms mieści się w budżecie; przekroczenie
odrzuca wynik jako `prepare_timeout`. Nie przerywamy rasteryzacji SVG ani fontów
w połowie, więc pojedynczy kafel może pracować dłużej niż ten budżet.
Anulowanie publikacji lub timeout połączenia również nie zatrzymuje fizycznego
workera: miejsce przygotowania pozostaje zajęte do odebrania jego wyniku.
To reguła przyjęcia wyniku, nie gwarancja maksymalnego czasu pracy czy shutdownu.
Przy zajętym miejscu przygotowania serwer może zwrócić `busy`. Implementacja
Kafla odracza odpowiedź w granicach deadline operacji: po jednym żądaniu na
połączenie, przy jednym aktywnym zadaniu. Zakończenie zadania wybudza I/O;
producent nie musi cyklicznie ponawiać całej publikacji. Nie powstaje kolejka
nieograniczonej długości. Pingi innych usług są nadal obsługiwane.
Współrzędne `at` są całkowite 0–239, rozmiary 1–240, clip jest niepustym
prostokątem wewnątrz canvas. Obraz może wystawać poza canvas (np. x=106,
szerokość=160 w Pogodzie), ale jest zawsze przycięty. Współrzędne i promienie
tła są skończone 0–240; corner_radius 0–120; kanały i gain 0–255.
Pasy mają period 1–480 i width 0–period; tracking fontu −8…8 px.
Każda lista `sizes` ma 1–8 pozycji, w malejącej skali tego samego kroju.
Prefix i suffix mają po najwyżej 48 B, stały tekst najwyżej 128 B.
Współrzędne `at` są całkowite 0–239, rozmiary 1–240, clip jest niepustym
prostokątem wewnątrz canvas. Obraz może wystawać poza canvas (np. x=106,
szerokość=160 w Pogodzie), ale jest zawsze przycięty. Współrzędne i promienie
tła są skończone 0–240; corner_radius 0–120; kanały i gain 0–255.
Pasy mają period 1–480 i width 0–period; tracking fontu −8…8 px.
Każda lista `sizes` ma 1–8 pozycji, w malejącej skali tego samego kroju.
Prefix i suffix mają po najwyżej 48 B, stały tekst najwyżej 128 B.
Współrzędne `at` są całkowite 0–239, rozmiary 1–240, clip jest niepustym
prostokątem wewnątrz canvas. Obraz może wystawać poza canvas (np. x=106,
szerokość=160 w Pogodzie), ale jest zawsze przycięty. Współrzędne i promienie
tła są skończone 0–240; corner_radius 0–120; kanały i gain 0–255.
Pasy mają period 1–480 i width 0–period; tracking fontu −8…8 px.
Każda lista `sizes` ma 1–8 pozycji, w malejącej skali tego samego kroju.
Prefix i suffix mają po najwyżej 48 B, stały tekst najwyżej 128 B.

Samo przekazanie danych nie może wybudzać niewidocznego kafla ani odrysowywać
identycznego obrazu. SVG jest statyczne: raster przy zmianie zasobu/rozmiaru,
kompozycja przy zmianie widocznych danych. Ruch pagera działa do 25 fps;
po zakończeniu animacji i przy niezmiennych danych pętla czeka w poll.

## 11. Potwierdzenia i błędy

Po `hello` każda operacja ma jedną odpowiedź: `stored` dla put, `accepted`
dla publish/ping albo `rejected`. Publish/ping powtarzają `revision`;
put powtarza ID przy sukcesie. Ponieważ nie ma wielu operacji w locie,
nie dodajemy osobnego `request_id`. Timeout oznacza nieznany wynik;
ponowne publish tej samej rewizji jest bezpieczne według reguł idempotencji.

`rejected` zawiera `operation`, `code`; dla poprawnie odczytanej rewizji także
`revision`. `missing_resources` zawiera dodatkowo `resources` (1–64 ID).
Pozostałe kody: `invalid_message`, `unsupported_schema`, `invalid_template`,
`invalid_svg`, `invalid_data`, `hash_mismatch`, `resource_conflict`,
`limit_exceeded`, `stale_revision`, `revision_conflict`, `no_snapshot`,
`service_busy`, `busy`, `prepare_timeout`. Dla `hello` operation wynosi `hello`.
Nie przesyłamy stack trace, treści źródeł ani ścieżek hosta.
Odbiorca ogranicza diagnostykę powtarzalnych błędów; producent nie loguje całych
komunikatów ani zasobów przy każdym niepowodzeniu.

## 12. Konkretna sekwencja Pogody

1. Połączenie, prefiks, [hello](weather/hello.json), odpowiedź ready.
2. [Publish rewizji 1](weather/publish.json). Zimny dashboard odpowiada
   [missing_resources](weather/missing.json).
3. [Put szablonu](weather/put-template.json), surowe bajty
   [template.json](weather/template.json), odpowiedź stored.
4. [Put ilustracji](weather/put-illustration.json), surowe bajty
   [illustration.svg](weather/illustration.svg), odpowiedź stored.
5. Ponownie identyczny publish rewizji 1, odpowiedź
   [accepted](weather/accepted.json). Dopiero teraz nowy kafel jest aktywny.
6. Co 5 s ping rewizji 1. Bez ponownego JSON szablonu i SVG.
7. Nowy pomiar: [rewizja 2](weather/publish-update.json). Pełne `tiles/data`,
   te same dwa hashe, żadnego uploadu. Potem ping rewizji 2.
8. Zmiana WMO lub `is_day`: Pogoda wybiera kolejny gotowy SVG, oblicza jego ID,
   publikuje nową rewizję; jeśli potrzeba, uzupełnia tylko ten zasób i ponawia
   snapshot. Szablon i pozostałe dane nie wymagają nowej wersji zasobu.
9. Brak użytecznych danych: [rewizja 3](weather/publish-error.json) pokazuje
   standardowy panel „Brak danych”. Przy użytecznym, nieaktualnym cache zamiast
   tego należy opublikować warning/stale z zachowanym content.

[resources.json](weather/resources.json) zawiera rzeczywiste długości i hashe
obu plików. Wszystkie przykłady to dane syntetyczne, bez lokalnego IP,
harmonogramu odbioru, tokenów i konfiguracji prywatnego urządzenia.

## 13. Kryteria odbioru i testy implementacji

- Fragmentacja strumienia w każdym bajcie nagłówka, JSON i SVG; kilka ramek
  w jednym odczycie; EOF/timeout w każdym stanie; graniczne długości.
- Duplikaty kluczy, błędny Unicode, nieznane wersje/sekcje/pola, niewłaściwe
  typy, geometryczne i pamięciowe limity; żadnej częściowej podmiany.
- Brakujące zasoby, poprawne ponowienie, konflikt hasha, niedokończony upload,
  usunięcie cache pomiędzy put a publish, nowa instancja odbiorcy i producenta.
- Utracone ACK, identyczne retry, konflikt/mniejsza rewizja i odnowienie TTL
  wyłącznie po przyjęciu właściwej operacji.
- Osobne stany kafli, warning ze starym pomiarem, error bez starej liczby,
  offline po 15 s, odzyskanie oraz nieukrywalne oznaczenia awarii.
- Zgodność renderu zatwierdzonej Pogody, dzień/noc, wspólna podmiana tekstu
  i SVG, długie polskie opisy, ujemna temperatura i jednostka bez obcięcia.
- CPU/RAM/eMMC bez regresji; stabilna tożsamość i nawigacja pagera.
- Zero odrysowań w spoczynku, brak pracy ukrytych stron, ograniczony cache,
  brak parsowania i przygotowania fontów w klatce gestu.

Przy zmianie protokołu aktualizujemy dashboard, Pogodę i Śmieci w jednej
kontrolowanej migracji wdrożenia, zatrzymując starych producentów przed startem
nowego odbiornika. Nie obiecujemy atomowego, jednoczesnego startu trzech
procesów; strona systemowa działa w okresie rejestracji usług. Rollback musi
przywrócić zgodny zestaw wszystkich trzech binarek i konfigurację gniazda.
Nie wprowadzamy przejściowego adaptera DASH/4.

### Kontrola geometrii i limitów po przygotowaniu

Walidacja SVG uwzględnia skalę docelowego rastra, stroke/miter i parametry dash,
przed wejściem do rasteryzatora. Efektywne współrzędne z rozszerzeniem obrysu
mieszczą się w ±262144 px; stroke ma najwyżej 4096 px po skalowaniu. NanoSVG
ma opisaną lokalną poprawkę pomijającą nieużywaną konwersję przyrostu prawie
poziomej krawędzi. Ścieżki i wyniki poprawnych ilustracji pozostają bez zmian.

Publikacja liczy do limitu usługi unię aktywnych, staged i kandydackich zasobów,
również znalezionych w globalnym cache innych usług. Wspólne ID liczy się raz;
przekroczenie liczby lub bajtów odrzuca publikację przed przygotowaniem, bez
zmiany poprzedniego snapshotu i heartbeat.
