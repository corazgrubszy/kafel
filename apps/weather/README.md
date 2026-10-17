# kafel-weather

Osobny demon C++ publikujący pogodę przez DASH/4. Nie uruchamia serwera HTTP,
nie pobiera śmieci i nie zależy od kodu dashboardu. Sam build nie instaluje ani nie uruchamia aplikacji.
`zig build deploy-weather` aktualizuje wyłącznie tę usługę, bez restartu
dashboardu ani Śmieci; [wdrożenie CM4](../../docs/deploy.md).

## Build i uruchomienie

Z katalogu głównego Kafel:

```sh
zig build weather -Doptimize=ReleaseSmall
zig build weather -Dtarget=aarch64-linux-musl -Doptimize=ReleaseSmall --prefix /tmp/kafel-weather-arm64
zig build weather-test -Doptimize=ReleaseFast
```

Pierwsze polecenie daje natywny `zig-out/bin/kafel-weather`, drugie statyczny
ARM64 `/tmp/kafel-weather-arm64/bin/kafel-weather`. `zig build test` obejmuje
również testy usługi. Żadne z tych poleceń nie wdraża programu.

```sh
KAFEL_WEATHER_LATITUDE=52.0 KAFEL_WEATHER_LONGITUDE=17.0 zig-out/bin/kafel-weather
```

| Zmienna | Znaczenie |
| --- | --- |
| `KAFEL_WEATHER_LATITUDE` | Wymagana, liczba z zakresu -90…90 |
| `KAFEL_WEATHER_LONGITUDE` | Wymagana, liczba z zakresu -180…180 |
| `KAFEL_DASHBOARD_SOCKET` | Domyślnie `/tmp/dashboard-v2-<uid>/status.sock` |
| `KAFEL_WEATHER_CA_BUNDLE` | Opcjonalny plik CA, domyślnie systemowy |

Błędna konfiguracja kończy proces z kodem 2. SIGINT/SIGTERM kończą pętlę;
trwające synchroniczne pobieranie kończy się przed wyjściem.
`--version` wypisuje wersję bez wymagania konfiguracji ani sieci.


## Odpowiedzialności

- `weather.hpp/.cpp`: model, walidacja prognozy, cache w RAM i retry;
  bez sieci i bez zegara globalnego — czas jest argumentem.
- `packet.hpp/.cpp`: czysta funkcja składająca snapshot pogody w treść DASH/4;
  cache nie zależy od protokołu ani polskich podpisów.
- `source.hpp/.cpp`: jedno pobranie przez `net::HttpClient`; status HTTP i JSON
  muszą być poprawne przed przekazaniem danych do cache.
- `libs/dash-protocol/.../unix_publisher`: wspólny, nieblokujący datagram Unix; nie tworzy
  ani nie usuwa gniazda dashboardu.
- `main.cpp`: konfiguracja i jedna pętla harmonogramu, bez wątków roboczych.

Źródło to Open-Meteo przez HTTPS z weryfikacją CA i nazwy hosta. Żądanie
ma timeout 5 s i limit body 64 KiB. Zależności: wspólny HttpClient
(cpp-httplib + Mbed TLS) i picojson 1.3.0, pojedynczy nagłówek około 26 KiB,
licencja BSD-2-Clause. Typy JSON pozostają wewnątrz implementacji dekodera.
Ograniczenie głębokości JSON wynosi 32, długości prognozy — 200 godzin.

Zapytanie wymusza °C, km/h i znaczniki czasu Unix w UTC. Walidacja obejmuje
obowiązkowe pola, typy, skończone liczby, zakresy temperatury/wiatru,
zgodne długości tablic i ciągłe, godzinowe znaczniki czasu. Odrzucamy również ucięty JSON,
przepełnione liczby oraz śmieci po końcu dokumentu. Warunki są ustalane
jak w InfoHub: opad godzinowy, śnieg ma pierwszeństwo przed deszczem.
Nie zastępujemy tego mapowaniem `weathercode`, aby zachować obecne podpisy.

## Odświeżanie, błędy i wygląd

Pierwsze pobranie następuje od razu. Po sukcesie następne jest za 15 minut;
po kolejnych błędach za 30 s, 1 min, 2 min, 5 min i dalej co 5 min.
Opóźnienia liczą się od zakończenia próby, żądania nie nakładają się.
Sukces zeruje retry. Błędy transportu, HTTP i JSON są traktowane tak samo.

Cache w RAM zmienia się wyłącznie po poprawnym pobraniu. Przy starcie bez
danych kafel pokazuje `AWARIA`, `-- °C` i `Brak danych`. Błąd późniejszego
pobrania zachowuje ostatnie dane; po **więcej niż 45 minutach** od sukcesu
stan zmienia się na `warning`, pierwszy podpis otrzymuje `Nieaktualne: `.
Sukces przywraca `ok`. Restart procesu zaczyna z pustym cache.

Heartbeat ma okres 5 s, także podczas retry. Przed pobraniem wysyłamy
aktualny snapshot, po sukcesie — od razu nowy. Sieć jest synchroniczna;
obowiązują opisane w `libs/http-client/README.md` ograniczenia timeoutu DNS
backendu systemowego. Zawieszony resolver może opóźnić heartbeat i wyjście;
nie obiecujemy twardej izolacji publikacji od takiego przypadku.
Brak dashboardu nie zatrzymuje pobierania; kolejny heartbeat próbuje ponownie.
Logujemy pierwszą awarię publikacji, później najwyżej raz na minutę i powrót.
Harmonogram, retry i wiek cache używają zegara monotonicznego.

Kafel zachowuje istniejący wygląd: `Pogoda`, szerokość **2**, metryka
z temperaturą zaokrągloną do całych stopni, `°C` i dwa podpisy:
`Sucho` / `Deszcz` / `Śnieg` oraz `Wiatr: 5 m/s` (dokładność 0,1 m/s),
a następnie sam słupek wiatru. Poziom wiatru jest przeliczany liniowo
z zakresu 0–32,7 m/s na 0–10000, z nasyceniem powyżej zakresu.
Liczbę i geometrię segmentów dobiera UI: 4 px szerokości, 2 px przerwy;
w polu 138 px są 23 segmenty z po 1 px luzu na obu końcach.
To dashboard nadal odpowiada za font, paletę i zmianę podpisów.

```text
DASH/4\tkafel-weather\tKafel Weather\n
TILE\tweather\t2\tok\tnormal\tPogoda\tmetric-meter\t13\t°C\tSucho\tWiatr: 5 m/s\t1529\n
```

Zapis powyżej przedstawia separatory; na przewodzie są rzeczywiste TAB/LF.
DASH/4 otrzymuje ogólny typ `metric-meter`, bez logiki pogody w dashboardzie.
Nowy dashboard musi poprzedzać wdrożenie nowego producenta.

## Testy

Testy domenowe bez oczekiwania i sieci zewnętrznej sprawdzają dekoder,
zakresy, głębokość JSON, dobór godziny i rodzaju opadu, walidację współrzędnych,
pełną sekwencję retry i reset, retencję cache, granicę 45 minut, powrót,
zaokrąglenia i brak ujemnego zera. Pakiety porównujemy z oczekiwanym pełnym
tekstem i przepuszczamy przez rzeczywisty parser DASH/4.
Test transportu odbiera dokładny datagram w prywatnym katalogu tymczasowym.
Integracja z lokalnym serwerem HTTP obejmuje poprawną prognozę, 503,
niepełny JSON i przekroczenie limitu body. Test rzeczywistego demona sprawdza heartbeat podczas błędu źródła,
opóźnienie retry, SIGTERM, konfigurację i `--version`.
Testy HTTPS/certyfikatów/timeoutów należą do współdzielonego HttpClient i są uruchamiane osobno.
