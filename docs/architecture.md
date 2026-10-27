# Architektura Kafel

## Granice odpowiedzialności

| Warstwa | Odpowiedzialność | Nie odpowiada za |
|---|---|---|
| `DashboardView` | Widok systemowy, duży pager, dolne CPU/RAM/eMMC, gesty i idle | JSON, zasoby, pobieranie pogody |
| `ui::prepare_card` | Przygotowanie kompozycji 240×240, typografii i SVG → niezmienny `CardFrame` RGB | Transport, Linux, WMO i frakcje |
| `ClockPainter` | Systemowy zegar, hostname, data i przesuwanie cyfr | Odbiór usług i odczyt urządzeń |
| `ServiceFeed` | Odbiór, walidacja zasobów, asynchroniczne przygotowanie i wymiana snapshotu z UI | Pobieranie danych źródłowych |
| `dash5::Receiver` | Pełne snapshoty, zasoby SHA-256, rewizje, atomic commit i TTL | Rysowanie i semantyka danych usług |
| `dash5::Publisher/Server` | Framing Unix stream, UID, potwierdzenia, deadline i reconnect | Reguły pogodowe i kalendarz |
| Modele producentów | Pobieranie/odczyt, walidacja, cache, retry lub kalendarz | Sprzęt LCD i framebuffer |
| `packet` producenta | Dane i wskazania szablonu/SVG zgodne z DASH/5 | Rasteryzacja i zarządzanie UI |
| `HttpClient` | GET, TLS, deadline i limit odpowiedzi | JSON, retry i reguły biznesowe |
| Adaptery Linux | Framebuffer, evdev i system probe | Układ oraz polityka usług |

`libs/json` udostępnia ograniczony, ścisły parser z odrzucaniem duplikatów,
błędnego UTF-8 i nadmiernej głębokości podczas jednego budowania DOM.
Nie ma osobnego przebiegu sprawdzającego duplikaty. Protokół, kompozycje i fonty
używają wspólnego dekodera UTF-8. Parser JSON współdzielą protokół i kompozycje;
renderer nie zależy od protokołu ani aplikacji Pogody.

## Od snapshotu do pikseli

1. Producent ma niezmienne zasoby osadzone w binarce i bieżący model domenowy.
   Zmiana modelu daje pełny publish, brak zmiany — mały ping co 5 s.
2. Receiver sprawdza komunikat i zależności; prosi o brakujące hashe. Upload
   sprawdza rozmiar, SHA-256 i profil dokumentu, ale nie zmienia widoku ani TTL.
3. Serwer zachowuje sparsowane żądanie do końca operacji. Jeden kandydat
   jest przygotowywany poza wątkiem I/O, a pipe sygnalizuje gotowy wynik.
   Inne połączenia nadal obsługują heartbeat. Każde może zatrzymać jedno żądanie
   do swojego deadline; producent nie ponawia go podczas przygotowania.
   Rozłączenie właściciela oznacza publikację jako anulowaną. Slot pozostaje
   zajęty do odebrania końca rzeczywistej pracy; nie uruchamiamy drugiego workera.
   Ten stan należy wyłącznie do `Receiver`. `AsyncCompiler` nie ma kolejki ani
   flag anulowania, a serwer Unix nie utrzymuje drugiej rezerwacji.
   `Receiver::operation` zwraca gotową ramkę odpowiedzi albo `nullopt`,
   gdy kandydat czeka na grafikę. Po sygnale workera `finish_preparation` zatwierdza zachowanego
   kandydata. Nie wykonuje ponownie walidacji żądania; oczekiwanie nie jest
   wyjątkiem ani odpowiedzią JSON. Anulowane zadanie jest odbierane przed
   uruchomieniem kolejnego, bez blokowania wątku I/O na rasteryzacji.
4. `CardTemplate` kompiluje geometrię, style i powiązania podczas walidacji
   zasobu. `CompositionDocument` odczytuje JSON bezpośrednio do jednego modelu:
   tekst ma własny `Overflow`, obraz ma geometrię i nazwę pola SVG, font — nazwę
   kroju. Nie ma pośredniego schematu JSON ani równoległych tablic trybów tekstu.
   Przygotowanie kafla ma osobne etapy formatowania, fontów, tekstu i obrazów.
   Najpierw powstają własne napisy, dopiero potem widoki wymagane przez fonty.
   Wynik przygotowania tekstu jest właścicielem napisów i masek, bez widoków
   wskazujących na własne pola. Widoki powstają lokalnie na czas malowania.
   Cache przechowuje i usuwa zasoby; zwykłe funkcje przygotowują tekst i obrazy.
   Walidator zwraca odczytane powiązania SVG razem z wynikiem walidacji zasobu;
   odbiornik nie parsuje ponownie szablonu wyłącznie po metadane zależności.
   Ograniczony cache (26 szablonów) przekazuje ten sam wynik do
   przygotowania kafla. Gotowe szablony są niezmienne; ich indeks ma osobną
   blokadę poza wątkiem UI.
   Zmiana danych tworzy klucz z końcowych napisów i ilustracji. Przy identycznym
   kluczu pozostaje poprzedni `CardFrame`, bez przygotowania fontów i SVG.
   Dla widocznej zmiany wspólny LRU wykorzystuje gotowe fonty i bitmapy SVG
   (do 32 wpisów, 8 MiB buforów). Nowe fonty przestrzegają również limitu masek.
5. Przed zatwierdzeniem powstaje kompletna ramka ACK oraz potrzebne miejsce
   w kontenerze. Potem zostają tylko bezalokacyjne przeniesienia, czyszczenie
   stagingu i zmiana generacji. Nie zwracamy wartości picojson po mutacji modelu:
   biblioteka kopiuje ją z alokacją nawet przy `std::move`. Błąd alokacji przed
   zatwierdzeniem zachowuje poprzedni model, rewizję, TTL i prezentację.
   EOF i deadline są sprawdzane przed obsługą zakończenia workera.
   Udane przygotowanie zastępuje cały zestaw jednej usługi. Aktywne zasoby są
   przypięte także po rozłączeniu producenta. Nieprzypięty staging jest zwalniany.
6. `ServiceFeed` przekazuje kompletny, współdzielony snapshot przez krótką
   atomową wymianę wskaźnika i wybudza UI przez pipe. Przekazanie snapshotu
   poprzedza wysłanie ACK; producent po potwierdzeniu nie widzi starego modelu.
   Wynik to konkretny, niezmienny `PreparedTiles`, bez pustej bazy i rzutowania
   RTTI. Receiver sprawdza liczbę ramek i zgodność ich obecności ze stanem kafli.
   Wątek UI nie parsuje JSON,
   nie przygotowuje fontów i nie rasteryzuje SVG.

Cache słabych wskaźników ma limit 26 wpisów oraz usuwanie nieaktywnych/LRU.
Identyczne dane wykorzystują istniejącą klatkę. Różne pomiary, które po
formatowaniu dają te same piksele, również współdzielą obraz; nie powodują
zbędnego zapisu do framebufferu. Aktualizacja warning/stale bez zmiany treści
nie kompiluje ponownie grafiki.

## UI i harmonogram

Górny pager korzysta z istniejącego `RibbonController`: każda strona ma trzy
jednostki i zerową przerwę, co daje krok 240 px. Nie dodano drugiego silnika
gestów. Zegar pozostaje pierwszą stroną. `TilePainter` nadal rysuje dolne
CPU/RAM/eMMC z zatwierdzoną typografią i słupkami. `ClockPainter` jest częścią
aplikacji dashboardu. `DashboardTheme`, paleta i `LargeTileLayout` również
należą do aplikacji. Biblioteka ma tylko ogólne style tekstu, panelu i metryki;
nie zna hostname ani formatu HH:MM.
`ui::Surface` przechowuje RGB888, a `Rgb565Surface` przygotowuje piksele dla
framebufferu. Kafel DASH ma rozmiar 240×240; jego wcięcie panelu (4 px) wybiera
wywołujący dashboard, a nie ogólny renderer.

Pętla główna obsługuje evdev, zmiany usług, system probe i zegar. Rysuje tylko
przy zmianie widocznych danych albo podczas animacji, do 25 fps. Poza tym czeka
w poll. Ukryta strona zegara i ukryte kafle nie generują klatek. Gesty i malowanie
cache nie alokują na stercie. Natywne testy obejmują zero alokacji w całym geście.

Tożsamość to `(service_id, tile_id)`. Zmiana samej treści zachowuje stronę.
Zmiana kolejności kończy gest i wraca do tego samego ID; usunięcie wybiera
najbliższą stronę. Po 60 s bezczynności wraca zegar. Uwaga nie przejmuje ekranu,
kolejność stron pozostaje stała niezależnie od uwagi i stanu usługi.

## Stan i ograniczenia

Napisy stanu są zamieniane na `TileState` i `Freshness` przy odczycie protokołu;
wewnętrzny kod nie porównuje wartości `"ok"`/`"warning"`/`"error"`.
`ServiceCard` jest sprawdzaną wartością z danymi tylko do odczytu: zdrowy lub
ostrzegający kafel online musi mieć przygotowany obraz. Zmiana pól wymaga
utworzenia nowej, sprawdzonej wartości.
Dane usług są nieaktualne albo brakujące niezależnie od dostępności procesu.
Warning zachowuje użyteczną kompozycję i dodaje oznaczenie dashboardu.
Error/missing i offline pokazują panel błędu zamiast starej liczby. Błędny
komunikat nie odnawia TTL; poprawny ping przyjętej rewizji odnawia. Offline
następuje po 15 s według zegara monotonicznego. Zarejestrowany wpis offline
pozostaje do wycofania kafli lub restartu odbiornika.

Rejestr: 7 usług i 13 kafli. Aktywne obrazy RGB: najwyżej 2246400 B (2,15 MiB).
Maski narożników są małe i współdzielone przez cztery rogi danego kafla
(144 B przy r=12, najwyżej 183 KiB dla całego rejestru przy r=120).
To świadoma korekta początkowego szkicu trzech RGBA: mały, stały koszt pamięci
pozwala wyświetlić dowolną stronę bez parsowania podczas gestu. Robocze RGBA
są zwalniane. Przygotowanie ma budżet 8 MiB, w tym rezerwę na ograniczone
DOM/fonty/scratch oraz rzeczywisty limit masek sprawdzany przed alokacją.
Maski nie są iloczynem pełnego alfabetu i każdej skali; dla przykładowej Pogody
testy mierzą koszt przygotowania masek. Fonty mogą pozostać w ograniczonym LRU,
a obrazy RGB nie pożyczają od niego pamięci.

Limity zasobów i geometrii są jawne w [kontrakcie DASH/5](dash5/specification.md).
To limity przyjęcia, nie obietnica stałego RSS ani systemu czasu rzeczywistego.
Komunikacja jest lokalna, dla zaufanych procesów tego samego UID; peer UID nie
uwierzytelnia prawa do wybranej nazwy usługi. Nie ma endpointu TCP.

## Źródła i trwałe dane

Pogoda pozostaje osobnym demonem. HttpClient weryfikuje łańcuch CA i hostname,
nie podąża za redirectami; status HTTP jest odpowiedzią, nie błędem transportu.
Pogoda narzuca limit 5 s/65536 B, cache i retry. Systemowy scheduler/DNS nie
stanowią obietnicy twardego real-time. WMO i wybór SVG dzień/noc należą do
Pogody, nie do dashboardu.

Śmieci wczytują ograniczony plik. Pełna walidacja i ustawienie strefy muszą się
udać przed podmianą cache. Strefa jest ustawiana tylko w procesie producenta.
Odczyt harmonogramu nie powoduje jego modyfikacji. Harmonogram i kopia w
`/var/lib/kafel-waste` są trwałym stanem, niezależnym od wydania binarek.

## Wdrożenie i urządzenia

Pierwsza migracja DASH/5 aktualizuje trzy aplikacje razem. Instalator odmawia
mieszania generacji. Później aplikacja nadal jest jednostką wdrożenia: nowy
wspólny kod biblioteki wymaga aktualizacji korzystających z niego binarek,
a zgodny deploy wybranego modułu nie restartuje innych. Atomowy symlink
`/opt/kafel/current` wskazuje wydanie; rollback obejmuje zmieniane jednostki
oraz konfigurację. [Szczegóły](deploy.md).

`DashboardConfig` parsuje CLI i środowisko bez sprzętu. `DisplayFormat` jest
testowalnym kontraktem RGB565, a `Framebuffer` wybiera zgodne urządzenie po
ioctl i sprawdza je po blokadzie, bez polegania na numerze fbN.

Konsola jest odseparowana od renderera: statyczny helper C++ uruchamiany jako root w `ExecStartPre`
mapuje fbcon na potwierdzone HDMI albo go odłącza. Nie uruchamia interpretera ani drugiego procesu dashboardu. Operacje są weryfikowane, częściowy błąd przywraca poprzedni
stan i blokuje start dashboardu. Izolacja pozostaje także po awarii procesu.
Parametry boot są częścią transakcji instalatora, nie pętli UI.

## Wspólna typografia

Widok systemowy i usługi używają tego samego Sans Regular/Bold. NumberFont
jest adapterem TrueType, nie osobnym geometrycznym fontem. Cyfry mają stałe
komórki, a słowa Dziś/Jutro pozostają proporcjonalne w tym samym kroju.
Zegar, temperatura i Dziś/Jutro mają 60 px oraz wspólną linię bazową 192. Nagłówki mają 22 px, a podpisy 18 px i linię bazową 223.

TextFont::ink_bounds uwzględnia overshoot, dzięki czemu clipping opadających
cyfr nie zmienia rastra przy przejściu do widoku statycznego. Jednostki metryk
są wyrównywane do rzeczywistej linii bazowej. Testy porównują kontury z TTF,
fazy animacji, przejście przez północ, clipping i zerowe alokacje.

Duże strony używają wspólnej geometrii `ui::LargeTileLayout`. Usługi nadal
publikują własne, deklaratywne szablony; dashboard nie zna pojęć pogody ani frakcji.
Kontrakt wyrównań jest sprawdzany w testach szablonów. Wszystkie duże panele
mają margines 4 px i narożniki 6 px; grafika jest proporcjonalna, do 64 px wysokości.
Heroicons są przypięte w vendor wraz z licencją MIT. Generator zachowuje geometrię
ścieżki i regułę even-odd; zmienia jedynie kolor oraz proporcjonalne położenie.

## Identyfikator buildu

Generator odczytuje Git przy każdym buildzie i tworzy mały plik C++ z funkcją
`kafel_git_revision`. Jest on bezpośrednim źródłem każdej z trzech aplikacji,
a nie nagłówkiem znalezionym przez ścieżkę include. Dzięki temu zmiana commita
lub stanu dirty unieważnia cache kompilacji bez zmiany kodu aplikacji. Test
regresyjny buduje ten sam program kolejno dla dwóch commitów i zmiany roboczej,
bez czyszczenia cache, i sprawdza rzeczywisty wynik jego uruchomienia.

Duży pager wybiera politykę `RibbonController::Paging::Adjacent`: jeden gest
może zakończyć się najwyżej na sąsiedniej stronie. Bezwładność wymaga co najmniej
24 px ruchu palca; początek gestu przypada na najbliższą aktualnie widoczną stronę,
także przy przechwyceniu trwającego przejścia. Ogólna wstążka zachowuje politykę
Free dla małych kafli. Uwaga usługi nie zmienia kolejności nawigacji i nie dodaje
starych gwiazdek do zatwierdzonej kompozycji; stan zbiorczy pozostaje niezależny.

## Niezmienniki po audycie

`CardFrame` waliduje dane i rzeczywistą pojemność buforów w konstruktorze.
Ma prywatne bufory i wewnętrznie tworzoną maskę narożników. `paint` składa
statyczny obraz i marquee w jednym buforze wiersza, a następnie stosuje jedną
geometrię panelu. Snapshot może zawierać puste obrazy kafli błędu; kompilator
zachowuje ich pozycje i pomija je przy sumowaniu pamięci.

Zasoby Pogody i katalog WMO należą do `apps/weather/assets/icons`; narzędzie
`apps/weather/tools/catalog.cpp` eksportuje mapowanie używane przez usługę.
Przykłady UI czytają gotowe zasoby. Produkcyjne generowanie nie zależy od przykładów.

Gotowość producenta oznacza odebrany ACK dla snapshotu po ostatnim wyniku źródła,
z zachowaniem jego PID. Próby źródła i potwierdzenia publikacji mają jawny porządek
w logu; nie wypisujemy ACK co klatkę ani dla każdego zwykłego heartbeat.

## Koszt aktualizacji

Test prawdziwego `ServiceFeed` sprawdza liczniki kompilacji szablonów,
przygotowania fontów, rasteryzacji SVG i obrazów:

- heartbeat: zero tych operacji;
- zmiana temperatury bez zmiany końcowego napisu: zero tych operacji;
- nowa widoczna temperatura: jeden obraz, najwyżej jeden font, bez kompilacji
  szablonu i rasteryzacji niezmienionej ilustracji.

Test transportu liczy jedno dekodowanie JSON na wiadomość i zero dodatkowych
przy wznowieniu przygotowania. Licznik istnieje tylko w buildzie testowym.

Test zapisu framebuffera sprawdza jedno wywołanie dla packed RGB565,
320 dla stride z paddingiem, częściowe zapisy, EINTR i błędy urządzenia.
Są to liczniki operacji, nie pomiar czasu na CM4.

## Sprawdzanie refaktoryzacji

`zig build visual-test` porównuje 240 pełnych obrazów PPM z sumami zatwierdzonej
wersji: ekrany usług i awarii, przewijanie stron oraz opadające cyfry zegara.
Test wchodzi również do `zig build test` i `dashboard-test`. Używa przykładowych
danych i działa bez urządzenia, Pillow oraz połączenia sieciowego.

Test prawdziwego `AsyncCompiler` używa wątku, pipe i renderowania TTF/SVG.
Punkt synchronizacji istnieje tylko w buildzie testowym; nie podmienia kompilatora.
Regresje obejmują anulowanie przed końcem pracy, ponowne użycie po błędzie,
jednoczesny EOF/deadline i zakończenie oraz dołączenie wątku w destruktorze. Osobna bramka zatrzymuje prawdziwy wątek
przed przekazaniem snapshotu: ACK musi czekać na zwolnienie tej bramki.
Test atomowości wymusza kolejno błędy alokacji zatwierdzania: dla wstawienia
oraz podmiany wpisu, w ścieżce synchronicznej i asynchronicznej.
