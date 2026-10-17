# kafel-waste

Mały, jednowątkowy demon C++: odczytuje lokalny harmonogram, wyznacza odbiór
na dziś/jutro lub najbliższy termin i publikuje jeden kafel przez DASH/4.
Nie używa HTTP ani TLS. Nie modyfikuje harmonogramu.

## Build i wdrożenie

```sh
zig build waste -Doptimize=ReleaseSmall
zig build waste-test -Doptimize=ReleaseFast
zig build waste-linux
zig build deploy-waste
```

`waste` buduje natywny `zig-out/bin/kafel-waste`, `waste-linux` daje statyczny
ARM64/musl `zig-out/linux-arm64/kafel-waste`. `deploy-waste` testuje i aktualizuje wyłącznie Śmieci, bez restartowania
dashboardu i Pogody. `zig build deploy` aktualizuje cały zestaw, pomijając
niezmienione aplikacje. Szczegóły: [wdrożenie](../../docs/deploy.md).

Dane należą do usługi i pozostają poza katalogami wydań:
`/var/lib/kafel-waste/garbage_schedule.json`. Jednostka systemd używa
`StateDirectory=kafel-waste` z prawami katalogu `0700`.

Przy aktualizacji starej konfiguracji instalator kopiuje harmonogram z
`/home/<user>/infohub/data/garbage_schedule.json`, zachowując oryginał do etapu sprzątania opisanego poniżej.
Przed zatrzymaniem Śmieci sprawdza format jako konto demona. Kopię zapisuje
atomowo, bez nadpisywania istniejącego pliku, z prawami `0600`; potwierdza
zgodność bajtów i ponownie sprawdza odczyt jako konto usługi. Różne dane w obu
lokalizacjach przerywają wdrożenie przed restartem. Identyczna kopia jest używana
ponownie. Kolejne wdrożenia nie przywracają starej kopii nad aktualnym plikiem.

Raport zawiera `waste_schedule.path`, SHA-256 i wynik migracji. Lokalne,
ignorowane przez Git archiwum `deploy-results/<czas>/rollback.tar.gz` zawiera
oryginał jako `waste-schedule-original.json`. Rollback przywraca poprzednią
konfigurację i usługę, zachowując obie kopie danych. Przy świeżej instalacji
bez starego harmonogramu przygotuj plik osobno, dostępny dla konta usługi.

Konfiguracja w `/etc/kafel/waste.env`:

```sh
KAFEL_WASTE_SCHEDULE="/var/lib/kafel-waste/garbage_schedule.json"
KAFEL_WASTE_SPAN=1
```

Instalator wypełnia ją automatycznie. Opcjonalne `waste_schedule` i `waste_span`
w lokalnym `deploy.local.json` pozwalają zmienić ścieżkę i szerokość; wartości
są napisami, np. `"waste_span": "2"`. Kolejne wdrożenie zachowuje ustawienia.
`KAFEL_DASHBOARD_SOCKET` opcjonalnie zmienia wspólne gniazdo DASH/4.

`kafel-waste --check FILE` sprawdza harmonogram i dostępność strefy bez
publikowania. `--version` nie wymaga pliku ani konfiguracji. SIGINT/SIGTERM
kończą oczekiwanie i pętlę.

## Jedna kolumna

Domyślna szerokość to **1**, opcjonalnie **2**. Nie zmieniamy fontów ani
rysowania zatwierdzonego glyphu kubła. Brak dodatkowych spacji pomiędzy
kolorowanymi glyphami pozwala zmieścić cztery kubły w jednym kaflu:
glyph ma 10 px szerokości i 14 px kroku, grupa zajmuje 56 px kroku.

Zdrowy kafel jednokolumnowy pomija zbędne `OK`; gwiazdka uwagi jest
częścią nagłówka bezpośrednio po tytule. `Śmieci*` mieści się w obecnym foncie.
Ostrzeżenia i awarie zachowują widoczne oznaczenia stanu.
Dłuższe grupy, inne długie tytuły oraz pełne daty korzystają
z istniejącego marquee. Każdy wiersz przewija się niezależnie. `Dzisiaj`
na najwęższej komórce może również uruchomić minimalne przewijanie,
ponieważ pomiar fontu obejmuje pełne kroki znaków. Jest to kompromis
jednej kolumny; wariant dwukolumnowy nie wymaga zmniejszania tekstu.

Gdy odbiory są dziś i jutro, co 5 s zmienia się **cały tekst**: kolorowe
kubły + U+2028 + `Dzisiaj` albo `Jutro`. Flaga uwagi pozostaje aktywna.
Gdy nie ma odbioru dziś ani jutro, pokazujemy grupę najbliższego terminu
i pełną polską datę, np. `19 października`, bez uwagi.

```text
DASH/4\tkafel-waste\tKafel Waste\n
TILE\twaste\t1\tok\tattention\tŚmieci\ttext\t[#0072CE]🗑[/][#E6B800]🗑[/]\u2028Dzisiaj\n
```

Na przewodzie są rzeczywiste TAB, LF i U+2028. Nie rozszerzamy protokołu.
Dashboard nie interpretuje dat ani frakcji. Kolejność frakcji: mixed, glass, paper, plastic, kitchen, garden.
Kolory są nasycone; kuchenne są brązowe, a ogrodowe mają ten sam zielony co szkło:

| Frakcja | Kolor | HEX |
|---|---|---|
| `mixed` | Jasnoszary | `#B8C0C8` |
| `glass` | Zielony | `#008F39` |
| `paper` | Niebieski | `#0072CE` |
| `plastic` | Żółty | `#E6B800` |
| `kitchen` | Brązowy | `#A0522D` |
| `garden` | Zielony | `#008F39` |

## Harmonogram i kalendarz

Obsługiwany jest istniejący format:

```json
{
  "timezone": "Europe/Warsaw",
  "pickups": [
    {"date": "2026-10-19", "fraction": "paper"},
    {"date": "2026-10-19", "fraction": "plastic"}
  ]
}
```

Przykład jest syntetyczny; rzeczywisty harmonogram nie jest dodawany do repo.
Daty muszą być poprawnymi `YYYY-MM-DD`, a frakcje jedną z sześciu nazw powyżej.
Dni są sortowane; duplikaty tej samej frakcji w danym dniu są scalane.
Nieznana frakcja odrzuca nowy plik, zamiast znikać bez ostrzeżenia.
Limit: 256 KiB, 4096 rekordów i głębokość JSON 32.

Strefa musi występować w systemowej bazie `/usr/share/zoneinfo`.
Ustawiamy `TZ` wyłącznie we własnym procesie. Dzisiejsza data pochodzi
z `localtime_r`, jutro jest następną **datą kalendarzową**, nie `+86400 s`.
Działa to również przy zmianie czasu, roku i w latach przestępnych.
Nie zakładamy na stałe roku 2026; kolejne harmonogramy mogą zawierać inne lata.

## Odświeżanie i błędy

Heartbeat co 5 s wyznacza stan na podstawie bieżącej lokalnej daty, więc
przejście przez północ nie czeka na ponowne wczytanie pliku. Plik odczytujemy
przy starcie i co 60 s. Po błędzie odczytu/JSON/strefy ponawiamy co 30 s.
Żaden błąd pliku nie zatrzymuje heartbeat.

`ScheduleCache` podmienia dane dopiero po pełnej walidacji. Niepoprawny nowy
plik zachowuje poprzednie odbiory ze stanem `warning` i podpisem
`Nieaktualne: ...`; sukces przywraca `ok`. Bez poprzednich danych jest
`error` i `Brak harmonogramu`. Po wyczerpaniu wszystkich terminów jest
`warning` i `Brak terminów`, bez fałszywej flagi odbioru dziś/jutro.

## Odpowiedzialności i testy

- `schedule.hpp/.cpp`: model, walidacja i grupowanie, odczyt ograniczonego
  pliku, adapter strefy czasowej, cache i wybór dnia;
- `packet.hpp/.cpp`: czysta prezentacja — kolory, polskie podpisy i DASH/4;
- `main.cpp`: konfiguracja, harmonogram odczytu i heartbeat, logi i sygnały;
- `libs/dash-protocol/.../unix_publisher`: wspólny nieblokujący transport
  producentów; Pogoda i Śmieci nie zależą od siebie;
- `libs/json`: wspólna ograniczona nakładka na istniejący picojson.

Testy pokrywają walidację, sortowanie, duplikaty, kalendarz/DST, dziś/jutro,
najbliższy termin, wyczerpany rok, uwagę, zachowanie cache po błędzie i powrót,
pełne pakiety z rzeczywistym parserem DASH/4 oraz błędy plików. Integracja
uruchamia prawdziwego demona z prywatnym gniazdem Unix i syntetycznym plikiem;
sprawdza heartbeat, SIGTERM i identyczny hash pliku po pracy demona.
Test renderera potwierdza kompletne cztery kubły bez skalowania i niezależne
przewijanie pełnej daty. Test wdrożenia sprawdza przejście z dwóch usług do
trzech i powrót po błędzie — harmonogram pozostaje nietknięty.


### Kopia harmonogramu i sprzątanie InfoHuba

Po sprawdzeniu działania `kafel-waste`, `zig build deploy-waste` zapisuje kopię
starego harmonogramu jako
`/var/lib/kafel-waste/garbage_schedule.infohub-backup.json` (`0600`).
Sprawdza zgodność bajtów i odczyt kopii jako konto usługi, dopiero potem usuwa
`/home/<user>/infohub/data/garbage_schedule.json` oraz puste katalogi `data`
i `infohub`. Inne pliki pozostają nietknięte i są zgłaszane w raporcie.
Konflikt kopii lub błąd zapisu blokuje usunięcie źródła. Backup pozostaje
niezmienny przy kolejnych wdrożeniach, również gdy aktywny harmonogram się zmieni.
Sprzątanie wykonuje się także przy niezmienionej binarce, bez restartu usług.
Raport `infohub_cleanup` wskazuje kopię i ewentualne pozostałości.
Lokalne repo InfoHub i prywatne dane poza CM4 pozostają bez zmian.
