# Raport z projektu: Symulacja Parku Narodowego (Temat 7)

## Spis treści

1. [Założenia projektowe](#1-założenia-projektowe)
2. [Ogólny opis kodu](#2-ogólny-opis-kodu)
3. [Szczegółowy opis modułów](#3-szczegółowy-opis-modułów)
4. [Mechanizmy IPC i synchronizacji](#4-mechanizmy-ipc-i-synchronizacji)
5. [Obsługa sygnałów](#5-obsługa-sygnałów)
6. [Walidacja danych i obsługa błędów](#6-walidacja-danych-i-obsługa-błędów)
7. [Elementy specjalne](#7-elementy-specjalne)
8. [Testy funkcjonalne](#8-testy-funkcjonalne)
9. [Napotkane problemy](#9-napotkane-problemy)
10. [Podsumowanie](#10-podsumowanie)
11. [Linki do kodu](#11-linki-do-kodu)

---

## 1. Założenia projektowe

### 1.1. Parametry symulacji

| Parametr | Wartość | Opis |
|----------|---------|------|
| N (daily_visitor_limit) | Podawane przez użytkownika (1–30 000) | Dzienny limit osób w parku |
| M_GROUP_SIZE | 10 | Wielkość grupy turystycznej |
| X1_BRIDGE_CAP | 9 | Pojemność mostu wiszącego (X1 < M) |
| X2_TOWER_CAP | 18 | Pojemność wieży widokowej (X2 < 2M) |
| X3_FERRY_CAP | 12 | Pojemność promu (X3 < 1.5*M) |
| MAX_GROUPS | 15 | Maksymalna liczba jednoczesnych grup |
| TICKET_PRICE | 50 | Cena biletu w PLN |
| P (num_guides) | Podawane przez użytkownika (1–15) | Liczba przewodników |
| N_PARK_CAPACITY | 500 | Pojemność wewnętrznej kolejki/slotów turystów |

### 1.2. Założenia funkcjonalne

1. **Godziny otwarcia**: Park działa od Tp (moment uruchomienia) do Tk (Tp + czas podany przez użytkownika w sekundach). Po Tk nowi turyści są odrzucani.

2. **Limit dzienny N**: Do parku w danym dniu może wejść co najwyżej N osób. Po osiągnięciu limitu kolejni turyści są odrzucani.

3. **Wejście do parku**:
   - Dzieci poniżej 7 lat — wejście bezpłatne
   - VIP (5% szans, legitymacja PTTK) — wejście bezpłatne z pominięciem kolejki do kasy
   - Pozostali — bilet płatny (50 PLN), przechodzą przez kolejkę kasową

4. **Zwiedzanie**:
   - VIP ≥ 15 lat może zwiedzać park samodzielnie (bez grupy)
   - Pozostali zwiedzają w M-osobowych grupach pod opieką przewodnika
   - Dzieci < 15 lat wymagają opiekuna (dorosły ≥ 18 lat z grupy; w razie braku — przewodnik)

5. **Trasy** (losowy wybór):
   - Trasa 1: Kasa → Most (A) → Wieża (B) → Prom (C) → Kasa
   - Trasa 2: Kasa → Prom (C) → Wieża (B) → Most (A) → Kasa

6. **Wydłużenie czasu**: Jeśli w grupie są dzieci < 12 lat, czas przejścia między atrakcjami jest wydłużony o 50%.

### 1.3. Zasady atrakcji

#### Most wiszący (A)

- Maksymalnie X1 = 9 osób jednocześnie
- Ruch jednokierunkowy — w danej chwili wszyscy idą w tym samym kierunku
- Przewodnik wchodzi pierwszy i ustala kierunek (pod warunkiem, że nikt nie idzie z drugiej strony)
- Na moście mogą znajdować się turyści z kilku grup idących w tym samym kierunku
- Po przejściu przewodnik czeka na drugiej stronie, aż wszyscy z jego grupy przejdą
- Dzieci < 15 lat wchodzą pod opieką osoby dorosłej
- VIP czeka w kolejce jak wszyscy (brak priorytetu na moście)

#### Wieża widokowa (B)

- Maksymalnie X2 = 18 osób jednocześnie
- Dwie klatki schodowe — jedną się wchodzi, drugą schodzi
- Przewodnik NIE wchodzi na wieżę — czeka na dole, aż wszyscy z grupy zejdą
- Dzieci ≤ 5 lat i ich opiekunowie nie mogą wejść na wieżę
- Dzieci < 15 lat wchodzą pod opieką osoby dorosłej
- VIP omija kolejkę
- Na sygnał SIGUSR1 (wydany przez przewodnika) turyści będący na wieży natychmiast z niej schodzą

#### Prom (C)

- Maksymalnie X3 = 12 osób jednocześnie
- Kursuje w obie strony, pokonanie rzeki w jedną stronę zajmuje losowy czas
- Przewodnik wchodzi na prom jako pierwszy, potem członkowie grupy
- Na promie mogą znajdować się turyści z kilku grup
- Po zejściu z promu przewodnik czeka, aż wszyscy z grupy przepłyną
- Dzieci < 15 lat wchodzą pod opieką osoby dorosłej
- VIP omija kolejkę

#### Ewakuacja awaryjna (sygnał2)

- Na SIGUSR2 wydany przez przewodnika turyści z grupy natychmiast wracają do kasy, omijając wszystkie atrakcje

---

## 2. Ogólny opis kodu

### 2.1. Architektura systemu

Program napisany w C, składa się z 5 plików źródłowych i pliku Makefile:

| Plik | Opis |
|------|------|
| `common.h` | Nagłówek wspólny — stałe, struktury danych, funkcje inline |
| `main.c` | Proces główny — inicjalizacja IPC, zarządzanie procesami, cleanup |
| `kasjer.c` | Proces kasjera — rejestracja wejść/wyjść, logowanie do pliku |
| `przewodnik.c` | Procesy przewodników — tworzenie i prowadzenie grup |
| `turysta.c` | Procesy turystów — zwiedzanie parku, reakcja na sygnały |
| `Makefile` | Kompilacja (`make all`), uruchomienie (`make run`), czyszczenie (`make clean`) |

Symulacja działa na procesach — obowiązkowe użycie `fork()` i `exec()`. Każdy turysta, przewodnik i kasjer to osobny proces uruchamiany przez `fork()` + `execl()`.

### 2.2. Hierarchia procesów

```
MAIN (main.c)
├── KASJER (kasjer.c) — 1 proces
│   └── KASJER-FIFO (kasjer.c fork) — 1 proces potomny do obsługi FIFO
├── PRZEWODNIK-REPORTER (przewodnik.c tryb "reporter") — 1 proces
├── PRZEWODNIK 1..P (przewodnik.c) — P procesów
└── TURYSTA 1..N (turysta.c) — N procesów (generowane w pętli)
```

### 2.3. Komunikacja między procesami

| Nadawca | Odbiorca | Mechanizm | Dane | Cel |
|---------|----------|-----------|------|-----|
| Turysta | Kasjer | Kolejka komunikatów (MSG_TYPE_ENTRY) | ID, PID, wiek, VIP | Rejestracja wejścia |
| Turysta (VIP solo) | Reporter | Kolejka komunikatów (MSG_TYPE_EXIT_NOTICE) | ID, PID, wiek, VIP | Powiadomienie o wyjściu |
| Reporter | Kasjer | Kolejka komunikatów (MSG_TYPE_EXIT) | ID, PID, wiek, VIP | Przekazanie rejestracji wyjścia |
| Przewodnik | Kasjer | Kolejka komunikatów (MSG_TYPE_EXIT) | lista turystów | Raport wyjść grupy |
| Przewodnik | Kasjer-FIFO | FIFO (named pipe) | tekst raportu | Raport z wycieczki / awarii |
| Przewodnik | Turysta | Sygnał SIGUSR1 | — | Ewakuacja z wieży |
| Przewodnik | Turysta | Sygnał SIGUSR2 | — | Ewakuacja ogólna |
| Main | Kasjer | Sygnał SIGTERM | — | Zakończenie pracy |
| Main | Reporter | Sygnał SIGTERM | — | Zakończenie pracy |
| Main | Wszystkie | Sygnał SIGTERM (kill(0, SIGTERM)) | — | Cleanup przy wyjściu |

### 2.4. Współdzielone zasoby

**Pamięć dzielona** (`struct ParkSharedMemory`) przechowuje:
- Stan parku (czas otwarcia/zamknięcia, flagi zamknięcia, limit dzienny)
- Statystyki (liczba wejść, wyjść, przychód, bilety płatne/darmowe)
- Kolejkę wejściową turystów (tablica cykliczna z head/tail)
- Stany wszystkich grup (`struct GroupState[MAX_GROUPS]`)
- Stany atrakcji (most — kierunek i liczniki; wieża — licznik i lista odwiedzających; prom — kierunek, liczniki, kolejki VIP/normalne)

**Semafory System V** (jeden zestaw `TOTAL_SEMAPHORES` semaforów) służą do:
- Wzajemnego wykluczania (mutexy: kolejka, statystyki, most, wieża, prom, grupy, kasa)
- Ograniczania dostępu (limity pojemności: most, wieża, prom, schody w górę/w dół, kolejka, kasa)
- Synchronizacji grup (SEM_MEMBER_GO, SEM_GROUP_DONE, SEM_TOURIST_ASSIGNED, SEM_TOURIST_READ_DONE)
- Kolejkowania kierunkowego (SEM_BRIDGE_WAIT, SEM_FERRY_WAIT, SEM_FERRY_VIP_WAIT)
- Kolejkowania na wieżę (SEM_TOWER_VIP_WAIT, SEM_TOWER_NORMAL_WAIT)
- Synchronizacji z przewodnikiem (SEM_BRIDGE_GUIDE_READY, SEM_FERRY_GUIDE_READY)
- Sygnalizacji zdarzeń (SEM_PRZEWODNIK, SEM_ALL_DONE)
  
W `sem_lock/sem_unlock/sem_trylock` dla wybranych mutexów i semaforów pojemności używany jest `SEM_UNDO`, aby procesy nie blokowały się po awarii.

### 2.5. Cykl życia turysty

1. **Inicjalizacja**: losowanie atrybutów (wiek 3–70, VIP 5%)
2. **Sprawdzenie dostępności**: park otwarty? limit dzienny N nie przekroczony?
3. **Ścieżka VIP solo** (VIP ≥ 15 lat): pominięcie kolejki, samodzielne zwiedzanie 3 atrakcji, powrót do kasy
4. **Ścieżka grupowa** (pozostali):
   - Kolejka do kasy (nie-VIP) → opłata biletu
   - Wejście do kolejki grupowej → oczekiwanie na M osób
   - Przypisanie do grupy przez przewodnika (SEM_TOURIST_ASSIGNED)
   - Potwierdzenie odczytu (SEM_TOURIST_READ_DONE)
   - Oczekiwanie na start (SEM_MEMBER_GO)
   - Zwiedzanie 3 atrakcji wg trasy z synchronizacją (SEM_GROUP_DONE)
   - Powrót do kasy
5. **Zakończenie**: VIP solo wysyła powiadomienie o wyjściu (do reportera), turyści grupowi są raportowani przez przewodnika; aktualizacja statystyk, shmdt

### 2.6. Cykl życia przewodnika

1. **Oczekiwanie** na semaforze SEM_PRZEWODNIK
2. **Tworzenie grupy**: zajęcie slotu (SEM_GROUP_SLOTS), pobranie turystów z kolejki, przydzielenie opiekunów, losowanie trasy
3. **Prowadzenie wycieczki**: budzenie turystów (SEM_MEMBER_GO), dla każdej atrakcji:
   - Most: wchodzi jako pierwszy, ustala kierunek
   - Wieża: czeka na dole
   - Prom: wchodzi jako pierwszy
   - Czekanie na SEM_GROUP_DONE od wszystkich turystów
4. **Zakończenie**: raport do kasy (kolejka komunikatów) i FIFO, zwolnienie slotu

---

## 3. Szczegółowy opis modułów

### 3.1. common.h

Plik nagłówkowy zawierający wszystkie wspólne definicje:

**Stałe konfiguracyjne** — limity pojemności, czasy trwania atrakcji, fazy zwiedzania, identyfikatory atrakcji i kierunków.

**Indeksy semaforów** — zorganizowane w grupy:

| Zakres | Nazwa | Opis |
|--------|-------|------|
| 0 | SEM_PARK_LIMIT | Zarezerwowany (limit realizowany przez daily_entered_count) |
| 1 | SEM_PRZEWODNIK | Budzenie przewodników |
| 2–3 | SEM_QUEUE_MUTEX, SEM_STATS_MUTEX | Mutexy danych wspólnych |
| 4–8 | SEM_MOST_*, SEM_WIEZA_*, SEM_PROM_* | Mutexy i limity atrakcji |
| 11–26 | SEM_GROUP_DONE_BASE (11–25), SEM_GROUP_MUTEX (26) | Synchronizacja grup |
| 27–526 | SEM_TOURIST_ASSIGNED_BASE | Semafory przypisania turystów |
| 527–1026 | SEM_TOURIST_READ_DONE_BASE | Semafory potwierdzenia odczytu |
| 1027–1028 | SEM_BRIDGE_WAIT_KA/AK | Kolejkowanie kierunkowe mostu |
| 1029–1033 | SEM_FERRY_WAIT/VIP_WAIT, SEM_FERRY_CAP | Kolejkowanie promu |
| 1034–1048 | SEM_FERRY_GUIDE_READY_BASE | Gotowość przewodnika na promie |
| 1049–1198 | SEM_MEMBER_GO_BASE | Budzenie członków grup |
| 1199–1223 | SEM_QUEUE_SLOTS, SEM_GROUP_SLOTS, SEM_TOWER_*, SEM_CASH_QUEUE_*, SEM_BRIDGE_GUIDE_READY_BASE, SEM_ALL_DONE | Pozostałe |

**Struktura komunikatu** (`struct msg_buffer`) — typ, ID turysty, PID, wiek, VIP, info.

**Struktura grupy** (`struct GroupState`) — dane aktywnej grupy: trasa, skład, opiekunowie, flagi sygnałów.

**Struktura pamięci dzielonej** (`struct ParkSharedMemory`) — kompletny stan parku.

**Funkcje inline**:
- `report_error()`, `fatal_error()` — obsługa błędów z `perror()`
- `sem_use_undo()` — określenie, które semafory używają `SEM_UNDO`
- `sem_lock()`, `sem_unlock()`, `sem_trylock()`, `sem_getval()` — operacje na semaforach (z `SEM_UNDO` dla wybranych semaforów)
- `sem_lock_interruptible()` — opuszczenie z obsługą przerwania (EINTR + flaga)
- `sem_timed_wait()` — czekanie z timeoutem (`semtimedop`)
- `msgsnd_retry()` — wysyłanie do kolejki z ponowieniem po `EINTR`
- `ferry_enter()`, `ferry_leave()` — logika kolejkowania na promie
- `sim_sleep()` — symulacja czasu z wydłużeniem 50% dla dzieci < 12 lat
- `get_timestamp()` — pobranie aktualnego czasu jako string (format `HH:MM:SS`)
- `tower_add/remove/has_visitor()` — zarządzanie listą odwiedzających wieżę
- `get_attraction_for_step()`, `get_bridge/ferry_direction()` — helpery tras

### 3.2. main.c

**Główne funkcje:**

| Funkcja | Opis |
|---------|------|
| `cleanup()` | Sprzątanie zasobów IPC (FIFO, kolejki, shm, sem) + SIGTERM do grupy procesów |
| `handle_sigint()` | Obsługa Ctrl+C — bezpieczne wypisanie i exit (wywołuje atexit) |
| `handle_sigchld()` | Zbieranie zombie procesów — `waitpid(-1, WNOHANG)` |
| `handle_sigalrm()` | Pusty handler do wybudzenia sigsuspend |
| `get_input()` | Walidacja danych od użytkownika (zakres, typ) |
| `init_semaphores()` | Inicjalizacja wartości semaforów (`semctl SETVAL`) |
| `init_shared_memory()` | Zerowanie i inicjalizacja pamięci dzielonej |
| `cleanup_old_ipc()` | Usuwanie zasobów IPC z poprzednich uruchomień |

**Przepływ main():**
1. `cleanup_old_ipc()` — usunięcie starych zasobów
2. `atexit(cleanup)` — rejestracja funkcji sprzątającej
3. Rejestracja handlerów: SIGINT, SIGCHLD, SIGALRM
4. Pobranie parametrów: liczba turystów, przewodników, czas, limit N
5. Walidacja konfiguracji (np. X3_FERRY_CAP vs M_GROUP_SIZE)
6. Tworzenie IPC: `shmget`, `semget`, `msgget` (×2), `mkfifo`
7. `fork()` + `execl()` kasjera
8. `fork()` + `execl()` przewodnika-reportera
9. `fork()` + `execl()` P przewodników (w pętli)
10. Pętla generowania N turystów: `fork()` + `execl()` z obsługą EAGAIN/ENOMEM
11. Oczekiwanie na zakończenie (SEM_ALL_DONE)
12. `kill(SIGTERM)` do reportera i kasjera
13. Wyświetlenie i zapis statystyk do `park_log.txt`
14. `shmdt()`, cleanup przez atexit

### 3.3. kasjer.c

**Architektura dwuprocesowa:**
- Proces główny: odbiera komunikaty z kolejki komunikatów (MSG_TYPE_ENTRY, MSG_TYPE_EXIT)
- Proces potomny (fork): czyta raporty przewodników z FIFO

**Przepływ:**
1. Dołączenie do IPC (`shmget`, `shmat`, `semget`, `msgget`)
2. Otwarcie pliku logu `park_log.txt` (`open` z O_WRONLY | O_CREAT | O_APPEND)
3. `fork()` procesu FIFO:
   - Potomny: `open(FIFO_PATH, O_RDWR)` + pętla `read()` + logowanie
   - Główny: pętla `msgrcv()`:
     - MSG_TYPE_ENTRY → logowanie wejścia, aktualizacja statystyk, budzenie przewodnika dla niepełnej grupy
     - MSG_TYPE_EXIT → logowanie wyjścia, SEM_ALL_DONE gdy wszyscy wyszli
4. Przy SIGTERM: `IPC_NOWAIT`, opróżnienie kolejki, `kill(SIGTERM)` do procesu FIFO

### 3.4. przewodnik.c

**Dwa tryby pracy:**
1. **Tryb reporter** (argument `"reporter"`) — pętla `msgrcv()` na kolejce raportowej, przekazuje MSG_TYPE_EXIT_NOTICE → MSG_TYPE_EXIT do kasy
2. **Tryb normalny** — prowadzenie grup turystycznych

**Kluczowe funkcje:**

| Funkcja | Opis |
|---------|------|
| `run_exit_reporter()` | Tryb reportera: przekazuje powiadomienia wyjść do kasy |
| `send_emergency_exit()` | Wysyła SIGUSR2 do wszystkich w grupie |
| `send_tower_evacuation()` | Wysyła SIGUSR1 do wszystkich w grupie |
| `send_exit_list_to_cashier()` | Wysyła MSG_TYPE_EXIT dla każdego turysty z grupy |
| `find_free_group_slot()` | Szuka wolnego slotu w tablicy grup |
| `guide_enter_bridge()` | Logika wejścia przewodnika na most |
| `guide_take_ferry()` | Logika wejścia przewodnika na prom |

**Logika tworzenia grupy:**
- Przydzielenie opiekunów: dorosły ≥ 18 lat → opiekun dziecka < 15 lat
- Brak opiekuna → przewodnik przejmuje rolę opiekuna (dziecko pod opieką przewodnika nie wchodzi na wieżę)
- Losowe awarie (2% szans): przed startem lub między atrakcjami → SIGUSR2 + raport FIFO
- Losowa ewakuacja wieży (3% szans) → SIGUSR1

### 3.5. turysta.c

**Handlery sygnałów** (async-signal-safe, używają `write()` zamiast `printf()`):
- `sigusr1_handler()` — ustawia `tower_evacuation_flag`
- `sigusr2_handler()` — ustawia `emergency_exit_flag`
- `sigterm_handler()` — ustawia `sigterm_flag` + `emergency_exit_flag`

**Kluczowe funkcje:**

| Funkcja | Opis |
|---------|------|
| `int_to_str()` | Konwersja int na string (async-signal-safe, dla handlerów) |
| `tourist_error_ctx()` | Formatowanie kontekstu błędu z ID i PID turysty |
| `wake_guides_for_count()` | Obliczanie i budzenie odpowiedniej liczby przewodników |
| `enter_park_and_report()` | Aktualizacja statystyk + MSG_TYPE_ENTRY do kasy |
| `reject_if_closed_late()` | Odrzucenie turysty po Tk z korektą statystyk |
| `wake_guides_for_queue()` | Pobranie wielkości kolejki + wywołanie `wake_guides_for_count()` |
| `tower_acquire_slot()` | Zajęcie miejsca na wieży z priorytetem VIP |
| `tower_release_slot()` | Zwolnienie miejsca + budzenie czekających (VIP priorytet) |
| `do_bridge()` | Most: kolejkowanie kierunkowe, zmiana kierunku po opróżnieniu |
| `do_tower()` | Wieża: ograniczenia wiekowe, schody, sem_timed_wait z obsługą SIGUSR1 |
| `do_ferry()` | Prom: czekanie na przewodnika, ferry_enter z kolejkowaniem |
| `do_ferry_vip()` | Prom dla VIP solo: bezpośrednie ferry_enter z priorytetem |

---

## 4. Mechanizmy IPC i synchronizacji

### 4.1. Pamięć dzielona (System V Shared Memory)

```c
// tworzenie (main.c)
shm_id = shmget(ftok(FTOK_PATH, FTOK_SHM_ID), sizeof(struct ParkSharedMemory), IPC_CREAT | 0600);

// dolaczenie (kazdy proces)
struct ParkSharedMemory *park = (struct ParkSharedMemory*)shmat(shm_id, NULL, 0);

// odlaczenie (przy zakonczeniu)
shmdt(park);

// usuniecie (cleanup w main.c)
shmctl(shm_id, IPC_RMID, NULL);
```

**Zastosowanie:** Przechowywanie całego stanu parku — kolejka turystów, grupy, atrakcje, statystyki. Minimalne prawa dostępu: `0600`.

### 4.2. Semafory (System V)

```c
// tworzenie (main.c)
sem_id = semget(ftok(FTOK_PATH, FTOK_SEM_ID), TOTAL_SEMAPHORES, IPC_CREAT | 0600);

// inicjalizacja (main.c init_semaphores)
union semun arg;
arg.val = initial_value;
semctl(sem_id, sem_num, SETVAL, arg);

// opuszczenie P (common.h sem_lock)
struct sembuf op = {sem_num, -1, sem_use_undo(sem_num) ? SEM_UNDO : 0};
semop(sem_id, &op, 1);

// podniesienie V (common.h sem_unlock)
struct sembuf op = {sem_num, 1, sem_use_undo(sem_num) ? SEM_UNDO : 0};
semop(sem_id, &op, 1);

// proba bez blokowania (common.h sem_trylock)
struct sembuf op = {sem_num, -1, IPC_NOWAIT | (sem_use_undo(sem_num) ? SEM_UNDO : 0)};
semop(sem_id, &op, 1);

// czekanie z timeoutem (common.h sem_timed_wait)
semtimedop(sem_id, &op, 1, &timeout);

// usuniecie (cleanup w main.c)
semctl(sem_id, 0, IPC_RMID);
```

**Typy semaforów:**

| Typ | Przykład | Zastosowanie |
|-----|----------|--------------|
| Mutex (binary, init=1) | SEM_QUEUE_MUTEX | Wyłączny dostęp do sekcji krytycznej |
| Counting (init=N) | SEM_MOST_LIMIT (init=9) | Ograniczenie liczby jednoczesnych dostępów |
| Event (init=0) | SEM_PRZEWODNIK | Sygnalizacja zdarzenia (producent-konsument) |
| Barrier (init=0) | SEM_GROUP_DONE | Synchronizacja grupy (czekanie na N zgłoszeń) |

### 4.3. Kolejki komunikatów (System V Message Queues)

```c
// tworzenie (main.c) — dwie kolejki
msg_id = msgget(ftok(FTOK_PATH, FTOK_MSG_ID), IPC_CREAT | 0600);
report_msg_id = msgget(ftok(FTOK_PATH, FTOK_MSG_REPORT_ID), IPC_CREAT | 0600);

// wysylanie (turysta.c, przewodnik.c) — wrapper w common.h
struct msg_buffer msg;
msg.msg_type = MSG_TYPE_ENTRY;  // lub MSG_TYPE_EXIT, MSG_TYPE_EXIT_NOTICE
msgsnd_retry(msg_id, &msg, sizeof(msg) - sizeof(long), 0);

// odbieranie (kasjer.c, przewodnik.c reporter)
msgrcv(msg_id, &msg, sizeof(msg) - sizeof(long), 0, flags);

// usuniecie (cleanup w main.c)
msgctl(msg_id, IPC_RMID, NULL);
```

**Zastosowanie:**
- Kolejka główna (`msg_id`): komunikacja turysta/przewodnik → kasjer (wejścia/wyjścia)
- Kolejka raportowa (`report_msg_id`): turysta → reporter → kasjer (powiadomienia o wyjściach VIP)
  
Wysyłanie realizuje `msgsnd_retry()` — ponawia `msgsnd()` po `EINTR`.

### 4.4. FIFO (łącze nazwane)

```c
// tworzenie (main.c)
mkfifo(FIFO_PATH, 0600);

// zapis (przewodnik.c)
int fifo_fd = open(FIFO_PATH, O_WRONLY);
write(fifo_fd, report, strlen(report));
close(fifo_fd);

// odczyt (kasjer.c — proces potomny)
int fifo_fd = open(FIFO_PATH, O_RDWR);
read(fifo_fd, buffer, sizeof(buffer) - 1);

// usuniecie (cleanup w main.c)
unlink(FIFO_PATH);
```

**Zastosowanie:** Raporty przewodników (zakończenie wycieczki, awarie) przesyłane do kasjera.

### 4.5. Procesy (fork + exec)

```c
// tworzenie (main.c)
pid_t pid = fork();
if (pid == 0) {
    execl("./program", "program", "arg1", NULL);
    fatal_error("Błąd execl");
}

// oczekiwanie
waitpid(pid, NULL, 0); // blokujace
waitpid(-1, NULL, WNOHANG); // nieblokujace (SIGCHLD handler)
```

---

## 5. Obsługa sygnałów

### 5.1. Zarejestrowane sygnały

| Sygnał | Proces | Handler | Działanie |
|--------|--------|---------|-----------|
| SIGINT | main | `handle_sigint()` | Ctrl+C → bezpieczne wypisanie + `exit(0)` → atexit cleanup |
| SIGCHLD | main | `handle_sigchld()` | Zbieranie zombie: `waitpid(-1, WNOHANG)` w pętli |
| SIGALRM | main | `handle_sigalrm()` | Pusty handler do wybudzenia blokujących wywołań |
| SIGTERM | kasjer | `sigterm_handler()` | Ustawienie flagi `shutdown_flag` |
| SIGTERM | przewodnik | `sigterm_handler()` | Ustawienie flagi `shutdown_flag` |
| SIGTERM | turysta | `sigterm_handler()` | Ustawienie `sigterm_flag` + `emergency_exit_flag` |
| SIGUSR1 | turysta | `sigusr1_handler()` | Ewakuacja z wieży: `tower_evacuation_flag = 1` |
| SIGUSR2 | turysta | `sigusr2_handler()` | Ewakuacja ogólna: `emergency_exit_flag = 1` |

### 5.2. Rejestracja handlerów (sigaction)

```c
struct sigaction sa;
sa.sa_handler = handler_function;
sigemptyset(&sa.sa_mask);
sa.sa_flags = 0;
sigaction(SIGNAL, &sa, NULL);
```

### 5.3. Async-signal-safe handlery

W handlerach sygnałów używane są wyłącznie bezpieczne funkcje. Zamiast `printf()` — ręczne budowanie stringa i `write(STDOUT_FILENO, ...)`:

```c
void sigusr1_handler(int sig) {
    (void)sig;
    tower_evacuation_flag = 1;
    char msg[128];
    int pos = 0;
    // reczne budowanie stringa (int_to_str)
    write(STDOUT_FILENO, msg, pos);
}
```

### 5.4. Wysyłanie sygnałów

```c
// SIGUSR1 do turysty (ewakuacja wiezy) — przewodnik.c
kill(pid, SIGUSR1);

// SIGUSR2 do turysty (ewakuacja ogolna) — przewodnik.c
kill(pid, SIGUSR2);

// SIGTERM do kasjera/reportera — main.c
kill(kasjer_pid, SIGTERM);

// SIGTERM do wszystkich procesow w grupie — main.c cleanup
kill(0, SIGTERM);
```

---

## 6. Walidacja danych i obsługa błędów

### 6.1. Walidacja wejścia użytkownika

Funkcja `get_input()` w `main.c` sprawdza:
- Czy wejście jest liczbą (`scanf("%d")`)
- Czy wartość mieści się w podanym zakresie `[min, max]`
- Czyści bufor stdin przy błędnym wejściu
- Pętla do skutku — użytkownik musi podać poprawną wartość

```c
int get_input(const char* prompt, int min, int max) {
    int value;
    while (1) {
        printf("%s (%d - %d): ", prompt, min, max);
        if (scanf("%d", &value) == 1) {
            if (value >= min && value <= max) return value;
            printf("Błąd: Wartość musi być z przedziału <%d, %d>!\n", min, max);
        } else {
            printf("Błąd: To nie jest liczba!\n");
            while (getchar() != '\n');
        }
    }
}
```

### 6.2. Walidacja konfiguracji

```c
if (X3_FERRY_CAP < M_GROUP_SIZE + 1) {
    fprintf(stderr, "[MAIN] Błąd konfiguracji: X3_FERRY_CAP (%d) < M_GROUP_SIZE+1 (%d)\n", ...);
    exit(1);
}
```

### 6.3. Obsługa błędów funkcji systemowych

Dwie funkcje pomocnicze (`common.h`):

```c
static inline void report_error(const char *context) {
    perror(context); // blad niekrytyczny
}

static inline void fatal_error(const char *context) {
    perror(context); // blad krytyczny
    exit(1);
}
```

Użycie w kodzie — każde wywołanie funkcji systemowej ma sprawdzany kod powrotu:
- `shmget`, `shmat`, `semget`, `msgget` → `fatal_error` przy -1
- `semop` → obsługa EINTR (wznowienie), reszta → `fatal_error`
- `fork` → obsługa EAGAIN/ENOMEM (próba odzyskania zasobów przez `waitpid`)
- `close`, `write`, `read` → `report_error` (kontynuacja pracy)

### 6.4. Obsługa braku zasobów na fork()

```c
pid_t pid = fork();
if (pid == -1) {
    if (errno == EAGAIN || errno == ENOMEM) {
        int reaped = 0;
        while (waitpid(-1, NULL, WNOHANG) > 0) {
            finished_tourists++;
            reaped++;
        }
        if (reaped > 0) { i--; continue; } // ponowna proba
    }
    report_error("[MAIN] Błąd fork");
}
```

---

## 7. Elementy specjalne

### 7.1. Kolorowanie wyjścia terminala (kody ANSI)

Każdy moduł ma przypisany kolor dla łatwej identyfikacji w logach:

| Kolor | Zastosowanie |
|-------|--------------|
| Zielony (`\033[0;32m`) | Przewodnik |
| Cyan (`\033[0;36m`) | Turysta |
| Żółty (`\033[0;33m`) | Kasjer, ostrzeżenia |
| Magenta (`\033[0;35m`) | VIP, wieża |
| Czerwony (`\033[0;31m`) | Błędy, ewakuacja |
| Biały (`\033[0;37m`) | Main, informacje systemowe |
| Czerwone tło (`\033[41m`) | Awarie |

### 7.2. System opieki nad dziećmi

- Automatyczne przydzielanie opiekunów: dorosły ≥ 18 lat → opiekun dziecka < 15 lat
- Jeśli brak dorosłego — przewodnik przejmuje rolę opiekuna
- Ograniczenia: dziecko ≤ 5 lat + opiekun nie wchodzą na wieżę
- Dziecko pod opieką przewodnika nie wchodzi na wieżę (przewodnik czeka na dole)

### 7.3. Losowe awarie i ewakuacje

- **Awaria (2% szans)**: przed startem wycieczki lub między atrakcjami → SIGUSR2 do grupy + raport FIFO
- **Ewakuacja wieży (3% szans)**: podczas pobytu grupy na wieży → SIGUSR1 do grupy

### 7.4. Wydłużenie czasu dla grup z dziećmi < 12 lat

```c
static inline void sim_sleep(int min_us, int max_us, int has_young_children) {
    int duration = min_us + (rand() % (max_us - min_us + 1));
    if (has_young_children) {
        duration = (int)(duration * 1.5); // +50%
    }
    usleep(duration);
}
```

### 7.5. Priorytet VIP

- **Na wieży**: VIP omija kolejkę. Normalni turyści czekają, gdy VIP oczekuje na wejście.
- **Na promie**: VIP omija kolejkę normalnych turystów. Oddzielne semafory: `SEM_FERRY_VIP_WAIT` i `SEM_FERRY_WAIT`.
- **Na moście**: VIP czeka jak wszyscy (zgodnie z wymaganiami).

### 7.6. Czyszczenie starych zasobów IPC

Przy starcie programu `cleanup_old_ipc()` sprawdza i usuwa zasoby z poprzednich uruchomień (kolejki, shm, sem, FIFO), zapobiegając konfliktom.

### 7.7. Zapis statystyk do pliku

Po zakończeniu symulacji statystyki są wyświetlane w terminalu i zapisywane do `park_log.txt` (otwierany przez `open()` z flagami `O_WRONLY | O_CREAT | O_APPEND`).

### 7.8. Niepełne grupy na koniec dnia

Po zamknięciu parku (Tk) lub osiągnięciu limitu N, jeśli w kolejce pozostają turyści w liczbie < M, przewodnik jest budzony aby zabrać niepełną grupę.

---

## 8. Testy funkcjonalne

### Test 1: Test przeciążeniowy – VIP-Dzieci (20 000 procesów)

**Cel:**  
Sprawdzenie stabilności systemu pod ekstremalnym obciążeniem oraz weryfikacja logiki darmowych biletów i priorytetów.

**Warunki początkowe:**
System uruchomiony z parametrem 20 000 turystów.
Wymuszenie w kodzie (lub losowanie), aby każdy turysta miał wiek < 16 lat oraz status VIP.
Pamięć współdzielona i semafory świeżo zainicjalizowane.

**Oczekiwany rezultat:**  
Brak zakleszczeń (deadlocków). Statystyki końcowe: Przychód = 0 PLN, Bilety płatne = 0, Wejścia darmowe = 20 000

### Wynik Testu Przeciążeniowego
```text
============== STATYSTYKI PARKU ==============
Godzina otwarcia (Tp):   12:59:25
Godzina zamknięcia (Tk): 12:59:55
Czas otwarcia:           30 sekund
Liczba przewodników:     15
Wygenerowani turyści:    20000
Weszło do parku:         20000
Wyszło z parku:          20000
Różnica (w parku):       0
Bilety płatne:           0
Wejścia darmowe VIP:     20000
Wejścia darmowe dzieci:  0
Nie stworzeni:           0
Odrzuceni po Tk:         0
Przychód (PLN):          0
----------------------------------------------
Status: Sukces - wszyscy weszli i wyszli z parku!
==============================================
```

**Rzeczywisty rezultat:**  
Taki jak oczekiwany.

**Status:** Zaliczony.

---

### Test 2: Test zatoru w kolejce

**Cel:**  
Sprawdzenie mechanizmu formowania grup i wydajności atrakcji w sytuacji ekstremalnego spiętrzenia turystów w kolejce.

**Warunki początkowe:**
Brak turystów VIP (wszyscy mają ten sam priorytet).

Sztuczne opóźnienie startu przewodników (sleep(30)).

Duża liczba turystów.

**Oczekiwany rezultat:**  
Przez pierwsze 60s liczba osób w kolejce rośnie liniowo.

Po 60s przewodnicy płynnie formują grupy zgodnie z limitem M_GROUP_SIZE.

Brak błędów synchronizacji przy gwałtownym dostępie do semaforów grup.

**Przebieg:**
```text
============== STATYSTYKI PARKU ==============
Godzina otwarcia (Tp):   13:07:34
Godzina zamknięcia (Tk): 13:09:14
Czas otwarcia:           100 sekund
Liczba przewodników:     10
Wygenerowani turyści:    20000
Weszło do parku:         20000
Wyszło z parku:          20000
Różnica (w parku):       0
Bilety płatne:           18848
Wejścia darmowe VIP:     0
Wejścia darmowe dzieci:  1152
Nie stworzeni:           0
Odrzuceni po Tk:         0
Przychód (PLN):          942400
----------------------------------------------
Status: Sukces - wszyscy weszli i wyszli z parku!
==============================================
```

**Rzeczywisty rezultat:**  
Taki jak oczekiwany.

**Status:** Zaliczony.

---

### Test 3: "Wąskie Gardło"

**Cel:**  
Weryfikacja odporności systemu na zakleszczenia (deadlocki) przy ekstremalnie ograniczonych zasobach i dużej konkurencji.

**Warunki początkowe:**
Pojemność wszystkich atrakcji ustawiona na minimum oraz grupy 1-osobowe.

```c
#define M_GROUP_SIZE 1
#define X1_BRIDGE_CAP 1
#define X2_TOWER_CAP 1 
#define X3_FERRY_CAP 2
```
Przewodnicy walczą o zasoby.

**Oczekiwany rezultat:**  
Brak trwałego zawieszenia procesów (każdy turysta ostatecznie przechodzi).

Statystyki wejść i wyjść zgadzają się co do jednego.

**Przebieg:**
```text
============== STATYSTYKI PARKU ==============
Godzina otwarcia (Tp):   13:20:29
Godzina zamknięcia (Tk): 13:22:09
Czas otwarcia:           100 sekund
Liczba przewodników:     10
Wygenerowani turyści:    20000
Weszło do parku:         20000
Wyszło z parku:          20000
Różnica (w parku):       0
Bilety płatne:           17850
Wejścia darmowe VIP:     1034
Wejścia darmowe dzieci:  1116
Nie stworzeni:           0
Odrzuceni po Tk:         0
Przychód (PLN):          892500
----------------------------------------------
Status: Sukces - wszyscy weszli i wyszli z parku!
==============================================
```


**Rzeczywisty rezultat:**  
Taki jak oczekiwany.

**Status:** Zaliczony.

---

### Test 4: "Dzień Pecha" (100% Awaryjności)

**Cel:**  
Weryfikacja szczelności procedur ewakuacyjnych i zwalniania zasobów w sytuacji, gdy każda grupa przerywa zwiedzanie w trakcie trasy. Test sprawdza, czy przy masowych przerwaniach nie powstają "wycieki" ludzi lub zasobów.

**Warunki początkowe:**
Zmiana w kodzie przewodnik.c: szansa na awarię w trakcie trasy ustawiona na 100%

**Oczekiwany rezultat:**  
Logi są czerwone od komunikatów [PRZEWODNIK] Awaria! Ewakuacja w trakcie trasy.

Turyści raportują SIGUSR2: Alarm! Natychmiastowy powrót do kasy!.

Mimo chaosu, liczba osób w parku na koniec wynosi 0.

**Przebieg:**
```text
============== STATYSTYKI PARKU ==============
Godzina otwarcia (Tp):   13:28:49
Godzina zamknięcia (Tk): 13:29:19
Czas otwarcia:           30 sekund
Liczba przewodników:     4
Wygenerowani turyści:    12345
Weszło do parku:         12345
Wyszło z parku:          12345
Różnica (w parku):       0
Bilety płatne:           11029
Wejścia darmowe VIP:     652
Wejścia darmowe dzieci:  664
Nie stworzeni:           0
Odrzuceni po Tk:         0
Przychód (PLN):          551450
----------------------------------------------
Status: Sukces - wszyscy weszli i wyszli z parku!
==============================================
```

**Rzeczywisty rezultat:**  
Taki jak oczekiwany.

**Status:** Zaliczony

---

### Test 5: "Strajk kasjera" (Przepełnienie Kolejki Komunikatów)

**Cel:**  
Weryfikacja stabilności mechanizmów IPC w sytuacji niedostępności odbiorcy wiadomości. Test sprawdza, czy funkcja systemowa msgsnd poprawnie blokuje procesy nadawcze w momencie zapełnienia systemowego bufora kolejki komunikatów, zapobiegając utracie danych i awarii programu.

**Warunki początkowe:**
Symulacja uruchomiona standardowo. W trakcie szczytowego obciążenia ręczne wysłanie sygnału zatrzymania do procesu Kasjera (kill -SIGSTOP [PID]), a po widocznym "zamrożeniu" systemu – wysłanie sygnału wznowienia (kill -SIGCONT [PID]).

**Oczekiwany rezultat:**  
Po wysłaniu SIGSTOP: Symulacja działa jeszcze przez chwilę (do momentu zapełnienia bufora jądra), a następnie całkowicie się zatrzymuje (procesy czekają na zwolnienie miejsca w kolejce).

Brak błędów krytycznych, segfaultów czy komunikatów o nieudanym wysłaniu wiadomości.

Po wysłaniu SIGCONT: Kasjer natychmiast "odtyka" kolejkę, następuje gwałtowny wysyp logów z zaległymi operacjami, a symulacja płynnie wraca do normalnego tempa.

Statystyki końcowe zgadzają się co do jednej osoby (brak zgubionych komunikatów).

**Przebieg:**
```text
============== STATYSTYKI PARKU ==============
Godzina otwarcia (Tp):   13:31:34
Godzina zamknięcia (Tk): 13:41:34
Czas otwarcia:           600 sekund
Limit dzienny (N):       20000
Liczba przewodników:     10
Wygenerowani turyści:    20000
Weszło do parku:         20000
Wyszło z parku:          20000
Różnica (w parku):       0
Bilety płatne:           17863
Wejścia darmowe VIP:     950
Wejścia darmowe dzieci:  1187
Nie stworzeni:           0
Odrzuceni po Tk:         0
Odrzuceni (limit N):     0
Przychód (PLN):          893150
----------------------------------------------
Status: Sukces - wszyscy weszli i wyszli z parku!
==============================================
```

**Rzeczywisty rezultat:**  
Taki jak oczekiwany.

**Status:** Zaliczony

---

### Test 6: Losowe Zatrzymania Przewodników

**Cel:**  
Weryfikacja odporności systemu na nagłe wstrzymanie procesu będącego w trakcie wykonywania zadań (ewentualnie wewnątrz sekcji krytycznej). Test sprawdza, czy mechanizmy synchronizacji poprawnie wstrzymują inne procesy zależne od zamrożonego zasobu oraz czy po wznowieniu pracy (SIGCONT) system powraca do stabilnego stanu bez utraty danych.

**Warunki początkowe:**
Symulacja uruchomiona w trybie standardowym. Test przeprowadzany manualnie z poziomu terminala systemu Linux. Brak modyfikacji w kodzie źródłowym – testujemy skompilowany, produkcyjny kod.

**Oczekiwany rezultat:**  
Po wysłaniu SIGSTOP (-19): Proces docelowy natychmiast przestaje generować logi. Jeśli proces trzymał zasób (np. był na moście), inne procesy próbujące uzyskać ten zasób wchodzą w stan oczekiwania. Nie występuje awaria programu ani błędy IPC.

Po wysłaniu SIGCONT (-18): Proces wznawia działanie dokładnie w punkcie zatrzymania. Zwolnienie zasobów następuje poprawnie, co odblokowuje kolejkę oczekujących.

Finał: Statystyki wejść i wyjść zgadzają się co do jednej osoby.

**Przebieg:**
```text
============== STATYSTYKI PARKU ==============
Godzina otwarcia (Tp):   13:54:30
Godzina zamknięcia (Tk): 13:59:30
Czas otwarcia:           300 sekund
Limit dzienny (N):       15000
Liczba przewodników:     10
Wygenerowani turyści:    15000
Weszło do parku:         15000
Wyszło z parku:          15000
Różnica (w parku):       0
Bilety płatne:           13404
Wejścia darmowe VIP:     711
Wejścia darmowe dzieci:  885
Nie stworzeni:           0
Odrzuceni po Tk:         0
Odrzuceni (limit N):     0
Przychód (PLN):          670200
----------------------------------------------
Status: Sukces - wszyscy weszli i wyszli z parku!
==============================================
```

**Rzeczywisty rezultat:**  
Taki jak oczekiwany.

**Status:** Zaliczony

---

### Test 7: "Wierny Opiekun" (Weryfikacja parowania Turysta-Turysta)

**Cel:**  
Weryfikacja mechanizmu dobierania opiekunów wewnątrz grupy zwiedzającej. Test ma wykazać, że w przypadku dostępności dorosłych turystów w danej grupie, system priorytetyzuje przypisanie dziecka do dorosłego turysty. Kluczowym elementem testu jest sprawdzenie trwałości tego przypisania przez wszystkie etapy wycieczki: Prom, Wieżę i Most.

**Warunki początkowe:**
Modyfikacja kodu: Wymuszenie wieku w turysta.c na podstawie parzystości ID (ID nieparzyste = 5 lat, ID parzyste = 30 lat). Zapewnia to "przeplatanie się" dzieci i dorosłych w kolejce wejściowej.

Parametry symulacji: 20 turystów (10 dzieci, 10 dorosłych), grupy 10-osobowe.

Oczekiwany efekt: Grupy składają się z miksu dzieci i dorosłych, co pozwala na ich sparowanie.

**Przebieg weryfikacji (Dowód z logów):**  
Analiza Pary 1: Dziecko T1 + Opiekun T4
```text
Start (Przypisanie): [T 1 | PID 386604] Mój opiekun: [T 4 | PID 386608]

Atrakcja 3 (Prom): [T 1 | PID 386604] Mam 5 lat - wsiadam na prom pod opieką [T 4 | PID 386608]

Atrakcja 2 (Wieża): [T 4 | PID 386608] Jestem opiekunem dziecka [T 1 | PID 386604] (wiek 5) - nie wejdę na wieżę (Potwierdzenie, że opiekun rezygnuje z wejścia, by pilnować dziecka)

Atrakcja 1 (Most): [T 1 | PID 386604] Mam 5 lat - idę przez most pod opieką [T 4 | PID 386608]
```

Analiza Pary 2: Dziecko T5 + Opiekun T8
```text
Start (Przypisanie): [T 5 | PID 386609] Mój opiekun: [T 8 | PID 386612]

Atrakcja 3 (Prom): [T 5 | PID 386609] Mam 5 lat - wsiadam na prom pod opieką [T 8 | PID 386612]

Atrakcja 2 (Wieża): [T 8 | PID 386612] Jestem opiekunem dziecka [T 5 | PID 386609] (wiek 5) - czekam na dole wieży.

Atrakcja 1 (Most): [T 5 | PID 386609] Mam 5 lat - idę przez most pod opieką [T 8 | PID 386612]
```

Analiza Pary 3: Dziecko T17 + Opiekun T16

```text
Start (Przypisanie): [T 17 | PID 386621] Mój opiekun: [T 16 | PID 386620]

Atrakcja 3 (Prom): [T 17 | PID 386621] Mam 5 lat - wsiadam na prom pod opieką [T 16 | PID 386620]

Atrakcja 2 (Wieża): [T 16 | PID 386620] Jestem opiekunem dziecka [T 17 | PID 386621] (wiek 5) - nie wejdę na wieżę

Atrakcja 1 (Most): [T 17 | PID 386621] Mam 5 lat - idę przez most pod opieką [T 16 | PID 386620]
```

**Rzeczywisty rezultat:**  
Test potwierdził poprawność algorytmu przydzielania opieki.

System poprawnie zidentyfikował dorosłych w grupie i przypisał ich do dzieci (Przewodnik nie musiał interweniować jako opiekun).

Relacja Opiekun-Dziecko jest trwała: ID opiekuna zapamiętane na początku wycieczki pozostaje niezmienne aż do wyjścia z parku.

Logika zachowania na Wieży (dorosły opiekun zostaje na dole z dzieckiem) zadziałała poprawnie.

**Status:** Zaliczony


---

## 9. Napotkane problemy

### 9.1. Duża liczba semaforów

**Problem:** System wymaga ~1200 semaforów (per-turysta + per-grupa + atrakcje), co może przekraczać limity systemowe.

**Rozwiązanie:** Sprawdzenie limitów (`cat /proc/sys/kernel/sem`) i ewentualna modyfikacja (`sysctl -w kernel.sem="250 32000 100 128"`).

### 9.2. Wyścigi przy obsłudze sygnałów

**Problem:** Handlery sygnałów muszą być async-signal-safe — nie mogą używać `printf()`, `malloc()` ani operacji na semaforach.

**Rozwiązanie:** Użycie `write(STDOUT_FILENO, ...)` z ręcznym budowaniem stringów (funkcja `int_to_str()` w `turysta.c`). Flagi ustawiane w handlerach to zmienne `volatile sig_atomic_t`.

### 9.3. Zombie procesy

**Problem:** Przy generowaniu tysięcy procesów turystów powstawały zombie procesy.

**Rozwiązanie:** Handler SIGCHLD z pętlą `waitpid(-1, NULL, WNOHANG)` zbierającą zakończone procesy. Dodatkowo w pętli generowania turystów — próba odzyskania zasobów przy EAGAIN.

### 9.4. Deadlock przy ewakuacji

**Problem:** Sygnał ewakuacji (SIGUSR1/SIGUSR2) mógł przyjść w trakcie blokującego `semop()`, powodując deadlock — turysta nigdy nie zgłaszał zakończenia atrakcji.

**Rozwiązanie:** Funkcja `sem_lock_interruptible()` sprawdza flagę przerwania po EINTR i zwraca -1, pozwalając turystowi pominąć atrakcję i zgłosić SEM_GROUP_DONE. Analogicznie `sem_timed_wait()` z obsługą dwóch flag.

### 9.5. Brak zasobów na fork()

**Problem:** Przy dużej liczbie turystów (20 000+) system mógł odmówić tworzenia nowych procesów (EAGAIN/ENOMEM).

**Rozwiązanie:** Obsługa błędu `fork()` z próbą odzyskania zasobów przez `waitpid(WNOHANG)`. Jeśli to nie pomaga — blokujące `waitpid()`. W ostateczności — kontynuacja z mniejszą liczbą turystów i korekta `total_expected`.

### 9.6. Niepełne grupy na koniec dnia

**Problem:** Po zamknięciu parku mogły pozostać osoby w kolejce (mniej niż M), które nigdy nie zostałyby obsłużone.

**Rozwiązanie:** Logika wykrywania w kasjera, main i turystach — budzenie przewodnika dla niepełnej grupy gdy `(all_entered == all_expected || park_closed) && queue_size > 0 && queue_size < M`.

---

## 10. Podsumowanie

### 10.1. Co udało się zrobić

- Pełna implementacja symulacji parku narodowego zgodna z opisem tematu 7
- Wszystkie trzy atrakcje z prawidłową logiką:
  - Most z ruchem jednokierunkowym i zmianą kierunku
  - Wieża z ograniczeniami wiekowymi, priorytetem VIP i ewakuacją (SIGUSR1)
  - Prom z kolejkowaniem kierunkowym i priorytetem VIP
- System grup z automatycznym przydzielaniem opiekunów
- Obsługa VIP (pominięcie kolejki, samodzielne zwiedzanie)
- Dwie trasy zwiedzania (losowy wybór)
- Wydłużenie czasu o 50% dla grup z dziećmi < 12 lat
- Pełna obsługa sygnałów (SIGINT, SIGCHLD, SIGALRM, SIGTERM, SIGUSR1, SIGUSR2)
- Walidacja danych wejściowych od użytkownika
- Obsługa błędów z `perror()` dla wszystkich funkcji systemowych
- Minimalne prawa dostępu do zasobów IPC (0600)
- Usunięcie zasobów IPC po zakończeniu (atexit + cleanup)
- Architektura procesowa z `fork()` + `exec()`
- Cztery mechanizmy IPC: pamięć dzielona, semafory, kolejki komunikatów, FIFO
- Logowanie do pliku `park_log.txt`
- Raportowanie statystyk końcowych
- Limit dzienny N osób w parku

### 10.2. Wykorzystane konstrukcje

1. **Tworzenie i obsługa procesów** — `fork()`, `execl()`, `exit()`, `waitpid()`
2. **Synchronizacja procesów (semafory)** — `semget()`, `semctl()`, `semop()`, `semtimedop()`
3. **Dwa mechanizmy komunikacji** — kolejki komunikatów (`msgget/msgsnd` przez `msgsnd_retry`/`msgrcv`) + pamięć dzielona (`shmget/shmat`)
4. **Obsługa sygnałów (dwa różne)** — SIGUSR1 (ewakuacja wieży) + SIGUSR2 (ewakuacja ogólna)

### 10.3. Elementy wyróżniające

- Kolorowanie wyjścia terminala (kody ANSI) — obrazujące działanie symulacji
- System opiekunów z możliwością przejęcia opieki przez przewodnika
- Losowe awarie z pełną ewakuacją
- Czyszczenie starych zasobów IPC przy starcie
- Obsługa braku zasobów na `fork()` (EAGAIN/ENOMEM)
- Async-signal-safe handlery sygnałów

---

## 11. Linki do kodu

Repozytorium: https://github.com/meneluero/Projekt-SO-Park-Narodowy

Commit: `80de208add08aebae62ea717a112f0583f62f87e`

### 11.1. Tworzenie i obsługa plików

| Funkcja | Lokalizacja | Opis |
|---------|-------------|------|
| `open()` | [kasjer.c#L65](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/kasjer.c#L65) | Otwarcie pliku logu `park_log.txt` |
| `open()` | [kasjer.c#L119](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/kasjer.c#L119) | Otwarcie FIFO do odczytu |
| `open()` | [przewodnik.c#L564](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/przewodnik.c#L564) | Otwarcie FIFO do zapisu (raport awarii) |
| `open()` | [przewodnik.c#L696](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/przewodnik.c#L696) | Otwarcie FIFO do zapisu (raport zakończenia) |
| `open()` | [main.c#L550](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/main.c#L550) | Otwarcie dummy FIFO (nonblock) |
| `open()` | [main.c#L802](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/main.c#L802) | Otwarcie logu do zapisu statystyk |
| `close()` | [kasjer.c#L154](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/kasjer.c#L154) | Zamknięcie FIFO |
| `close()` | [kasjer.c#L285](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/kasjer.c#L285) | Zamknięcie pliku logu |
| `close()` | [przewodnik.c#L583](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/przewodnik.c#L583) | Zamknięcie FIFO po raporcie awarii |
| `close()` | [przewodnik.c#L715](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/przewodnik.c#L715) | Zamknięcie FIFO po raporcie zakończenia |
| `close()` | [main.c#L25](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/main.c#L25) | Zamknięcie dummy FIFO (cleanup) |
| `close()` | [main.c#L857](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/main.c#L857) | Zamknięcie logu statystyk |
| `read()` | [kasjer.c#L128](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/kasjer.c#L128) | Odczyt raportów z FIFO |
| `write()` | [kasjer.c#L19](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/kasjer.c#L19) | Bezpieczne wypisanie w handlerze SIGTERM |
| `write()` | [kasjer.c#L27](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/kasjer.c#L27) | Zapis do pliku logu |
| `write()` | [przewodnik.c#L14](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/przewodnik.c#L14) | Bezpieczne wypisanie w handlerze SIGTERM |
| `write()` | [przewodnik.c#L579](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/przewodnik.c#L579) | Zapis raportu awarii do FIFO |
| `write()` | [przewodnik.c#L711](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/przewodnik.c#L711) | Zapis raportu zakończenia do FIFO |
| `write()` | [main.c#L114](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/main.c#L114) | Bezpieczne wypisanie w handlerze SIGINT |
| `write()` | [main.c#L853](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/main.c#L853) | Zapis statystyk do logu |
| `write()` | [turysta.c#L177](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/turysta.c#L177) | Bezpieczne wypisanie w handlerze SIGTERM |
| `write()` | [turysta.c#L196](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/turysta.c#L196) | Bezpieczne wypisanie w handlerze SIGUSR1 |
| `write()` | [turysta.c#L215](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/turysta.c#L215) | Bezpieczne wypisanie w handlerze SIGUSR2 |
| `unlink()` | [main.c#L33](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/main.c#L33) | Usunięcie FIFO (cleanup) |

### 11.2. Tworzenie procesów

| Funkcja | Lokalizacja | Opis |
|---------|-------------|------|
| `fork()` | [main.c#L558](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/main.c#L558) | Tworzenie procesu kasjera |
| `fork()` | [main.c#L569](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/main.c#L569) | Tworzenie procesu przewodnika-reportera |
| `fork()` | [main.c#L582](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/main.c#L582) | Tworzenie procesów przewodników (pętla) |
| `fork()` | [main.c#L638](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/main.c#L638) | Tworzenie procesów turystów (pętla) |
| `fork()` | [kasjer.c#L111](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/kasjer.c#L111) | Tworzenie procesu do obsługi FIFO |
| `execl()` | [main.c#L564](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/main.c#L564) | Uruchomienie programu `kasjer` |
| `execl()` | [main.c#L574](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/main.c#L574) | Uruchomienie programu `przewodnik` (reporter) |
| `execl()` | [main.c#L593](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/main.c#L593) | Uruchomienie programu `przewodnik` |
| `execl()` | [main.c#L671](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/main.c#L671) | Uruchomienie programu `turysta` |
| `exit()` | [common.h#L290](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/common.h#L290) | Zakończenie przy błędzie krytycznym (`fatal_error`) |
| `exit()` | [main.c#L117](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/main.c#L117) | Zakończenie w handlerze SIGINT |
| `waitpid()` | [main.c#L57](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/main.c#L57) | Czekanie na procesy potomne (cleanup) |
| `waitpid()` | [main.c#L131](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/main.c#L131) | Zbieranie zombie (SIGCHLD handler, WNOHANG) |
| `waitpid()` | [main.c#L643](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/main.c#L643) | Odzyskiwanie zasobów przy EAGAIN/ENOMEM |
| `waitpid()` | [main.c#L727](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/main.c#L727) | Czekanie na zakończenie reportera |
| `waitpid()` | [main.c#L739](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/main.c#L739) | Czekanie na zakończenie kasjera |
| `waitpid()` | [kasjer.c#L278](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/kasjer.c#L278) | Czekanie na zakończenie procesu FIFO |

### 11.3. Obsługa sygnałów

| Funkcja | Lokalizacja | Opis |
|---------|-------------|------|
| `sigaction()` | [main.c#L468](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/main.c#L468) | Rejestracja SIGINT |
| `sigaction()` | [main.c#L478](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/main.c#L478) | Rejestracja SIGCHLD |
| `sigaction()` | [main.c#L488](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/main.c#L488) | Rejestracja SIGALRM |
| `sigaction()` | [kasjer.c#L78](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/kasjer.c#L78) | Rejestracja SIGTERM (kasjer) |
| `sigaction()` | [przewodnik.c#L279](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/przewodnik.c#L279) | Rejestracja SIGTERM (reporter) |
| `sigaction()` | [przewodnik.c#L325](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/przewodnik.c#L325) | Rejestracja SIGTERM (przewodnik) |
| `sigaction()` | [turysta.c#L662](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/turysta.c#L662) | Rejestracja SIGUSR1 (turysta) |
| `sigaction()` | [turysta.c#L672](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/turysta.c#L672) | Rejestracja SIGUSR2 (turysta) |
| `sigaction()` | [turysta.c#L682](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/turysta.c#L682) | Rejestracja SIGTERM (turysta) |
| `sigemptyset()` | [main.c#L464](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/main.c#L464) | Inicjalizacja maski sygnałów |
| `sigemptyset()` | [turysta.c#L657](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/turysta.c#L657) | Inicjalizacja maski sygnałów |
| `kill()` | [przewodnik.c#L85](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/przewodnik.c#L85) | Wysłanie SIGUSR2 (ewakuacja ogólna) |
| `kill()` | [przewodnik.c#L105](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/przewodnik.c#L105) | Wysłanie SIGUSR1 (ewakuacja wieży) |
| `kill()` | [main.c#L51](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/main.c#L51) | Wysłanie SIGTERM do grupy procesów |
| `kill()` | [main.c#L724](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/main.c#L724) | Wysłanie SIGTERM do reportera |
| `kill()` | [main.c#L734](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/main.c#L734) | Wysłanie SIGTERM do kasjera |
| `kill()` | [kasjer.c#L273](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/kasjer.c#L273) | Wysłanie SIGTERM do procesu FIFO |
| `signal()` | [turysta.c#L163](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/turysta.c#L163) | Reset SIGTERM do SIG_DFL |

### 11.4. Synchronizacja procesów (semafory)

| Funkcja | Lokalizacja | Opis |
|---------|-------------|------|
| `ftok()` | [main.c#L511](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/main.c#L511) | Generowanie klucza IPC |
| `semget()` | [main.c#L524](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/main.c#L524) | Utworzenie zestawu semaforów |
| `semget()` | [kasjer.c#L53](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/kasjer.c#L53) | Dołączenie do semaforów (kasjer) |
| `semget()` | [przewodnik.c#L299](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/przewodnik.c#L299) | Dołączenie do semaforów (przewodnik) |
| `semget()` | [turysta.c#L703](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/turysta.c#L703) | Dołączenie do semaforów (turysta) |
| `semctl()` | [main.c#L184](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/main.c#L184) | Inicjalizacja semaforów (SETVAL) |
| `semctl()` | [main.c#L100](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/main.c#L100) | Usunięcie semaforów (IPC_RMID) |
| `semctl()` | [common.h#L394](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/common.h#L394) | Pobranie wartości semafora (GETVAL) |
| `semop()` | [common.h#L316](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/common.h#L316) | Operacja P — opuszczenie (`sem_lock`) |
| `semop()` | [common.h#L350](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/common.h#L350) | Operacja V — podniesienie (`sem_unlock`) |
| `semop()` | [common.h#L365](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/common.h#L365) | Próba bez blokowania (`sem_trylock`, IPC_NOWAIT) |
| `semop()` | [common.h#L331](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/common.h#L331) | Opuszczenie z obsługą przerwania (`sem_lock_interruptible`) |
| `semtimedop()` | [common.h#L403](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/common.h#L403) | Czekanie z timeoutem (`sem_timed_wait`) |

### 11.5. Łącza nazwane (FIFO)

| Funkcja | Lokalizacja | Opis |
|---------|-------------|------|
| `mkfifo()` | [main.c#L544](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/main.c#L544) | Utworzenie FIFO |
| `open()` | [przewodnik.c#L564](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/przewodnik.c#L564) | Otwarcie FIFO do zapisu (O_WRONLY) |
| `open()` | [kasjer.c#L119](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/kasjer.c#L119) | Otwarcie FIFO do odczytu (O_RDWR) |
| `write()` | [przewodnik.c#L579](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/przewodnik.c#L579) | Zapis raportu do FIFO |
| `read()` | [kasjer.c#L128](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/kasjer.c#L128) | Odczyt raportów z FIFO |
| `unlink()` | [main.c#L33](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/main.c#L33) | Usunięcie FIFO |

### 11.6. Pamięć dzielona

| Funkcja | Lokalizacja | Opis |
|---------|-------------|------|
| `ftok()` | [main.c#L511](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/main.c#L511) | Generowanie klucza IPC |
| `shmget()` | [main.c#L511](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/main.c#L511) | Utworzenie segmentu pamięci dzielonej |
| `shmget()` | [kasjer.c#L42](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/kasjer.c#L42) | Dołączenie (kasjer) |
| `shmget()` | [przewodnik.c#L289](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/przewodnik.c#L289) | Dołączenie (przewodnik) |
| `shmget()` | [turysta.c#L702](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/turysta.c#L702) | Dołączenie (turysta) |
| `shmat()` | [main.c#L517](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/main.c#L517) | Dołączenie do pamięci (main) |
| `shmat()` | [kasjer.c#L47](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/kasjer.c#L47) | Dołączenie do pamięci (kasjer) |
| `shmat()` | [przewodnik.c#L294](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/przewodnik.c#L294) | Dołączenie do pamięci (przewodnik) |
| `shmat()` | [turysta.c#L711](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/turysta.c#L711) | Dołączenie do pamięci (turysta) |
| `shmdt()` | [main.c#L863](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/main.c#L863) | Odłączenie od pamięci (main) |
| `shmdt()` | [kasjer.c#L288](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/kasjer.c#L288) | Odłączenie od pamięci (kasjer) |
| `shmdt()` | [przewodnik.c#L747](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/przewodnik.c#L747) | Odłączenie od pamięci (przewodnik) |
| `shmdt()` | [turysta.c#L1065](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/turysta.c#L1065) | Odłączenie od pamięci (turysta) |
| `shmctl()` | [main.c#L92](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/main.c#L92) | Usunięcie segmentu (IPC_RMID) |

### 11.7. Kolejki komunikatów

| Funkcja | Lokalizacja | Opis |
|---------|-------------|------|
| `ftok()` | [main.c#L532](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/main.c#L532) | Generowanie klucza IPC |
| `msgget()` | [main.c#L532](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/main.c#L532) | Utworzenie kolejki głównej |
| `msgget()` | [main.c#L538](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/main.c#L538) | Utworzenie kolejki raportowej |
| `msgget()` | [kasjer.c#L59](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/kasjer.c#L59) | Dołączenie do kolejki (kasjer) |
| `msgget()` | [przewodnik.c#L21](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/przewodnik.c#L21) | Dołączenie do kolejki raportowej (reporter) |
| `msgget()` | [przewodnik.c#L26](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/przewodnik.c#L26) | Dołączenie do kolejki głównej (reporter) |
| `msgget()` | [przewodnik.c#L304](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/przewodnik.c#L304) | Dołączenie do kolejki głównej (przewodnik) |
| `msgget()` | [turysta.c#L704](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/turysta.c#L704) | Dołączenie do kolejki głównej (turysta) |
| `msgget()` | [turysta.c#L705](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/turysta.c#L705) | Dołączenie do kolejki raportowej (turysta) |
| `msgsnd()` | [common.h#L383](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/common.h#L383) | Wysyłanie komunikatów (wrapper `msgsnd_retry`, używany przez turystę i przewodnika) |
| `msgrcv()` | [kasjer.c#L166](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/kasjer.c#L166) | Odbiór komunikatów (kasjer) |
| `msgrcv()` | [przewodnik.c#L39](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/przewodnik.c#L39) | Odbiór powiadomień o wyjściach (reporter) |
| `msgctl()` | [main.c#L74](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/main.c#L74) | Usunięcie kolejki głównej (IPC_RMID) |
| `msgctl()` | [main.c#L83](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/main.c#L83) | Usunięcie kolejki raportowej (IPC_RMID) |

### 11.8. Obsługa błędów

| Funkcja | Lokalizacja | Opis |
|---------|-------------|------|
| `perror()` | [common.h#L283](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/common.h#L283) | Funkcja `report_error()` — błąd niekrytyczny |
| `perror()` | [common.h#L288](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/common.h#L288) | Funkcja `fatal_error()` — błąd krytyczny |
| Walidacja danych | [main.c#L151](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/main.c#L151) | Funkcja `get_input()` — sprawdzenie zakresu i typu |
| Obsługa EINTR | [common.h#L322](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/common.h#L322) | Wznowienie `semop` po przerwaniu sygnałem |
| Obsługa EINTR | [common.h#L383](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/common.h#L383) | Ponowienie `msgsnd` w `msgsnd_retry()` |
| Obsługa EAGAIN | [main.c#L640](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/main.c#L640) | Odzyskiwanie zasobów przy `fork()` |
| Walidacja konfiguracji | [main.c#L502](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/80de208add08aebae62ea717a112f0583f62f87e/main.c#L502) | Sprawdzenie X3_FERRY_CAP vs M_GROUP_SIZE |

---

**Autor:** Wiktor Kościółek

**Data:** 06.02.2026

**Środowisko:** Ubuntu 24.04.1 LTS (WSL2)

**Kompilator:** GCC 13.3.0

**Repozytorium:** https://github.com/meneluero/Projekt-SO-Park-Narodowy
