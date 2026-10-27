# Duża Pogoda — osobny przykład SVG 240×240

Kompozycja z JSON, ilustracja z SVG, tekst z istniejącego renderera TrueType.
Bez integracji z dashboardem, protokołem, usługami lub wdrożeniem. Dane są
przykładowe; obrazy w `previews/` są renderami C++, nie obrazami generowanymi AI.

```sh
zig build weather-card -Doptimize=ReleaseFast -Dweather-card-scene=sun
zig build weather-card -Doptimize=ReleaseFast -Dweather-card-scene=clouds
zig build weather-card -Doptimize=ReleaseFast -Dweather-card-scene=rain
zig build weather-card -Doptimize=ReleaseFast -Dweather-card-scene=snow
zig build weather-card-test -Doptimize=ReleaseFast
```

Wynik: `/tmp/kafel-weather-240.ppm`, RGB8, dokładnie 240×240. Opcjonalne
`-Dweather-card-output=/tmp/weather.ppm` zmienia ścieżkę. Cztery pliki PNG
są w `previews/`; `comparison.png` składa je bez skalowania w siatkę 480×480.

## Podział odpowiedzialności

- `apps/weather/src/appearance.cpp`: WMO → kategoria i podpis. Brak WMO w rendererze.
- `svg/*.svg`: zwykłe dokumenty SVG. Katalog to zasoby przykładu, nie rejestr w UI.
- `ui::SvgDocument`: parsowanie dokumentu przekazanego jako bajty.
- `ui::SvgRasterizer`: rasteryzacja do bufora RGBA należącego do wywołującego.
- `weather.json`: układ i powiązania tekstu z danymi, bez własnego formatu geometrii.
- `CompositionPainter`: nakłada przekazaną warstwę RGBA, bez nazw ilustracji.
- `WeatherCard::set_svg`: jednorazowa rasteryzacja i wymiana cache dopiero po sukcesie.

Wybór pliku należy do wywołującego — renderer przyjmie również inny dokument,
bez dopisywania do katalogu ikon. To osobny plik wykonywalny studium; produkcyjne szablony DASH/5 są w `apps/weather/assets`.
Pogoda grupuje mgłę jako chmury, mżawkę/burzę jako deszcz; nieznany kod daje
neutralne chmury i podpis `Brak opisu`. Nie ma jeszcze wariantu nocnego.

## Wybór biblioteki

**NanoSVG**: `nanosvg.h` i `nanosvgrast.h`, razem około 125 KiB źródeł,
licencja zlib, bez zależności poza biblioteką standardową i matematyką.
Przypięty upstream i sumy: `vendor/nanosvg/VERSION.json`; licencja obok.
Repo upstream deklaruje ograniczone utrzymanie; wybrana rewizja zawiera
nowsze poprawki niż fork FLTK. To wybór do małego, kontrolowanego prototypu.

[Źródło NanoSVG](https://github.com/memononen/nanosvg).
Rozważono też [LunaSVG](https://github.com/sammycage/lunasvg): szersza obsługa
SVG, ale projekt wieloplikowy z backendem PlutoVG. Tutaj wystarczają dwa nagłówki.

Adapter dopuszcza statyczne kształty, ścieżki, transformacje, wypełnienia,
obrysy, przezroczystość oraz gradienty liniowe/radialne. Odrzuca tekst SVG,
CSS `style`, filtry, maski, clipping, skrypty i zasoby zewnętrzne. Tekst jest
rysowany przez nasz TrueType, po ilustracji. To profil dla kontrolowanych
zasobów, nie pełny silnik ani walidator dowolnego XML/SVG.

Limit wejścia SVG: 64 KiB, zagnieżdżenie do 32, do 1024 kształtów i 65536 punktów.
Raster docelowy do 512×512; w przykładzie 240×240. Zachowujemy proporcje
viewBox i centrujemy obraz. Bufor zawiera zwykłą, niepremultiplikowaną alfę,
zgodną z `Surface::blend`. Rysowanie kafla używa cache, bez ponownego parsowania
lub rasteryzacji. Błędny nowy SVG nie niszczy poprzedniego cache.

## Szablon JSON

`CompositionDocument` korzysta z `kafel::json::document` i picojson. Wczytuje
układ, style, kontrolowane fonty i nazwane powiązania. Nie utrzymujemy kopii
geometrii ilustracji w JSON ani w kodzie renderera.

```json
{
  "at": [12, 169],
  "clip": [0, 0, 240, 240],
  "style": "label",
  "content": {
    "field": "apparent_temperature",
    "format": "integer",
    "prefix": "Odczuwalna ",
    "suffix": "°C"
  }
}
```

Limit JSON: 64 KiB, głębokość 32, 32 pola, 64 elementy, 64 style, 8 fontów.
Nieznane klucze, duplikaty (także escaped), błędny UTF-8, typy i referencje
są odrzucane. Font wskazuje identyfikator zasobu hosta, nie dowolną ścieżkę.
Wszystkie dane dokumentu są własnością `CompositionDocument`; painter i fonty
muszą żyć w poprawnej kolejności. Błędne dane nie zmieniają poprzedniej klatki.
Nie dodano parsera CSS, wierszy/kolumn, animacji ani nowego protokołu usług.

## Pomiary lokalne

```sh
python3 scripts/measure_weather_svg.py
```

Wynik: `measurements-local.json`. Platforma i procesor są zapisane w raporcie.
To pomiary komputera deweloperskiego, nie CM4. ReleaseFast; osobny proces na SVG.
100 próbek parsowania z destrukcją, pierwszy zimny raster, 300 próbek ciepłego
rastra, 200 klatek z cache i 100 klatek z ponowną rasteryzacją. Mediana i p95;
I/O pliku nie wchodzi do czasów parsera/rasteryzacji. Dane tekstowe są takie same
we wszystkich pomiarach, aby porównywać koszt ilustracji.

Benchmark ma instrumentowane wyłącznie `malloc/realloc/free` NanoSVG.
Raportuje żądane bajty sterty: bez nagłówków alokatora, wektorów C++ i fontów.
Normalny renderer nie ma tego narzutu. Osobno podajemy szczyt RSS całego procesu;
nie należy go interpretować jako pamięci samej biblioteki SVG.

Cache RGBA: **230400 B = 225 KiB**. Bufor RGB kafla: **172800 B = 168,75 KiB**.
Po przygotowaniu cache parser i rasteryzator są niszczone. Przy rysowaniu z cache
nie ma wywołań alokatora NanoSVG ani alokacji C++. Pomiary obejmują koszt
rzeczywistego tła i typografii, nie tylko blit obrazka.

## Testy

`zig build test` obejmuje również testy przykładu: gradienty obu rodzajów,
prostą alfę, AA, viewBox, bufory i clipping, nieobsługiwane funkcje SVG,
zachowanie cache po błędzie, JSON, życie danych i wybór WMO poza UI.
Nie uruchamiano podglądu na CM4 ani wdrożenia.
