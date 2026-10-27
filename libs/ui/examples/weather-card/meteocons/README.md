# Meteocons: Fill / Flat — próba w NanoSVG

`comparison.png`: lewa kolumna Fill, prawa Flat. Kolejne wiersze: słońce,
chmury, deszcz, śnieg. Każdy obraz źródłowy ma 240×240; zestawienie 480×960
nie skaluje kafli. Typografia, dane i pozycje są identyczne między stylami.

Źródło: oficjalny pakiet `@meteocons/svg-static` **0.1.0**, zweryfikowany
SHA-512 z rejestru npm. Osiem oryginalnych plików, MIT i sumy kontrolne
znajdują się w `vendor/meteocons-static`. Autor: Bas Milius.
https://github.com/basmilius/meteocons

Statyczne oryginały używają `clear-day`, `cloudy`, `rain`, `snow`.
Wybrano `cloudy`, ponieważ Flat `overcast` używa rzeczywistej maski, której
NanoSVG nie obsługuje. Maska nie została usunięta ani zastąpiona przybliżeniem.

## Adaptacja

- Przemapowano kolory do Rosé Pine Moon: gold, rose, iris, text i foam.
- Zachowano oryginalne ścieżki, kształty, transformacje i gradienty.
- Usunięto wyłącznie clipping prostokątem pokrywającym cały viewport 128×128.
  Wybrane ikony mieszczą się wewnątrz tego obszaru; sam clipping jest tu zbędny.
- Dodano zewnętrzną transformację rozmieszczającą ilustrację w kaflu.
  W obrębie danej pogody Fill i Flat dostają dokładnie tę samą transformację.
- Nie zmieniano profilu ani kodu NanoSVG, żeby przepuszczać maski, animacje lub CSS.

Bezpośrednio nasz profil SVG przyjmuje oba oryginalne słońca. Sześć pozostałych
oryginałów odrzuca ze względu na `clip-path`. Po opisanej adaptacji renderują
się wszystkie osiem. `compatibility.json` zawiera wynik rzeczywistych prób,
sumy oryginałów, adaptacji oraz buforów RGB.

```sh
python3 scripts/compare_meteocons.py
```

Skrypt działa offline na przypiętych zasobach, buduje samodzielny renderer,
sprawdza oryginały i kopie, generuje PPM oraz PNG przez bezstratną konwersję.
Nie używa przeglądarki do rysowania, nie instaluje pakietów npm, nie korzysta
z SSH i niczego nie wdraża. Nie podmienia dotychczasowych ilustracji prototypu.
Testy odrzucają maski, animacje, inne clippingi i nierozpoznane kolory zamiast
po cichu usuwać istotne części grafiki.

Cztery pogody to próbka do porównania wyglądu. Open-Meteo udostępnia obecnie
29 kodów WMO; dopasowanie wszystkich zjawisk i dzień/noc jest osobnym krokiem.

## Wybrany wariant: Flat

Pełny katalog WMO, warianty dzień/noc i przygotowane maski: [katalog](catalog/README.md).
Powyższe porównanie czterech ilustracji zachowano jako wcześniejsze studium Fill/Flat.
