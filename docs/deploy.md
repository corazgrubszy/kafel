# Wdrożenie CM4

Z katalogu Kafel wybierz aplikację:

```sh
zig build deploy-dashboard
zig build deploy-weather
zig build deploy-waste
```

`zig build deploy` aktualizuje cały zestaw, również pomijając niezmienione
aplikacje. Nie wykonuje operacji Git ani GitHub.

## Narzędzia na urządzeniu

Instalator i audyt wykonują kod przez `sudo python3` na CM4. Przy starcie
usługi systemd uruchamia natomiast statyczny program C++:
`ExecStartPre=+/opt/kafel/libexec/kafel-display-console isolate`.
Start dashboardu i ochrona LCD nie wymagają interpretera.
[Sposób izolacji i jej ograniczenia](cm4stack.md).

Przy aktualizacji z poprzedniego helpera instalator zachowuje skrypt
`display_console.py` i starą jednostkę w archiwum rollbacku. Instaluje nową
binarkę z prawami `0755`, usuwa stary skrypt i uruchamia dashboard. Nieudana
transakcja przywraca również poprzedni helper oraz jednostkę.

## Przebieg

Terminal pokazuje cztery etapy: testy i budowę ARM64, połączenie i porównanie,
instalację oraz kontrolę działania. Szczegóły testów, transferów i instalatora
trafiają do `deploy-results/<czas-UTC>/deploy.log`. Błąd zatrzymuje wdrożenie;
terminal wskazuje dziennik. Raport `report.json` rozróżnia `changed` i `unchanged`
oraz zawiera sprawdzone PID i gotowość zmienionych usług.
`untouched_services` zapisuje PID pozostałych aplikacji przed i po wdrożeniu,
a `display` — wynik izolacji i informację o potrzebnym reboot.

Każdy target testuje i buduje wyłącznie swoją aplikację oraz uruchamia wspólne
testy instalatora. Dashboard obejmuje także testy UI i protokołu. Target całego
zestawu wykonuje pełne testy. Wyjście testów jest zapisane w dzienniku.

Wpisz hasło SSH i sudo, kiedy terminal zapyta. Nie trzeba otwierać wcześniej
sesji SSH ani wykonywać `sudo -v`. Własne połączenie współdzielone jest
zamykane na końcu; jawnie podane istniejące połączenie pozostaje otwarte.

## Konfiguracja

Skopiuj `deploy.example.json` do ignorowanego `deploy.local.json`, ustaw `host`
i `user`. Można je nadpisać przez `-Dhost=... -Duser=...`. Konto musi być
istniejące i nie-rootowe. Opcjonalne `latitude` i `longitude` są napisami.
`framebuffer`, `touch`, `dashboard_socket` i `idle_timeout` sterują dashboardem
(jak [parametry CLI](configuration.md)). Trafiają do
`/etc/kafel/dashboard.env`. Domyślnie LCD i dotyk są wybierane automatycznie.
Zmiana gniazda wymaga wspólnego wdrożenia odbiorcy i zainstalowanych producentów.
`waste_schedule` wskazuje harmonogram. Szerokość usług nie jest konfigurowana:
DASH/5 używa dużych kompozycji 240×240.
Nie zapisuj prywatnej konfiguracji w plikach śledzonych przez Git.

Instalator zachowuje `/etc/kafel/weather.env` i `/etc/kafel/waste.env`, chyba
że ustawienia lokalne nadpisują odpowiednie wartości. Aktualizacja dashboardu
nie wymaga współrzędnych ani harmonogramu. Aktualizacja pogody nie czyta
harmonogramu. Aktualizacja Śmieci nie wymaga współrzędnych.

`deploy-waste` migruje starą ścieżkę
`/home/<user>/infohub/data/garbage_schedule.json` do
`/var/lib/kafel-waste/garbage_schedule.json` i aktualizuje `waste.env`.
Do etapu sprzątania oryginał pozostaje nietknięty; jego dodatkowa kopia trafia do lokalnego archiwum
rollbacku jako `waste-schedule-original.json`. Docelowy katalog ma prawa `0700`,
plik `0600` i właściciela zgodnego z kontem usługi. Instalator sprawdza format,
odczyt jako to konto i zgodność bajtów, a SHA-256 zapisuje w raporcie.
Konflikt różnych plików zatrzymuje instalację przed zmianą procesów.
Rollback zachowuje dane i przywraca poprzednią konfigurację. Kolejne wdrożenia
korzystają już z nowej ścieżki; własne ścieżki poza starym katalogiem są zachowane.
Ta migracja nie występuje przy `deploy-dashboard` ani `deploy-weather`.
Szczegóły i przygotowanie świeżej instalacji: [kafel-waste](../apps/waste/README.md).

## Pomijanie zmian i izolacja

Przed transferem porównujemy SHA-256 binarki z zainstalowaną. Identyczna
binarka jest kopiowana do staging lokalnie na CM4, bez ponownego przesyłania.
Instalator ponownie sprawdza jej sumę pod blokadą transakcji. Następnie porównuje
binarkę, treść/prawa/właściciela jednostki i konfiguracji oraz aktywność i autostart.
Jeśli wszystko się zgadza, nie przełącza wydania i nie restartuje usługi.
Zmiana samej konfiguracji lub jednostki także wymaga restartu właściciela.
Usługa zatrzymana albo pozbawiona autostartu jest uruchamiana i włączana.

Każda transakcja tworzy niezmienne wydanie w `/opt/kafel/releases/<hash>/bin`.
Wybrane binarki są aktualizowane, pozostałe zachowywane bajt w bajt. Atomowe
przełączenie `/opt/kafel/current` nie restartuje innych procesów. Aktualizowane
są tylko jednostki i konfiguracja zmienionych aplikacji. Wspólna blokada
`/run/lock/kafel-deploy.lock` nie pozwala na równoległe instalacje.

Usługi noszą nazwy `kafel-dashboard.service`, `kafel-weather.service` oraz
`kafel-waste.service`; szablony są w `packaging/systemd`. Autostart jest włączony.
Dashboard działa z grupami `video input`, producenci jako to samo konto.
Nie używamy `PrivateTmp=yes`: transport korzysta ze wspólnego gniazda
`/tmp/kafel-<uid>/dashboard.sock` (DASH/5, lokalny Unix stream).
Pierwsza instalacja wymaga `zig build deploy`, czyli wszystkich trzech aplikacji.
Osobne targety służą do aktualizacji już zainstalowanego zestawu; producent
wymaga działającego dashboardu.
Dawne jednostki `dashboard.service` i `infohub.service` muszą być już usunięte;
nowe targety nie wykonują ponownie historycznej migracji.

## Kontrola i odzyskiwanie

Przed zatrzymaniem sprawdzamy sumy, wersje i konfigurację. Dla dashboardu
sprawdzamy dotyk, a po zatrzymaniu starego procesu także framebuffer.
Po instalacji sprawdzamy stabilny PID, aktywność oraz marker READY zmienionych
usług. Nowy dashboard oczekuje na heartbeat zainstalowanych producentów.
Producent musi potwierdzić akceptację snapshotu przez dashboard po ostatnim
wyniku źródła i po ewentualnym błędzie publikacji. Sam marker READY, wczytanie
harmonogramu ani wcześniejszy ACK pustego kafla nie wystarczają. Na aktualny ACK instalator czeka
do 12 s po wstępnej kontroli stabilności, sprawdzając stały PID i aktywność procesu. Snapshot jawnego
błędu źródła jest poprawnym wynikiem; awaria Open-Meteo nie wymaga sukcesu HTTP.
Brak aktualnego ACK uruchamia rollback wyłącznie zmienianych aplikacji.
Przed startem rootowy `ExecStartPre` izoluje konsolę; renderer pozostaje
nie-rootowy. Dashboard deploy obejmuje też helper i jego sumę SHA-256.
Na Raspberry Pi z HDMI-A-1 do transakcji dołącza `cmdline.txt`: tryb HDMI
bez monitora, zachowanie root/serial i usunięcie błędnego mapowania.
Zmiana jest w archiwum i podlega rollbackowi. Reboot nie jest automatyczny.
Szczegóły i przywrócenie konsoli: [CM4Stack](cm4stack.md).
Awaria Open-Meteo nie blokuje wdrożenia: daemon publikuje błąd i ponawia pobranie.

Przy błędzie przywracamy poprzednią konfigurację, jednostki, wydanie oraz stan
wyłącznie usług objętych zmianą. Izolacja konsoli podczas normalnego stopu
i awarii pozostaje aktywna, aby tekst nie wrócił na LCD. Pozostałe procesy nie są zatrzymywane także
podczas rollbacku. Szczegóły przywracania są w dzienniku.
Archiwum `rollback.tar.gz` jest pobierane lokalnie przed usunięciem staging.
Może zawierać prywatną konfigurację; katalog ma prawa 0700, pliki 0600 i są
ignorowane przez Git. Przy braku zmian archiwum nie jest potrzebne.
Gdy archiwum nie daje się pobrać, staging pozostaje na CM4 i polecenie zgłasza
błąd ze ścieżką odzyskiwania. Po sukcesie pozostaje jedno bieżące wydanie;
błąd usunięcia starego wydania jest wypisywany jako ostrzeżenie.

## Testy lokalne bez urządzenia

```sh
zig build deploy-test
zig build test dashboard weather waste -Doptimize=ReleaseFast
```

Testy instalatora wykonują rzeczywistą logikę w izolowanym filesystemie
z podstawionym systemd: aktualizacja każdej aplikacji, brak zmian, zmiana
konfiguracji, rollback i zachowanie pozostałych procesów. Nie uruchamiają SSH.


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

## Pierwsza migracja do DASH/5

Uruchom `zig build deploy`, aktualizując dashboard, Pogodę i Śmieci razem.
Manifest deklaruje generację protokołu; instalator sprawdza `--version`
każdej wybranej i pozostawianej binarki. Odmawia częściowej migracji i mieszania
DASH/4 z DASH/5. Po tej migracji osobne deploy-* nadal pomijają niezmienione
moduły i nie restartują pozostałych procesów.

Stop odbywa się przed przełączeniem wydania, producenci przed odbiornikiem.
Start jest uporządkowany jednostkami systemd. Nie ma adaptera starszego
protokołu. Transakcja obejmuje wszystkie zmienione moduły; rollback odtwarza
ich poprzednie binarki i konfigurację. Harmonogram i jego backup nie są
częścią kompozycji i pozostają zachowane.

Domyślne nowe gniazdo: `/tmp/kafel-<uid>/dashboard.sock`, Unix stream.
Jawnie skonfigurowana wspólna ścieżka jest zachowywana; nie mieszamy na niej
starego i nowego procesu. Usunięto ustawienie `waste_span`/`KAFEL_WASTE_SPAN`.
Pakiety fontów, szablonów i SVG są osadzone w binarkach — żadnych dodatkowych
plików zasobów do ręcznego kopiowania na CM4.
