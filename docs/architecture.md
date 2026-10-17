# Architektura Kafel

## Granice odpowiedzialności

| Warstwa | Odpowiedzialność | Nie odpowiada za |
|---|---|---|
| `DashboardView` | Kompozycja, aktualizacja modeli, nawigacja, terminy zmian widoku | Sieć i interpretacja danych Pogody/Śmieci |
| `ui` | Geometria, paleta, tekst, cyfry, kafle, wstążka, animacje | Linux, protokół usług i pobieranie danych |
| `ServiceRegistry` | Atomowe snapshoty, tożsamość, pojemność, TTL | Rysowanie i odczyt gniazda |
| Adaptery Linux | Framebuffer, evdev, gniazdo odbiorcze i system probe | Układ oraz polityka usług |
| Modele producentów | Walidacja, cache, retry lub kalendarz | Sprzęt LCD i typografia |
| `packet` producenta | Przekształcenie stanu w DASH/4 | Pobieranie i rysowanie |
| `HttpClient` | GET, TLS, deadline i limit treści | JSON, retry i reguły biznesowe |
| `unix_publisher` | Ograniczony, nieblokujący datagram | Stan usługi i harmonogram publikacji |

Wymiana danych ma postać wartości i ograniczonych struktur. Nie ma kontenera
DI, hierarchii widgetów z wirtualnymi metodami ani globalnego magistralowego
systemu zdarzeń. Klasy posiadające zasoby systemowe zwalniają je przez RAII.
Nie wszystkie części są bezalokacyjne: ograniczony rejestr/parser i ścieżki
rysowania mają inne wymagania niż parser JSON czy klient HTTPS.

## Droga do pikseli

1. `LinuxSystemProbe` dostarcza `SystemSnapshot`; parser i rejestr usług
   dostarczają niezależne modele kafli.
2. `DashboardView` aktualizuje tylko odpowiednie dane i stan ruchu wstążki.
3. `Grid` i `RibbonLayout` wyznaczają geometrię z szerokości 1/2/3 kolumn.
4. `TilePainter`, `TextFont`, `NumberFont` i `Surface` rysują do buforów pamięci.
5. Adapter dashboardu przygotowuje RGB565, a `Framebuffer::present` zapisuje
   wszystkie wiersze z uwzględnieniem rzeczywistego stride.

Font TrueType jest zaufanym zasobem wkompilowanym w binarkę. Maski potrzebnych
znaków i rozmiarów powstają na starcie. Usługa nie może podać obcego fontu
ani wymusić dowolnej wielkości tekstu.

## Czas i wejście

Zegar ścienny służy do godziny i daty. Zegar monotoniczny służy do gestów,
animacji, TTL i deadline'ów. `FrameSchedule` ogranicza klatki do 25 fps i
wylicza timeout dla `poll`: wejście dotykowe, gniazdo usług, próbka systemowa,
powrót wstążki lub następna zmiana wizualna.

`RibbonController` odpowiada za bezwładność i dosuwanie. To nawigacja jednego
poziomego rzędu, nie ogólny silnik fizyczny. `CaptionCycle` przełącza podpisy;
marquee każdej linii jest niezależne. Aktualizacja jednego kafla nie resetuje
pozostałych. Statyczny mieszczący się tekst nie generuje dodatkowych klatek.

Adapter evdev rozpoznaje bezpośredni dotyk i skaluje osie do LCD. Obsługuje
utratę synchronizacji `SYN_DROPPED` i anuluje niepewny gest. Nie implementuje
klawiatury, formularzy ani gestów wielopalcowych; urządzenia wyłącznie MT
bez zgodnych ABS_X/Y wymagają innego adaptera.

## Producent i odbiorca

Każdy producent wysyła pełny zestaw swoich kafli. Odbiorca najpierw sprawdza
cały datagram i pojemność rejestru, potem go przyjmuje. Odrzucenie nie odświeża
TTL. Brak producenta daje offline; producent żywy, lecz bez danych źródła,
może wysyłać error. To dwie różne sytuacje.

Dane i podpisy pochodzą od producenta; wygląd i rytm prezentacji od UI.
Zmiana kubłów i podpisu jest jedną aktualizacją tekstu z U+2028. Pasek wiatru
jest ogólnym poziomem 0–10000, nie specjalnym typem pogodowym.

Gniazdo Unix jest prywatne dla konta usług. Nie ma uwierzytelniania sieciowego,
potwierdzeń odbioru ani automatycznego usuwania wpisów offline z rejestru.
Heartbeat umożliwia powrót po restarcie odbiorcy. Stała pojemność oznacza,
że nieaktywne ID zajmują miejsce do restartu dashboardu.

## Sieć i pliki

Pogoda jest jednowątkowa. Przed synchronicznym pobraniem publikuje stan;
przy typowym limicie żądania zachowuje heartbeat, ale systemowy resolver DNS
może opóźnić powrót. Nie ma obietnicy twardego deadline'u czasu rzeczywistego.
HttpClient weryfikuje łańcuch CA i hostname, nie podąża za redirectami;
status HTTP jest odpowiedzią, nie automatycznym błędem transportu.
Limit body nie jest limitem całego RSS procesu.

Śmieci wczytują tylko lokalny, ograniczony plik. Pełna walidacja i ustawienie
strefy muszą się udać przed podmianą cache. Strefa jest ustawiana wyłącznie
w procesie producenta. Odczyt harmonogramu nie powoduje jego modyfikacji.

## Wdrożenie

Wybrana aplikacja jest jednostką aktualizacji. Biblioteki są statycznie
wbudowane w binarki; zmiana wspólnej biblioteki wymaga przebudowania i wdrożenia
każdego korzystającego z niej programu, którego zachowanie chcemy zmienić.
Nie ma osobnego wdrażania `ui`, protokołu ani HttpClient.

Wydanie pod `/opt/kafel/releases` jest kompletnym zestawem aktualnie
zainstalowanych binarek. Częściowa aktualizacja zachowuje inne binarki i nie
restartuje ich procesów. Atomowy symlink zapewnia spójne ścieżki startowe;
rollback obejmuje tylko zmieniane jednostki i konfigurację.
Szczegóły transakcji: [wdrożenie](deploy.md).

## Urządzenia i konsola

`DashboardConfig` parsuje CLI i środowisko bez otwierania sprzętu.
`DisplayFormat` jest przenośnym, testowalnym kontraktem RGB565;
`Framebuffer` odczytuje ioctl, wybiera zgodne urządzenie i ponownie sprawdza
format po uzyskaniu blokady. Nie polega na kolejności rejestracji `fbN`.

Zmiany konsoli wymagają uprawnień systemowych, więc są oddzielone od renderera:
krótki helper root w `ExecStartPre` mapuje fbcon na potwierdzone HDMI albo go
odłącza. Nie ma stale działającego procesu Python. Operacje są weryfikowane,
a częściowy błąd przywraca wcześniejszy stan i blokuje start dashboardu.
Przy normalnym stopie izolacja pozostaje, także przy awarii procesu.
Parametry boot są częścią transakcji instalatora, nie pętli renderującej.
