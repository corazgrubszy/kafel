# HttpClient

Mały, synchroniczny adapter GET nad cpp-httplib 0.61.0 i Mbed TLS 3.6.7.
Publiczne API (`include/net/http_client.hpp`) nie ujawnia typów tych bibliotek.
Każde wywołanie ma własny transport i zwraca posiadane dane: status, nagłówki
oraz treść albo błąd transportu. Nie zawiera retry, JSON ani logiki usług.
Nie jest zależnością dashboardu.

```cpp
net::HttpClient client; // systemowy zestaw CA
const auto result = client.get("https://example.org/data", {
    .timeout = std::chrono::milliseconds(3000),
    .max_body_bytes = 65536,
});
if (result) {
    // Sprawdź result->status przed interpretacją result->body.
} else {
    // result.error().code oraz result.error().message
}
```

Weryfikacja łańcucha certyfikatów i nazwy hosta jest obowiązkowa. Można wskazać
własny plik CA w konstruktorze. Zegar systemowy musi być poprawny. HTTP jest
również obsługiwane; nie zapewnia ochrony TLS. Przekierowania nie są wykonywane,
kompresja jest wyłączona; statusy HTTP, także 404/503, są zwykłymi odpowiedziami.
Limit dotyczy treści odpowiedzi, również chunked i bez Content-Length; nie jest
limitem całej pamięci procesu (TLS, nagłówki i pojemność stringa kosztują osobno).

Timeout obejmuje operację sieciową, w tym handshake i powolne przesyłanie treści.
To nie jest twarda gwarancja czasu rzeczywistego: rozwiązywanie DNS zależy od
backendu systemowego cpp-httplib, z granulacją pełnych sekund; na glibc anulowanie
resolvera może dodatkowo opóźnić powrót. Błędy DNS są raportowane jako connection,
bez zgadywania przyczyny. Jednorazowe wywołania nie utrzymują puli połączeń.

## Build i testy

Z katalogu głównego repo:

```sh
zig build --build-file libs/http-client/build.zig -Doptimize=ReleaseSmall
zig build --build-file libs/http-client/build.zig test -Doptimize=ReleaseSmall
```

Testy wymagają Python 3 z modułem `cryptography`, wyłącznie do generowania
jednorazowych certyfikatów. Interpreter można wskazać przez `-Dpython=/path/python3`.
Serwer testowy wymaga obsługi TLS 1.3 w Pythonie. Testy nie używają Internetu.
41 przypadków obejmuje TLS 1.2/1.3, RSA, ECDSA, SHA-512 i intermediate CA;
odrzucanie certyfikatów wygasłych, przyszłych, obcych i ze złą nazwą hosta;
timeout handshake'u, oczekiwania na nagłówki i ciągłego powolnego body;
limity Content-Length/chunked/EOF, dokładną granicę limitu, statusy HTTP,
przekierowania, query, walidację argumentów i brak pliku CA.

## Pomiar CM4

```sh
KAFEL_HOST=user@cm4stack.local libs/http-client/run-cm4.sh
```

Adres SSH trzeba podać jawnie przez `KAFEL_HOST=user@host`; nie ma domyślnego
logowania ani urządzenia. Opcjonalne `PYTHON=/path/python3` wybiera interpreter
z pakietem `cryptography`; bez niego skrypt używa systemowego `python3`.
Skrypt buduje ReleaseSmall ARM64/musl, przesyła binarkę i jednorazowe fixture'y
do katalogu `/tmp`, wykonuje tę samą macierz testową i trzy zapytania Open-Meteo.
Zapisuje `results/cm4.log`, po czym usuwa zdalny katalog. Nie wymaga sudo,
nie instaluje binarki, nie zmienia InfoHub ani żadnej usługi.
Rozmiar dotyczy pełnej binarki probe; RSS i szczytowy HWM pochodzą z
`/proc/self/status` nowego procesu dla każdego żądania.

### Wyniki CM4 — 2026-10-17

ReleaseSmall ARM64/musl, Linux 6.18.50+rpt-rpi-v8. Wszystkie **41 testów**
przeszło na urządzeniu, w tym weryfikacja certyfikatów i timeouty.
Trzy niezależne procesy pobrały dane Open-Meteo przez HTTPS z systemowym CA;
każda odpowiedź miała HTTP 200.

| Pomiar | Wynik |
| --- | --- |
| Pełna binarka probe | 1 360 288 B (1328,4 KiB; 1,30 MiB) |
| Czas żądania, próby 1 / 2 / 3 | 242 / 223 / 220 ms |
| VmRSS, próby 1 / 2 / 3 | 1928 / 1932 / 1928 KiB |
| VmHWM, próby 1 / 2 / 3 | 1928 / 1932 / 1928 KiB |

SHA-256 zmierzonej binarki:
`e96816a1ef4b279e70026fc712419d53519440c1db70dcf7da642f4eb5894519`.

Mediana czasu wyniosła 223 ms, maksymalny zaraportowany HWM około 1,89 MiB.
To pomiar całego procesu probe, a nie samej biblioteki. RSS odczytano po
zakończeniu żądania; HWM jest szczytem raportowanym przez jądro. Trzy próby
opisują tę sesję sieciową, nie stanowią benchmarku długotrwałego demona.
Działający InfoHub i dashboard nie były podmieniane.
