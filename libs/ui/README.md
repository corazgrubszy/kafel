# UI

Aktualne komponenty renderera: Surface RGB888, TextFont TrueType, NumberFont,
TilePainter, Grid, RibbonLayout/RibbonController i CaptionCycle.
Bez zależności od danych usług, framebufferu i evdev. Zależność fontów:
`vendor/stb`, font IBM Plex Mono z licencją OFL w `assets`.

`zig build test` uruchamia również testy tej biblioteki.
