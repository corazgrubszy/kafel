# Przygotowane ikony Pogody

Ten katalog jest źródłem produkcyjnych SVG Meteocons Flat i `catalog.json`.
Osadzony `../bundle.json` powstaje z tych plików; jego generowanie nie zależy
od przykładów UI. Oryginalne ilustracje i licencja MIT Bas Milius pozostają
w `vendor/meteocons-static` w głównym katalogu repo.

WMO oraz polskie opisy należą do `apps/weather/src/appearance.cpp`.
Małe narzędzie `apps/weather/tools/catalog.cpp` korzysta z tej samej tabeli,
bez renderera, protokołu, HTTP ani danych urządzenia:

```sh
zig build weather-catalog -Doptimize=ReleaseFast
python3 scripts/prepare_weather_icons.py
python3 scripts/prepare_service_assets.py --app weather
```

Przygotowanie wektorów wymaga opcjonalnych pakietów offline z
`scripts/requirements-weather-icons.txt`. Normalny build i CM4 korzystają
z gotowych, wersjonowanych plików. `catalog.json` zapisuje SHA-256 oryginałów
oraz przygotowanych SVG; zwykłe testy porównują mapowanie z pogodowym kodem C++.

Plansze, PNG i przykładowe kadry są oddzielone w
`libs/ui/examples/weather-card/meteocons/catalog`. Narzędzie
`scripts/render_weather_catalog.py` czyta ikony stąd i tworzy tam wyłącznie
wyniki przykładu. Zmiana położenia źródeł nie zmienia danych ani grafiki.
