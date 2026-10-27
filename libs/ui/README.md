# UI

Renderer programowy bez Linux, protokołu usług i domenowej wiedzy o Pogodzie.
Surface, TextFont, tabularny NumberFont, TilePainter, Grid, RibbonController
oraz kompozycje JSON/SVG są współdzielone przez dashboard i przykłady.
Fonty: IBM Plex Sans Regular/Bold (OFL); rasteryzacja przez stb.

## Typografia

Główne wartości używają Sans Bold: zegar, temperatura, procenty i Dziś/Jutro.
Tekst pomocniczy używa Sans Regular: data, adres IP, frakcje i jednostki. Hostname jest nagłówkiem Sans Bold 22 px.
Zegar, temperatura i Dziś/Jutro mają 60 px, procenty zasobów stałe 32 px; skala temperatury pozostaje w szablonie.
CPU/RAM/eMMC wyświetlają 0–99% (100% jest pokazywane jako 99%); słupek zachowuje rzeczywisty pomiar.

NumberFont jest adapterem nad rzeczywistym TTF, bez własnych konturów cyfr.
Stabilizuje advance cyfr i zachowuje naturalną szerokość interpunkcji. Obecny
Sans Bold ma cyfry tabularne już w pliku, bez dodatkowej obsługi OpenType.

TextFont::ink_bounds wyznacza granice tuszu, także overshoot zaokrąglonych
cyfr. Wspólny box i linia bazowa zapewniają brak obcięć oraz identyczne fazy
0/1 animacji względem widoku statycznego. Stary glyph opada, nowy wjeżdża
z góry; inne pozycje pozostają nieruchome. Maski są przygotowane przed
rysowaniem. Animacja i gesty nie alokują.

## Kompozycje

`CardTemplate` kompiluje szablon do geometrii, stylów i powiązań pól.
Aktualizacja podstawia dane bez ponownego parsowania szablonu. `visual_key()`
sprawdza dane i wyznacza tożsamość sformatowanych napisów oraz ilustracji,
więc pomiar bez zmiany widocznej wartości nie wymaga rasteryzacji.

`CardRenderCache` przechowuje fonty i bitmapy SVG w LRU: do 32 wpisów i 8 MiB
buforów. Zmiana temperatury zachowuje gotową ilustrację i niezmieniony font
podpisów. Nowe bitmapy znaków podlegają też wspólnemu limitowi masek 8 MiB.
`PreparationStats` pozwala sprawdzić liczbę przygotowań fontów, SVG i klatek.

`prepare_card()` pozostaje wygodną funkcją dla pojedynczego renderu; tworzy
lokalny szablon i cache. Dashboard zachowuje te obiekty pomiędzy aktualizacjami; cache kompilacji mieści
do 26 szablonów i współdzieli wynik z walidacją zasobu.
Renderer nie zna nazw ikon ani kodów WMO.
[Kontrakt DASH/5](../../docs/dash5/specification.md),
[architektura](../../docs/architecture.md),
[osobne studium Pogody](examples/weather-card/README.md).

```sh
zig build test -Doptimize=ReleaseFast
zig build preview -Dscene=system
zig build preview-clock -Dclock-preview-dir=/existing/directory
```

## Przewijane podpisy

W szablonie `kafel.tile/1` pole tekstowe może wybrać `overflow: "marquee"`.
Tylko przepełniony tekst dostaje przygotowaną maskę pokrycia. `CardFrame` przechowuje
statyczne tło i te maski; `CardFrame::paint` łączy RGB z tymi maskami w buforze jednego wiersza, po czym
stosuje wspólny obrys panelu dokładnie raz. Bez fontów, alokacji i rasteryzacji.
`ui::MarqueeText` ustala ruch wahadłowy (24 px/s, pauzy 1200 ms), a `next_marquee_change`
podaje czas do pierwszej rzeczywistej zmiany piksela. Dashboard uruchamia czas przy
otwarciu strony i nie planuje animacji ukrytych kafli.

`CardFrame` jest niezmiennym obiektem sprawdzanym w konstruktorze: RGB ma
240×240×3 bajty, warstwy mają poprawne prostokąty, wymiary i długości masek,
a sumy i współrzędne są ograniczone przed malowaniem. Limity obejmują również
rzeczywiste pojemności przekazanych wektorów. `storage_bytes()` raportuje
przechowywaną pamięć, a `same_visuals()` służy do współdzielenia identycznych klatek.
Nie można przekazać własnej, niespójnej maski narożników ani zmieniać klatki po
przygotowaniu. Narożniki i margines 4 px dotyczą jednocześnie obrazu i tekstu.
