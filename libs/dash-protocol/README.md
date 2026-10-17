# DASH/4 — pełny zestaw kafli jednej usługi

DASH/4 jest lokalnym protokołem tekstowym. Jeden datagram Unix zawiera pełny
zestaw kafli jednej usługi. Dashboard sprawdza cały komunikat i pojemność rejestru
przed zmianą danych. Niepoprawny lub ucięty komunikat nie zmienia kafli ani czasu
ostatniego kontaktu. Nie ma fragmentacji, aktualizacji częściowych ani JSON.

## Format

Separatory pól to pojedyncze tabulatory, rekordów — LF (`\n`). Ostatni rekord
również kończy się LF. Zapis `\t` / `\n` poniżej oznacza rzeczywiste separatory.

```text
DASH/4\texample-service\tExample Service\n
TILE\tweather\t2\tok\tnormal\tPogoda\tmetric\t8\t°C\tPogodnie\tWiatr: 5 m/s\n
TILE\twaste\t2\tok\tattention\tŚmieci\ttext\t[#0072CE]🗑[/]\u2028Dzisiaj\n
```

Nagłówek: wersja, ID usługi, nazwa usługi.
Wspólny początek rekordu: `TILE`, ID kafla, szerokość, stan, uwaga, tytuł, typ.
Typ określa dalsze pola:

```text
TILE\tid\tspan\tstate\tattention\ttitle\ttext\tdetail\n
TILE\tid\tspan\tstate\tattention\ttitle\tmetric\tvalue\tunit[\tcaption1[\tcaption2[\tcaption3]]]\n
TILE\tid\tspan\tstate\tattention\ttitle\tmetric-meter\tvalue\tunit[\tcaption1[\tcaption2]]\tlevel\n
```

Nawiasy kwadratowe w drugim wzorze oznaczają opcjonalne pola; nie są częścią
komunikatu. Typ `text` ma dokładnie 8 pól, `metric` od 9 do 12.
Typ `metric-meter` ma od 10 do 12 pól: wartość, jednostkę, opcjonalnie
0–2 podpisy tekstowe i obowiązkowy poziom słupka w ostatnim polu. Jednostka może
być pusta, ale jej pole (tabulator) jest obowiązkowe. Brak podpisów oznacza
zakończenie wiersza po jednostce, bez dodatkowego tabulatora.

| Pole | Reguła |
|---|---|
| ID usługi / kafla | 1–32 znaki `a-z0-9._-` |
| Nazwa / tytuł | 1–48 bajtów poprawnego UTF-8 |
| Szerokość | `1`, `2` albo `3` kolumny; piksele i odstępy wylicza UI |
| Stan kafla | `ok`, `warning`, `error` |
| Uwaga | `normal`, `attention`; niezależna od stanu |
| Typ | `text`, `metric` albo `metric-meter` |
| Poziom `metric-meter` | Całkowita liczba ASCII 0–10000; brak znaku, kropki, jednostki i spacji |
| Opis `text` | 0–512 bajtów UTF-8 wraz ze znacznikami kolorów |
| Wartość `metric` | 0–8 bajtów; pusta albo `-?digits(.digits)?`, ASCII |
| Jednostka | 0–8 bajtów zwykłego UTF-8 |
| Podpisy | 0–3 napisy, każdy 1–64 bajty zwykłego UTF-8 |

Pola nie mogą zawierać znaków sterujących ASCII (w tym NUL, CR, tabulatora
ani LF) ani DEL. Nieznane rekordy, dodatkowe pola, puste rekordy, zduplikowane
ID kafli i nieznane wartości stanów/szerokości powodują odrzucenie całości.
Tylko opis `text` może zawierać znaczniki kolorów opisane niżej. Pozostałe
napisy są zwykłym tekstem.

## Układ zawartości

Nagłówek kafla prezentuje tytuł, stan i uwagę. W zdrowym kaflu jednej kolumny
pomijamy zbędne `OK`; tytuł i ewentualna gwiazdka tworzą wspólny wiersz.
Awaria, ostrzeżenie i offline nadal pokazują oznaczenie stanu. `text` rysuje kolorowany opis
fontem 18 px i przewija go, gdy jest zbyt długi.

`metric` rysuje wartość tym samym geometrycznym fontem co CPU/RAM/eMMC,
z obsługą minusa i kropki dziesiętnej. Jednostka ma 14 px. Renderer wybiera
spośród dwóch przygotowanych skal cyfr, uwzględniając jednostkę i szerokość
kafla. Pusta wartość daje `--`, a wartość niemieszcząca się nawet w mniejszej
skali — `###` bez jednostki. Nigdy nie pokazuje obciętej liczby.

Podpisy zajmują obszar pomocniczy pod wartością, fontem 14 px. Dwa lub trzy
podpisy zmieniają się co 4,8 s, z wygaszeniem/pojawieniem po 160 ms. Długi
podpis ma marquee; okres zostaje wydłużony tak, by mieścił pełny cykl odczytu.
Jeden podpis nie rotuje. Sama zmiana wartości lub treści podpisu nie restartuje cyklu, jeśli liczba
podpisów i wymagany okres odczytu pozostają takie same.
Przewijanie wstążki wstrzymuje i rozpoczyna cykl od pierwszego podpisu.

Podczas stałego podpisu nie ma odrysowywania bez zmiany widocznych danych.
Wybudzenie następuje w terminie marquee lub przejścia, maksymalnie 25 fps.
Niewidoczny kafel nie wybudza ekranu. Stan offline zastępuje metrykę tekstowym
błędem komunikacji; nie pozostawia starej liczby jako bieżącego pomiaru.

Usługa deklaruje treść, a UI ustala pozycje, fonty i kolory stanu. Nie ma
współrzędnych, CSS ani domenowych typów pogody. CPU/RAM/eMMC zachowują geometrię etykiet, cyfr i jednostek. Słupki mają
zatwierdzone 10 px; ich dolna krawędź pozostaje w dotychczasowym miejscu.

### Metryka ze słupkiem w rotacji

`metric-meter` rozszerza zawartość metryki o **ostatnią stronę obszaru
pomocniczego**. Najpierw pojawiają się podpisy tekstowe w podanej kolejności,
na końcu sam słupek. Łącznie najwyżej trzy strony. Przy braku podpisów
słupek jest jedyną, statyczną stroną. Tytuł, wartość i jednostka nie rotują.
Poziom jest unormowany do 0–10000 (0–100%); dashboard nie zna jednostek
ani dziedziny tej wartości. Na przykład:

```text
TILE\tweather\t2\tok\tnormal\tPogoda\tmetric-meter\t13\t°C\tSucho\tWiatr: 5 m/s\t1529\n
```

Renderer zachowuje obecne wygaszanie, rytm rotacji 4,8 s i usypianie
podczas pauz. Zmiana samego poziomu nie resetuje fazy rotacji. Ukryty
kafel nie wybudza renderera. Offline usuwa metrykę wraz ze słupkiem.
Brak danych źródłowych producent może wyrazić zwykłą metryką z pustą
wartością i jednym podpisem `Brak danych`, bez słupka.

Segmenty mają stałe **4 px szerokości, 2 px przerwy i 10 px wysokości**.
Ich liczba wynika z szerokości pola, nie z informacji przesłanej przez usługę.
Wypełnienie zaokrąglamy do najbliższego segmentu. Reszta miejsca pozostaje
na końcach, nigdy w przerwach. Pole 138 px daje 23 segmenty i po 1 px luzu.
Przy nieparzystej reszcie jeden koniec ma o 1 px więcej luzu — wynika to
z całkowitoliczbowej geometrii siatki. CPU/RAM/eMMC pozostają bez zmian.

To dwa równoprawne rodzaje metryki DASH/4: `metric` rotuje wyłącznie tekst,
`metric-meter` kończy rotację słupkiem. Nie ma rozpoznawania słów kluczowych
wewnątrz zwykłych podpisów ani heurystyki zależnej od tytułu kafla.
Starszy dashboard bez `metric-meter` odrzuci taki snapshot w całości;
przy wdrożeniu należy zaktualizować dashboard przed nowym producentem.

## Zestaw, tożsamość i kolejność

- Tożsamość kafla to para `(service_id, tile_id)`.
- Pominięty kafel znika. Sam nagłówek oznacza działającą usługę bez kafli.
- Kolejność usług pozostaje kolejnością rejestracji; kolejność ich kafli wynika
  z aktualnego komunikatu. CPU / RAM / eMMC zawsze poprzedzają kafle usług.
- Zmiana samej treści lub stanu zachowuje nawigację. Dopisanie kafli na końcu
  zachowuje trwający ruch wstążki.
- Przy usunięciu, zmianie szerokości lub kolejności UI kończy gest i wyrównuje
  widok do tego samego ID kafla. Gdy kafel zniknie, wybiera najbliższy pozostały
  indeks. Jeśli nie ma kafli usług, nadal dostępne są trzy metryki systemowe.
- Niepoprawny pakiet i przekroczenie limitu pozostawiają cały poprzedni zestaw.

## Dostępność i stan

Heartbeat należy do usługi, nie do poszczególnych kafli. Zalecany okres wysyłania
to 5 s. Po 15 s bez poprawnego, przyjętego komunikatu wszystkie kafle usługi
pokazują `OFFLINE` i `Brak komunikacji z usługą`. Ostatni zestaw pozostaje
w rejestrze. Poprawny kolejny komunikat przywraca osobne stany i opisy kafli.
Kafle nie mają osobnych zegarów TTL.

`warning` renderuje `UWAGA`, `error` — `AWARIA`. Awaria pogody nie zmienia
stanu odbioru. `attention` oznacza gwiazdkę na konkretnym kaflu oraz przy zegarze;
nie zmienia `ok` w awarię i nie wymusza przewijania. Ostrzeżenia, awarie i offline
również sygnalizują problem na głównym kaflu, zgodnie z dotychczasowym UI.

## Limity i transport

Rejestr ma 7 miejsc na usługi i 13 na kafle usług łącznie; każdy komunikat może
zawierać najwyżej 13 kafli. Datagram ma najwyżej 8192 bajty. Są to limity stałe:
parser, rejestr i aktualizacja widoku nie alokują pamięci na stercie. Nieaktywne
usługi zachowują miejsce do restartu dashboardu.

Transport pozostaje prywatnym gniazdem Unix datagram
`/tmp/dashboard-v2-<uid>/status.sock`, dostępnym dla tego samego użytkownika.
Ścieżka nie zmienia się wraz z wersją protokołu. Nazwa usługi jest metadanymi;
renderer używa tytułów kafli. Producent może zawrzeć nazwę w tytule, gdy pomaga
to rozróżnić kafle różnych usług.

## Kolorowany tekst

Znacznik `[#RRGGBB]tekst[/]` nadaje kolor fragmentowi opisu. Hex może zawierać
małe i wielkie litery. `[/]` przywraca domyślny kolor; `[[` wyświetla dosłowny
`[`. Nie ma zagnieżdżania: otwarcie koloru w aktywnym fragmencie pozostaje
tekstem. Niepoprawne otwarcia, otwarcia bez nieucieczonego zamknięcia i samotne
zamknięcia również pozostają widocznym tekstem. Nie ma HTML, ANSI ani CSS.

```text
[#0072CE]🗑[/] Dzisiaj   [#E6B800]🗑[/] Jutro
```

`🗑` to U+1F5D1, bez selektora emoji U+FE0F. Renderer ma własny kanciasty glyph
tego znaku. Kolory, frakcje i podpisy ustala producent; dashboard nie interpretuje
ich domenowego znaczenia. Font ma przygotowane ASCII, polskie litery, stopień
oraz półpauzę i pauzę. Inne poprawne znaki UTF-8 mają jawny fallback `?`.
Mierzenie i przewijanie pomijają poprawne znaczniki kolorów; opis jest jednym
ciągłym wierszem. Limit 512 bajtów obejmuje znaczniki.

## Wdrożenie

DASH/4 jest jedynym obsługiwanym formatem. Inne wersje są odrzucane bez zmiany
rejestru i bez odnowienia heartbeat. Nie ma adapterów pojedynczego kafla ani
niejawnych wartości domyślnych dla pominiętych pól.

Najpierw wdrażamy dashboard, potem producenta zgodnego z DASH/4. Nazwa katalogu socketu jest zachowana przy przeniesieniu do Kafel; nie
oznacza obsługi starego protokołu. Protokół
opisuje tekst i metryki; nie deklaruje obrazów ani specjalnych widoków
pogodowych.

Odbiornik przetwarza najwyżej 16 datagramów na obrót pętli. Diagnostyka raportuje
pierwsze odrzucenie i kolejne najwyżej raz na minutę, bez zapisywania treści
pakietu. Katalog ma uprawnienia 0700, gniazdo 0600. Blokada `flock` zapobiega
podmianie aktywnego gniazda przez drugą instancję. Po awarii kolejna instancja
może usunąć pozostałe gniazdo, ale nie podmienia zwykłego pliku ani symlinku.
To transport zaufanych procesów jednego użytkownika; nie zapewnia autoryzacji
poszczególnych ID usług ani trwałej historii.

### Dwa wiersze tekstu

Pole `text` może zawierać jeden separator wiersza Unicode U+2028 (UTF-8
E2 80 A8, nie dosłowny zapis `\u2028`). Pierwszy wiersz trafia do obszaru
głównego (18 px, kolorowane glyphy), drugi do pomocniczego (14 px, również
kolorowany). Każdy wiersz zamyka własne znaczniki kolorów. Dalsze separatory
odrzucają cały pakiet. Pojedynczy wiersz zachowuje dotychczasowy wygląd.
Limit 512 bajtów obejmuje oba wiersze i separator. Każdy wiersz przewija się tylko wtedy, gdy sam nie mieści się w kaflu.
Aktualizacja jednego kafla nie restartuje przewijania pozostałych kafli.
Statyczne mieszczące się wiersze nie generują klatek w spoczynku.

`kafel-waste` wysyła `Śmieci` domyślnie o szerokości 1 (opcjonalnie 2). Co 5 sekund publikuje cały tekst:
kolorowe kubły + U+2028 + `Dzisiaj` albo `Jutro`. Oba wiersze zmieniają się
atomowo; dashboard nie interpretuje terminów ani frakcji. Jeśli oba dni są
puste, podpis zawiera datę najbliższego odbioru, np. `19 października`.

Rdzeń C++ zawiera przenośne typy, parser i rejestr odbiorcy, bez zależności od dashboardu i fontów. Osobny adapter `platform/unix_publisher.hpp` / `src/unix_publisher.cpp` udostępnia nieblokujący transport Unix producentom kafel-weather i kafel-waste. Każdy producent składa własną treść zgodną z DASH/4.

`kafel-waste` publikuje ten sam typ `text` z dwoma wierszami, domyślnie
w jednej kolumnie (opcjonalnie w dwóch). Do czterech kubłów mieści się bez
dodatkowych spacji; dłuższe treści używają istniejącego marquee.
