# kafel-weather

Demon C++: Open-Meteo → walidacja/cache/retry → duży kafel DASH/5.
Nie rysuje pikseli i nie uruchamia serwera HTTP. Publikuje dane, szablon JSON
oraz wybraną, statyczną ilustrację SVG Meteocons Flat.

## Uruchomienie i wdrażanie

```sh
zig build weather-test -Doptimize=ReleaseFast
zig build weather
zig build deploy-weather
```

Pierwsze przejście ze starszego protokołu: **`zig build deploy`**, wszystkie trzy
aplikacje razem. Instalator odmawia częściowej migracji. Kolejne zgodne
aktualizacje Pogody nie restartują dashboardu ani Śmieci.

`/etc/kafel/weather.env` zawiera `KAFEL_WEATHER_LATITUDE`,
`KAFEL_WEATHER_LONGITUDE` i opcjonalne `KAFEL_DASHBOARD_SOCKET` oraz
`KAFEL_WEATHER_CA_BUNDLE`. Współrzędne pochodzą z prywatnej konfiguracji
wdrożenia; repo nie zawiera ustawień prywatnego urządzenia.
Domyślny socket: `/tmp/kafel-<uid>/dashboard.sock`.

## Dane i odporność

Zapytanie używa jawnych jednostek °C, km/h, mm, Unix UTC i parametrów:

- `current=temperature_2m,apparent_temperature,wind_speed_10m,weather_code,is_day`;
- `hourly=rain,snowfall,precipitation`.

Model zachowuje temperaturę, odczuwalną, wiatr, godzinowy wodny odpowiednik
opadów, koniec jego okresu, surowy kod WMO i flagę dnia. Suma opadów pochodzi
z ostatniego godzinowego okresu zakończonego niepóźniej od `current.time`.
Nie sumujemy mm deszczu z cm śniegu; `hourly.precipitation` zawiera całość.

Niepełna, błędnie typowana lub poza zakresem odpowiedź nie zastępuje cache.
`is_day` przyjmuje wyłącznie liczby 0/1. Pobieranie po sukcesie: co 15 minut.
Retry po błędzie: 30 s, 1 min, 2 min, maksymalnie 5 min; sukces resetuje retry.
Po 45 minutach bez udanej aktualizacji cache staje się warning/stale.
Bez cache publikujemy error/missing z „Brak danych”, bez starej liczby.
DASH/5 heartbeat jest niezależny od retry źródła: co 5 s.

HttpClient: weryfikacja CA/hostname, deadline 5 s, limit body 65536 B;
status inny niż 200 lub niepoprawny JSON jest błędem pobrania. Domyślne testy
używają lokalnego serwera, nie wywołują API pogodowego w sieci.

## Prezentacja i zasoby

`appearance.cpp` mapuje wszystkie 29 kodów aktualnego Open-Meteo, włącznie
z 97. Dzień/noc wybiera wyłącznie z `is_day`; dashboard nie zna WMO.
Nieznany kod daje `not-available` i „Brak opisu”. Niektóre intensywności dzielą
ikonę, zachowując osobny opis; marznące opady używają przybliżonego `sleet`.
Przygotowane SVG i katalog należą do [`assets/icons`](assets/icons/README.md).
[Pełny przegląd](../../libs/ui/examples/weather-card/meteocons/catalog/README.md)
jest osobnym przykładem UI; produkcja nie wymaga jego źródeł ani programu.

Duży kafel ma temperaturę z °C, opis, odczuwalną, wiatr w m/s i opady w mm.
Tekst i SVG zmieniają się wspólnie. Nie ma linii rozdzielających.

`assets/template.json` jest czytelnym szablonem. `assets/bundle.json` osadza
szablon i 40 przygotowanych SVG w samodzielnej binarce. To nie znaczy, że
wszystkie ikony są uploadowane: dashboard pobiera tylko potrzebne hashe.
Po zmianie szablonu/SVG:

```sh
python3 scripts/prepare_service_assets.py --app weather
```

Każdy build sprawdza aktualność bundle. Zwykły build nie wymaga Pythonowych
narzędzi graficznych. Odtwarzanie oryginalnych masek Meteocons jest osobnym
krokiem offline opisanym przy katalogu. Meteocons: Bas Milius, MIT;
fonty IBM Plex Sans: OFL; Rosé Pine: MIT, atrybucje w repo projektu.

[Kontrakt DASH/5 i konkretny przykład](../../docs/dash5/specification.md).

Układ dużego kafla: wiatr i opady nad główną temperaturą, obok ilustracji WMO;
temperatura Sans Bold 60 px na wspólnej linii z zegarem, odczuwalna 18 px pod nią.
Bez stałego napisu TERAZ. Viewport przygotowanych SVG jest przycięty do widocznej
sylwetki, bez zmian ścieżek, aby ikony miały spójną wysokość 64 px.
