# Raport z projektu: Symulacja Parku Narodowego

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
| N_PARK_CAPACITY | 500 | Maksymalna liczba osób w parku |
| M_GROUP_SIZE | 10 | Wielkość grupy turystycznej |
| X1_BRIDGE_CAP | 9 | Pojemność mostu (X1 < M) |
| X2_TOWER_CAP | 18 | Pojemność wieży (X2 < 2M) |
| X3_FERRY_CAP | 12 | Pojemność promu (X3 < 1.5*M) |
| MAX_GROUPS | 15 | Maksymalna liczba aktywnych grup |
| TICKET_PRICE | 50 | Cena biletu w PLN |

### 1.2. Założenia funkcjonalne

1. **Godziny otwarcia**: Park działa od czasu Tp (uruchomienie) do Tk (podany przez użytkownika czas w sekundach).

2. **Wejście do parku**:
   - Dzieci poniżej 7 lat - wejście bezpłatne
   - VIP (5% szans) - wejście bezpłatne z pominięciem kolejki
   - Pozostali - bilet płatny (50 PLN)

3. **Zwiedzanie**:
   - VIP ≥15 lat może zwiedzać samodzielnie
   - Pozostali zwiedzają w grupach M-osobowych pod opieką przewodnika
   - Dzieci <15 lat wymagają opiekuna (dorosły z grupy lub przewodnik)

4. **Trasy**:
   - Trasa 1: Kasa → Most → Wieża → Prom → Kasa
   - Trasa 2: Kasa → Prom → Wieża → Most → Kasa
   - Wybór trasy jest losowy dla każdej grupy

5. **Wydłużenie czasu**: Jeśli w grupie są dzieci <12 lat, czas przejścia między atrakcjami jest wydłużony o 50%.

### 1.3. Zasady atrakcji

#### Most wiszący (A)

- Maksymalnie X1=9 osób jednocześnie
- Ruch jednokierunkowy w danej chwili
- Przewodnik wchodzi pierwszy (jeśli nikt nie idzie z przeciwnej strony)
- Na moście mogą być turyści z różnych grup idących w tym samym kierunku
- Dzieci <15 lat wchodzą pod opieką dorosłego
- VIP czeka w kolejce jak pozostali

#### Wieża widokowa (B)

- Maksymalnie X2=18 osób jednocześnie
- Dwie klatki schodowe (jedna do wchodzenia, druga do schodzenia)
- Przewodnik NIE wchodzi na wieżę - czeka na dole
- Dzieci ≤5 lat i ich opiekunowie nie mogą wejść
- Dzieci <15 lat wchodzą pod opieką dorosłego
- VIP omija kolejkę
- Na sygnał SIGUSR1 turyści natychmiast schodzą z wieży

#### Prom (C)

- Maksymalnie X3=12 osób jednocześnie
- Kursuje w obie strony
- Przewodnik wchodzi pierwszy, potem członkowie grupy
- Na promie mogą być turyści z różnych grup
- Dzieci <15 lat wchodzą pod opieką dorosłego
- VIP omija kolejkę

#### Ewakuacja awaryjna

- Na sygnał SIGUSR2 turyści z grupy natychmiast wracają do kasy

---

## 2. Ogólny opis kodu

### 2.1. Architektura systemu

System składa się z 5 plików źródłowych:

| Plik | Opis |
|------|------|
| common.h | Nagłówek wspólny - stałe, struktury, funkcje inline |
| main.c | Proces główny - inicjalizacja i zarządzanie |
| kasjer.c | Proces kasjera - obsługa wejść/wyjść i logów |
| przewodnik.c | Procesy przewodników - prowadzenie grup |
| turysta.c | Procesy turystów - zwiedzanie parku |

### 2.2. Schemat działania

#### Hierarchia procesów

System działa w oparciu o następującą hierarchię procesów:

1. **MAIN (main.c)** - proces nadrzędny
   - Tworzy wszystkie zasoby IPC
   - Uruchamia procesy potomne przez fork() + exec()
   - Generuje procesy turystów
   - Czeka na zakończenie symulacji
   - Wykonuje cleanup

2. **KASJER (kasjer.c)** - 1 proces
   - Odbiera komunikaty o wejściach/wyjściach
   - Zapisuje logi do pliku
   - Posiada proces potomny do obsługi FIFO

3. **PRZEWODNIK (przewodnik.c)** - P procesów
   - Tworzy i prowadzi grupy turystyczne
   - Synchronizuje przejście przez atrakcje
   - Wysyła sygnały ewakuacji

4. **PRZEWODNIK-REPORTER (przewodnik.c)** - 1 proces
   - Specjalny tryb przewodnika
   - Przekazuje powiadomienia o wyjściach do kasjera

5. **TURYSTA (turysta.c)** - N procesów
   - Symuluje zachowanie turysty
   - Reaguje na sygnały ewakuacji

#### Komunikacja między procesami

| Nadawca | Odbiorca | Mechanizm | Typ wiadomości | Cel |
|---------|----------|-----------|----------------|-----|
| Turysta | Kasjer | Kolejka komunikatów | MSG_TYPE_ENTRY | Rejestracja wejścia |
| Turysta | Reporter | Kolejka komunikatów | MSG_TYPE_EXIT_NOTICE | Powiadomienie o wyjściu |
| Reporter | Kasjer | Kolejka komunikatów | MSG_TYPE_EXIT | Rejestracja wyjścia |
| Przewodnik | Kasjer | FIFO | Tekst | Raport z wycieczki |
| Przewodnik | Turysta | Sygnał SIGUSR1 | - | Ewakuacja z wieży |
| Przewodnik | Turysta | Sygnał SIGUSR2 | - | Ewakuacja ogólna |
| Main | Kasjer | Sygnał SIGTERM | - | Zakończenie pracy |
| Main | Przewodnik | Sygnał SIGTERM | - | Zakończenie pracy |

#### Współdzielone zasoby

**Pamięć dzielona** przechowuje:
- Stan parku (czas otwarcia/zamknięcia, flaga zamknięcia)
- Statystyki (liczba wejść, wyjść, przychód)
- Kolejkę wejściową turystów (tablica cykliczna)
- Stany wszystkich grup (tablica GroupState)
- Stany atrakcji (most, wieża, prom)

**Semafory** służą do:
- Wzajemnego wykluczania (mutexy dla kolejki, statystyk, atrakcji)
- Ograniczania dostępu (limity pojemności parku, mostu, wieży, promu)
- Synchronizacji grup (oczekiwanie na zakończenie atrakcji)
- Kolejkowania (oczekiwanie na zmianę kierunku mostu/promu)

#### Cykl życia turysty

1. **Inicjalizacja**: Losowanie atrybutów (wiek, VIP)
2. **Sprawdzenie dostępności**: Czy park jest otwarty?
3. **Ścieżka VIP solo** (jeśli VIP ≥15 lat):
   - Pominięcie kolejki
   - Samodzielne zwiedzanie wszystkich atrakcji
   - Powrót do kasy
4. **Ścieżka grupowa** (pozostali):
   - Kolejka do kasy (płatność dla nie-VIP ≥7 lat)
   - Kolejka grupowa (oczekiwanie na M osób)
   - Przypisanie do grupy przez przewodnika
   - Oczekiwanie na start wycieczki
   - Zwiedzanie 3 atrakcji według trasy
   - Powrót do kasy z grupą
5. **Zakończenie**: Zgłoszenie wyjścia, zwolnienie zasobów

#### Cykl życia przewodnika

1. **Oczekiwanie**: Czeka na semaforze SEM_PRZEWODNIK
2. **Tworzenie grupy**:
   - Zajmuje slot grupy
   - Pobiera turystów z kolejki
   - Przydziela opiekunów dla dzieci <15 lat
   - Losuje trasę (1 lub 2)
3. **Prowadzenie wycieczki**:
   - Budzi turystów (SEM_MEMBER_GO)
   - Dla każdej z 3 atrakcji:
     - Most/Prom: wchodzi jako pierwszy, wpuszcza grupę
     - Wieża: czeka na dole
     - Czeka na zakończenie przez wszystkich (SEM_GROUP_DONE)
4. **Zakończenie**:
   - Raportuje do kasy i FIFO
   - Zwalnia slot grupy
   - Wraca do kroku 1

#### Logika atrakcji

**Most wiszący:**
- Ruch jednokierunkowy - w danej chwili wszyscy idą w tym samym kierunku
- Przewodnik wchodzi pierwszy, ustala kierunek
- Gdy most pusty, kierunek może się zmienić
- Priorytet dla czekających z przeciwnej strony (po opróżnieniu mostu)
- VIP nie ma priorytetu (czeka jak wszyscy)

**Wieża widokowa:**
- Przewodnik nie wchodzi - czeka na dole
- Ograniczenia wiekowe: dzieci ≤5 lat nie wchodzą, ich opiekunowie też
- VIP ma priorytet - omija kolejkę zwykłych turystów
- Na sygnał SIGUSR1 turyści natychmiast schodzą
- Osobne semafory dla schodów w górę i w dół

**Prom:**
- Przewodnik wchodzi pierwszy, ustala kierunek
- VIP ma priorytet - omija kolejkę zwykłych turystów
- Po opróżnieniu promu priorytet dla czekających z przeciwnej strony
- Może przewozić turystów z różnych grup (w tym samym kierunku)

#### Obsługa sytuacji specjalnych

**Zamknięcie parku (czas Tk):**
- Nowi turyści są odrzucani
- Turyści w kolejce tworzą niepełną grupę
- Przewodnik jest budzony dla niepełnej grupy

**Ewakuacja z wieży (SIGUSR1):**
- Ustawia flagę tower_evacuation_flag
- Przerywa sem_timed_wait na wieży
- Turysta natychmiast schodzi

**Ewakuacja ogólna (SIGUSR2):**
- Ustawia flagę emergency_exit_flag
- Przerywa wszystkie sem_lock_interruptible
- Turysta pomija pozostałe atrakcje i wraca do kasy

**Brak opiekuna dla dziecka:**
- Przewodnik przejmuje rolę opiekuna
- Dziecko może zwiedzać, ale przewodnik nie wchodzi na wieżę
- Dziecko pod opieką przewodnika nie wchodzi na wieżę

**Losowe awarie (2% szans):**
- Przed startem wycieczki lub między atrakcjami
- Przewodnik wysyła SIGUSR2 do całej grupy
- Raportuje awarię przez FIFO

### 2.3. Przepływ danych

**Wejście turysty:**

TURYSTA wysyła MSG_TYPE_ENTRY do KASJER, który zapisuje do park_log.txt

**Wyjście turysty:**

TURYSTA wysyła MSG_TYPE_EXIT_NOTICE do PRZEWODNIK-RAPORTER, który przekazuje MSG_TYPE_EXIT do KASJER

**Raporty przewodników:**

PRZEWODNIK zapisuje przez FIFO do KASJER (proces potomny obsługujący FIFO)

---

## 3. Szczegółowy opis modułów

### 3.1. common.h

Plik nagłówkowy zawierający wszystkie wspólne definicje.

#### Stałe konfiguracyjne

```c
#define N_PARK_CAPACITY 500
#define M_GROUP_SIZE 10
#define X1_BRIDGE_CAP 9
#define X2_TOWER_CAP 18
#define X3_FERRY_CAP 12
#define MAX_GROUPS 15
```

#### Indeksy semaforów

System używa złożonej tablicy semaforów z następującymi grupami:

| Indeks | Nazwa | Opis |
|--------|-------|------|
| 0 | SEM_PARK_LIMIT | Limit wejść do parku |
| 1 | SEM_PRZEWODNIK | Budzenie przewodników |
| 2 | SEM_QUEUE_MUTEX | Mutex kolejki |
| 3 | SEM_STATS_MUTEX | Mutex statystyk |
| 4-5 | SEM_MOST_* | Synchronizacja mostu |
| 6-7 | SEM_WIEZA_* | Synchronizacja wieży |
| 8 | SEM_PROM_MUTEX | Synchronizacja promu |
| 11+ | SEM_GROUP_DONE_BASE | Semafory zakończenia atrakcji dla grup |
| ... | SEM_TOURIST_ASSIGNED_BASE | Semafory przypisania turystów |
| ... | SEM_MEMBER_GO_BASE | Semafory startu członków grup |

Łączna liczba semaforów: TOTAL_SEMAPHORES (~1000+)

#### Struktura komunikatu kolejki

```c
struct msg_buffer {
    long msg_type;
    int tourist_id;
    int age;
    int is_vip;
    char info[256];
};
```

#### Struktura stanu grupy

```c
struct GroupState {
    int active;
    int guide_id;
    pid_t guide_pid;
    int route;
    int size;
    int current_attraction;
    int attraction_step;
    int tourists_ready;
    pid_t member_pids[M_GROUP_SIZE];
    int member_ids[M_GROUP_SIZE];
    int member_ages[M_GROUP_SIZE];
    int member_vips[M_GROUP_SIZE];
    int member_is_caretaker[M_GROUP_SIZE];
    int member_caretaker_of[M_GROUP_SIZE];
    int member_has_caretaker[M_GROUP_SIZE];
    int member_caretaker_is_guide[M_GROUP_SIZE];
    int signal_tower_evacuate;
    int signal_emergency_exit;
};
```

#### Struktura pamięci dzielonej parku

```c
struct ParkSharedMemory {
    time_t park_open_time;
    time_t park_closing_time;
    int park_closed;
    
    int total_entered, total_exited, total_expected;
    int total_revenue, paid_entries;
    int free_entries_vip, free_entries_children;
    int people_in_park, vip_in_park;
    
    int people_in_queue, queue_head, queue_tail;
    int queue_ages[N_PARK_CAPACITY];
    int queue_ids[N_PARK_CAPACITY];
    int queue_vips[N_PARK_CAPACITY];
    pid_t queue_pids[N_PARK_CAPACITY];
    int assigned_group_id[N_PARK_CAPACITY];
    int assigned_member_index[N_PARK_CAPACITY];
    
    struct GroupState groups[MAX_GROUPS];
    int next_group_slot;
    
    int bridge_on_bridge;
    int bridge_direction;
    int bridge_waiting[2];
    
    int tower_current_count;
    pid_t tower_visitors[X2_TOWER_CAP];
    int tower_waiting_vip, tower_waiting_normal;
    
    int ferry_position, ferry_passengers;
    int ferry_on_ferry, ferry_direction;
    int ferry_waiting_vip[2], ferry_waiting_normal[2];
};
```

#### Funkcje pomocnicze (inline)

| Funkcja | Opis |
|---------|------|
| report_error() | Wypisuje błąd z perror() |
| fatal_error() | Wypisuje błąd i kończy program |
| sem_lock() | Operacja P na semaforze |
| sem_unlock() | Operacja V na semaforze |
| sem_trylock() | Próba opuszczenia bez blokowania |
| sem_getval() | Pobranie wartości semafora |
| sem_lock_interruptible() | Opuszczenie z obsługą przerwania |
| sem_timed_wait() | Czekanie z timeoutem |
| ferry_enter() | Wejście na prom z kolejkowaniem |
| ferry_leave() | Zejście z promu i budzenie czekających |
| tower_add_visitor() | Dodanie do listy odwiedzających wieżę |
| tower_remove_visitor() | Usunięcie z listy odwiedzających |
| tower_has_visitor() | Sprawdzenie czy turysta jest na wieży |
| sim_sleep() | Symulacja upływu czasu |
| get_timestamp() | Pobranie aktualnego czasu jako string |
| get_attraction_for_step() | Ustalenie atrakcji dla kroku trasy |
| get_bridge_direction() | Kierunek mostu dla trasy |
| get_ferry_direction() | Kierunek promu dla trasy |

### 3.2. main.c

**Odpowiedzialność:** Inicjalizacja systemu, zarządzanie procesami, cleanup.

#### Główne funkcje

| Funkcja | Opis |
|---------|------|
| cleanup() | Sprzątanie zasobów przy wyjściu |
| handle_sigint() | Obsługa Ctrl+C |
| handle_sigchld() | Obsługa zombie procesów |
| get_input() | Walidacja wejścia użytkownika |
| init_semaphores() | Inicjalizacja semaforów |
| init_shared_memory() | Inicjalizacja pamięci dzielonej |
| cleanup_old_ipc() | Usuwanie starych zasobów IPC |

#### Przepływ główny

1. Czyszczenie starych zasobów IPC
2. Rejestracja atexit(cleanup)
3. Rejestracja handlerów sygnałów (SIGINT, SIGCHLD)
4. Pobranie parametrów od użytkownika (walidacja)
5. Utworzenie zasobów IPC (shm, sem, msg, FIFO)
6. Fork + exec dla kasjera
7. Fork + exec dla przewodnika-reportera
8. Fork + exec dla P przewodników
9. Pętla generowania turystów (fork + exec)
10. Oczekiwanie na zakończenie wszystkich turystów
11. Wysłanie SIGTERM do kasjera
12. Wyświetlenie statystyk
13. Cleanup (wywoływany przez atexit)

### 3.3. kasjer.c

**Odpowiedzialność:** Rejestracja wejść/wyjść, logowanie, obsługa FIFO.

#### Architektura

- Proces główny: odbiera komunikaty z kolejki
- Proces potomny (fork): czyta raporty z FIFO

#### Główne funkcje

| Funkcja | Opis |
|---------|------|
| sigterm_handler() | Obsługa SIGTERM |
| write_log() | Zapis do pliku logu |

#### Przepływ

1. Dołączenie do zasobów IPC
2. Otwarcie pliku logu (park_log.txt)
3. Fork procesu do obsługi FIFO
4. Pętla odbierania komunikatów:
   - MSG_TYPE_ENTRY - rejestracja wejścia
   - MSG_TYPE_EXIT - rejestracja wyjścia
5. Przy SIGTERM: opróżnienie kolejki, zakończenie procesu FIFO

### 3.4. przewodnik.c

**Odpowiedzialność:** Tworzenie grup, prowadzenie wycieczek, sygnalizacja.

#### Tryby pracy

1. **Tryb normalny** - przewodnik prowadzący grupy
2. **Tryb reporter** - przekazuje powiadomienia o wyjściach do kasy

#### Główne funkcje

| Funkcja | Opis |
|---------|------|
| sigterm_handler() | Obsługa SIGTERM |
| run_exit_reporter() | Tryb reportera |
| send_emergency_exit() | Wysłanie SIGUSR2 do grupy |
| send_tower_evacuation() | Wysłanie SIGUSR1 do grupy |
| send_exit_list_to_cashier() | Raportowanie wyjść do kasy |
| find_free_group_slot() | Szukanie wolnego slotu grupy |
| guide_enter_bridge() | Wejście przewodnika na most |
| guide_take_ferry() | Wejście przewodnika na prom |

#### Przepływ (tryb normalny)

1. Oczekiwanie na semaforze SEM_PRZEWODNIK
2. Zajęcie slotu grupy (SEM_GROUP_SLOTS)
3. Pobranie turystów z kolejki
4. Przydzielenie opiekunów dla dzieci <15 lat
5. Losowanie trasy (1 lub 2)
6. Synchronizacja startu z turystami
7. Pętla 3 atrakcji:
   - Most: guide_enter_bridge() - wejście jako pierwszy
   - Wieża: czekanie na dole
   - Prom: guide_take_ferry() - wejście jako pierwszy
   - Czekanie na SEM_GROUP_DONE od wszystkich turystów
8. Raportowanie do kasy i FIFO
9. Zwolnienie slotu grupy

#### Losowe awarie (2% szans)

- Przed startem wycieczki
- W trakcie trasy (między atrakcjami)
- Wysyłany jest SIGUSR2 do wszystkich członków grupy

#### Ewakuacja z wieży (3% szans)

- Wysyłany jest SIGUSR1 do wszystkich członków grupy

### 3.5. turysta.c

**Odpowiedzialność:** Symulacja zachowania turysty.

#### Główne funkcje

| Funkcja | Opis |
|---------|------|
| sigterm_handler() | Obsługa zakończenia |
| sigusr1_handler() | Obsługa ewakuacji z wieży |
| sigusr2_handler() | Obsługa ewakuacji ogólnej |
| enter_park_and_report() | Wejście do parku i raportowanie |
| tower_acquire_slot() | Zajęcie miejsca na wieży |
| tower_release_slot() | Zwolnienie miejsca na wieży |
| do_bridge() | Logika przejścia przez most |
| do_tower() | Logika zwiedzania wieży |
| do_ferry() | Logika przepłynięcia promem |
| do_ferry_vip() | Prom dla VIP samodzielnych |

#### Przepływ (turysta grupowy)

1. Losowanie atrybutów (wiek 3-70, VIP 5%)
2. Sprawdzenie czy park otwarty
3. Ścieżka VIP samodzielny (≥15 lat):
   - Pominięcie kolejki
   - Samodzielne zwiedzanie
4. Ścieżka grupowa:
   - Kolejka do kasy (nie-VIP)
   - Wejście do kolejki grupowej
   - Oczekiwanie na przypisanie do grupy
   - Oczekiwanie na start wycieczki
   - Pętla 3 atrakcji z synchronizacją
5. Zgłoszenie wyjścia

#### Logika mostu (do_bridge)

1. Sprawdzenie flagi ewakuacji
2. Czekanie na przewodnika (SEM_BRIDGE_GUIDE_READY)
3. Sprawdzenie kierunku mostu:
   - Zgodny lub pusty → wejście
   - Przeciwny → czekanie w kolejce (SEM_BRIDGE_WAIT)
4. Zajęcie miejsca (SEM_MOST_LIMIT)
5. Przejście (sim_sleep)
6. Zwolnienie miejsca
7. Jeśli ostatni → zmiana kierunku lub reset

#### Logika wieży (do_tower)

1. Sprawdzenie wieku (≤5 lat → nie wchodzi)
2. Sprawdzenie opieki (opiekun dziecka ≤5 → nie wchodzi)
3. Zajęcie slotu (tower_acquire_slot) z priorytetem VIP
4. Wejście po schodach (SEM_TOWER_STAIRS_UP)
5. Dodanie do listy odwiedzających
6. Podziwianie widoków (sem_timed_wait z obsługą SIGUSR1)
7. Zejście po schodach (SEM_TOWER_STAIRS_DOWN)
8. Usunięcie z listy i zwolnienie slotu

#### Logika promu (do_ferry)

1. Czekanie na przewodnika (SEM_FERRY_GUIDE_READY)
2. Wejście na prom (ferry_enter) z priorytetem VIP
3. Podróż (sim_sleep)
4. Zejście z promu (ferry_leave)

---

## 4. Mechanizmy IPC i synchronizacji

### 4.1. Pamięć dzielona (Shared Memory)

#### Utworzenie

```c
shm_id = shmget(ftok(FTOK_PATH, FTOK_SHM_ID), 
                sizeof(struct ParkSharedMemory), IPC_CREAT | 0600);
```

#### Dołączenie

```c
struct ParkSharedMemory *park = (struct ParkSharedMemory*)shmat(shm_id, NULL, 0);
```

#### Odłączenie

```c
shmdt(park);
```

#### Usunięcie

```c
shmctl(shm_id, IPC_RMID, NULL);
```

#### Zastosowanie

- Przechowywanie stanu całego parku
- Kolejka wejściowa turystów
- Stany wszystkich grup
- Stany atrakcji (most, wieża, prom)
- Statystyki parku

### 4.2. Semafory (System V)

#### Utworzenie

```c
sem_id = semget(ftok(FTOK_PATH, FTOK_SEM_ID), TOTAL_SEMAPHORES, IPC_CREAT | 0600);
```

#### Inicjalizacja

```c
union semun arg;
arg.val = initial_value;
semctl(sem_id, sem_num, SETVAL, arg);
```

#### Operacje

```c
// opuszczenie (P)
struct sembuf op = {sem_num, -1, 0};
semop(sem_id, &op, 1);

// podniesienie (V)
struct sembuf op = {sem_num, 1, 0};
semop(sem_id, &op, 1);

// proba bez blokowania
struct sembuf op = {sem_num, -1, IPC_NOWAIT};
semop(sem_id, &op, 1);

// czekanie z timeoutem
semtimedop(sem_id, &op, 1, &timeout);
```

#### Typy semaforów używanych

| Typ | Przykład | Zastosowanie |
|-----|----------|--------------|
| Mutex | SEM_QUEUE_MUTEX | Wyłączny dostęp do sekcji krytycznej |
| Counting | SEM_PARK_LIMIT | Ograniczenie liczby zasobów |
| Binary | SEM_PRZEWODNIK | Sygnalizacja zdarzenia |
| Barrier | SEM_GROUP_DONE | Synchronizacja grupy |

### 4.3. Kolejki komunikatów (Message Queues)

#### Utworzenie

```c
msg_id = msgget(ftok(FTOK_PATH, FTOK_MSG_ID), IPC_CREAT | 0600);
```

#### Wysyłanie

```c
struct msg_buffer msg;
msg.msg_type = MSG_TYPE_ENTRY;
msg.tourist_id = id;
msgsnd(msg_id, &msg, sizeof(msg) - sizeof(long), 0);
```

#### Odbieranie

```c
struct msg_buffer msg;
msgrcv(msg_id, &msg, sizeof(msg) - sizeof(long), 0, 0);
```

#### Usunięcie

```c
msgctl(msg_id, IPC_RMID, NULL);
```

#### Zastosowanie

- Komunikacja turysta → kasjer (wejścia)
- Komunikacja turysta → reporter → kasjer (wyjścia)

### 4.4. FIFO (Named Pipe)

#### Utworzenie

```c
mkfifo(FIFO_PATH, 0600);
```

#### Otwarcie i zapis (przewodnik)

```c
int fifo_fd = open(FIFO_PATH, O_WRONLY);
write(fifo_fd, report, strlen(report));
close(fifo_fd);
```

#### Otwarcie i odczyt (kasjer)

```c
int fifo_fd = open(FIFO_PATH, O_RDWR);
read(fifo_fd, buffer, sizeof(buffer) - 1);
close(fifo_fd);
```

#### Usunięcie

```c
unlink(FIFO_PATH);
```

#### Zastosowanie

- Raporty przewodników o zakończonych wycieczkach
- Raporty o awariach

### 4.5. Procesy (fork + exec)

#### Tworzenie procesu

```c
pid_t pid = fork();
if (pid == 0) {
    execl("./program", "program", "arg1", NULL);
    fatal_error("Błąd execl");
}
```

#### Oczekiwanie na zakończenie

```c
waitpid(pid, NULL, 0);
waitpid(-1, NULL, WNOHANG);
```

---

## 5. Obsługa sygnałów

### 5.1. Zarejestrowane sygnały

| Sygnał | Obsługujący | Działanie |
|--------|-------------|-----------|
| SIGINT | main | Przerwanie programu (Ctrl+C), cleanup |
| SIGCHLD | main | Zbieranie zombie procesów |
| SIGTERM | wszystkie | Graceful shutdown |
| SIGUSR1 | turysta | Ewakuacja z wieży |
| SIGUSR2 | turysta | Ewakuacja ogólna (powrót do kasy) |

### 5.2. Rejestracja handlerów

```c
struct sigaction sa;
sa.sa_handler = handler_function;
sigemptyset(&sa.sa_mask);
sa.sa_flags = 0;
sigaction(SIGNAL, &sa, NULL);
```

### 5.3. Bezpieczne handlery (async-signal-safe)

W handlerach używane są tylko bezpieczne funkcje:

```c
void sigusr1_handler(int sig) {
    (void)sig;
    tower_evacuation_flag = 1;
    
    char msg[128];
    int pos = 0;
    // budowanie stringa recznie
    write(STDOUT_FILENO, msg, pos);
}
```

### 5.4. Wysyłanie sygnałów

```c
// do konkretnego procesu
kill(pid, SIGUSR1);

// do wszystkich w grupie procesow
kill(0, SIGTERM);
```

---

## 6. Walidacja danych i obsługa błędów

### 6.1. Walidacja wejścia użytkownika

```c
int get_input(const char* prompt, int min, int max) {
    int value;
    while (1) {
        printf(CLR_WHITE "%s (%d - %d): " CLR_RESET, prompt, min, max);
        if (scanf("%d", &value) == 1) {
            if (value >= min && value <= max) {
                return value;
            } else {
                printf(CLR_RED "Błąd: Wartość musi być z przedziału <%d, %d>!" 
                       CLR_RESET "\n", min, max);
            }
        } else {
            printf(CLR_RED "Błąd: To nie jest liczba!" CLR_RESET "\n");
            while (getchar() != '\n');
        }
    }
}
```

### 6.2. Obsługa błędów funkcji systemowych

#### Funkcje pomocnicze

```c
static inline void report_error(const char *context) {
    perror(context);
}

static inline void fatal_error(const char *context) {
    perror(context);
    exit(1);
}
```

#### Przykłady użycia

```c
// blad krytyczny - konczy program
shm_id = shmget(ftok(FTOK_PATH, FTOK_SHM_ID), sizeof(...), IPC_CREAT | 0600);
if (shm_id == -1) {
    fatal_error("[MAIN] Błąd shmget");
}

// blad niekrytyczny - raportuje i kontynuuje
if (close(fd) == -1) {
    report_error("[KASJER] Błąd close");
}

// obsluga EINTR (przerwanie przez sygnal)
while (semop(sem_id, &op, 1) == -1) {
    if (errno == EINTR) {
        continue;
    }
    fatal_error("Błąd semop");
}
```

### 6.3. Walidacja konfiguracji

```c
if (X3_FERRY_CAP < M_GROUP_SIZE + 1) {
    fprintf(stderr, CLR_RED "[MAIN] Błąd konfiguracji: X3_FERRY_CAP (%d) < "
            "M_GROUP_SIZE+1 (%d). Prom nie pomieści grupy z przewodnikiem.\n" 
            CLR_RESET, X3_FERRY_CAP, M_GROUP_SIZE + 1);
    exit(1);
}
```

### 6.4. Obsługa braku zasobów (fork)

```c
pid_t pid = fork();
if (pid == -1) {
    if (errno == EAGAIN || errno == ENOMEM) {
        int reaped = 0;
        while (waitpid(-1, NULL, WNOHANG) > 0) {
            finished_tourists++;
            reaped++;
        }
        if (reaped > 0) {
            i--;
            continue;
        }
    }
    report_error("[MAIN] Błąd fork");
}
```

---

## 7. Elementy specjalne

### 7.1. Kolorowanie wyjścia terminala

```c
#define CLR_RESET "\033[0m"
#define CLR_RED "\033[0;31m"
#define CLR_GREEN "\033[0;32m"
#define CLR_YELLOW "\033[0;33m"
#define CLR_BLUE "\033[0;34m"
#define CLR_MAGENTA "\033[0;35m"
#define CLR_CYAN "\033[0;36m"
#define CLR_WHITE "\033[0;37m"
#define CLR_BOLD "\033[1m"
#define CLR_BG_RED "\033[41m"
```

#### Użycie

```c
printf(CLR_GREEN "[PRZEWODNIK %d] Melduję się w pracy!" CLR_RESET "\n", id);
printf(CLR_BG_RED CLR_WHITE "[AWARIA]" CLR_RESET "\n");
```

#### Schemat kolorów

| Kolor | Zastosowanie |
|-------|--------------|
| Zielony | Przewodnik |
| Cyan | Turysta |
| Żółty | Kasjer, ostrzeżenia |
| Magenta | VIP |
| Czerwony | Błędy, ewakuacja |
| Biały | Main, informacje systemowe |

### 7.2. System opieki nad dziećmi

#### Przydzielanie opiekunów

```c
for (int i = 0; i < actual_group_size; i++) {
    if (group->member_ages[i] < 15) {
        for (int j = 0; j < actual_group_size; j++) {
            if (group->member_ages[j] >= 18 && !group->member_is_caretaker[j]) {
                group->member_is_caretaker[j] = 1;
                group->member_caretaker_of[j] = i;
                group->member_has_caretaker[i] = j;
                break;
            }
        }
    }
}

// jesli brak opiekuna - przewodnik przejmuje opieke
for (int i = 0; i < actual_group_size; i++) {
    if (group->member_ages[i] < 15 && group->member_has_caretaker[i] == -1) {
        group->member_caretaker_is_guide[i] = 1;
    }
}
```

### 7.3. Losowe awarie i ewakuacje

#### Awaria przed/w trakcie wycieczki (2% szans)

```c
if ((rand() % 100) < 2) {
    printf(CLR_BG_RED CLR_WHITE "[PRZEWODNIK %d] Awaria!" CLR_RESET "\n", id);
    send_emergency_exit(group, id);
}
```

#### Ewakuacja z wieży (3% szans)

```c
if (attraction == ATTR_TOWER && (rand() % 100) < 3) {
    send_tower_evacuation(group, park, id);
}
```

### 7.4. Wydłużenie czasu dla grup z dziećmi

```c
static inline void sim_sleep(int min_us, int max_us, int has_young_children) {
    if (max_us <= 0) return;
    int duration = min_us + (rand() % (max_us - min_us + 1));
    if (has_young_children) {
        duration = (int)(duration * 1.5);
    }
    if (duration > 0) {
        usleep(duration);
    }
}
```

### 7.5. Priorytet VIP

#### Na wieży

```c
static int tower_acquire_slot(int sem_id, struct ParkSharedMemory *park, int is_vip) {
    while (1) {
        sem_lock(sem_id, SEM_WIEZA_MUTEX);
        int can_enter = (park->tower_current_count < X2_TOWER_CAP);
        if (!is_vip) {
            can_enter = can_enter && (park->tower_waiting_vip == 0);
        }
        // ...
    }
}
```

#### Na promie

```c
if (!is_vip && park->ferry_waiting_vip[direction] > 0) {
    park->ferry_waiting_normal[direction]++;
    // czekaj na SEM_FERRY_WAIT
} else {
    // wejdz od razu lub czekaj na SEM_FERRY_VIP_WAIT
}
```

### 7.6. Zmiana kierunku mostu

```c
if (park->bridge_on_bridge == 0) {
    if (park->bridge_waiting[other_dir] > 0) {
        park->bridge_direction = other_dir;
        int to_wake = park->bridge_waiting[other_dir];
        park->bridge_waiting[other_dir] = 0;
        
        for (int i = 0; i < to_wake; i++) {
            sem_unlock(sem_id, SEM_BRIDGE_WAIT(other_dir));
        }
    } else if (park->bridge_waiting[direction] == 0) {
        park->bridge_direction = DIR_NONE;
    }
}
```

### 7.7. Czyszczenie starych zasobów IPC

```c
void cleanup_old_ipc() {
    int old_msg_id = msgget(ftok(FTOK_PATH, FTOK_MSG_ID), 0600);
    if (old_msg_id != -1) {
        msgctl(old_msg_id, IPC_RMID, NULL);
    }
    // podobnie dla shm, sem, FIFO...
}
```

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

Semafor SEM_MOST_LIMIT i inne liczniki atrakcji wracają do stanu początkowego (nikt nie utknął "logicznie" na moście po ewakuacji).

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

## 9. Napotkane problemy

### 9.1. Problem z liczbą semaforów

**Opis:** System używa bardzo dużej liczby semaforów (~1000+), co może przekraczać limity systemowe.

**Rozwiązanie:**

- Sprawdzenie limitów: `cat /proc/sys/kernel/sem`
- Ewentualna modyfikacja: `sysctl -w kernel.sem="250 32000 100 128"`

### 9.2. Wyścigi przy obsłudze sygnałów

**Opis:** Handlery sygnałów muszą być async-signal-safe, nie mogą używać printf().

**Rozwiązanie:** Użycie write() z ręcznym budowaniem stringów:

```c
char msg[128];
int pos = 0;
// reczne budowanie stringa
write(STDOUT_FILENO, msg, pos);
```

### 9.3. Zombie procesy

**Opis:** Przy dużej liczbie turystów mogą powstawać zombie procesy.

**Rozwiązanie:** Handler SIGCHLD z pętlą waitpid(WNOHANG):

```c
void handle_sigchld(int sig) {
    while (waitpid(-1, NULL, WNOHANG) > 0);
}
```

### 9.4. Deadlock przy ewakuacji

**Opis:** Potencjalny deadlock gdy sygnał ewakuacji przychodzi w trakcie oczekiwania na semafor.

**Rozwiązanie:** Funkcja sem_lock_interruptible() sprawdzająca flagę przerwania:

```c
static inline int sem_lock_interruptible(int sem_id, int sem_num, 
                                          volatile sig_atomic_t *interrupt_flag) {
    while (semop(sem_id, &op, 1) == -1) {
        if (errno == EINTR) {
            if (interrupt_flag != NULL && *interrupt_flag) {
                return -1;
            }
            continue;
        }
        fatal_error("Błąd semop");
    }
    return 0;
}
```

### 9.5. Brak zasobów na fork()

**Opis:** Przy generowaniu dużej liczby turystów może zabraknąć zasobów.

**Rozwiązanie:** Obsługa EAGAIN/ENOMEM z próbą odzyskania zasobów przez waitpid().

### 9.6. Niepełne grupy na koniec dnia

**Opis:** Po zamknięciu parku mogą pozostać turyści w niepełnej grupie.

**Rozwiązanie:** Logika wykrywania i budzenia przewodnika dla niepełnej grupy:

```c
if ((all_entered == all_expected || park->park_closed) && queue_size > 0) {
    // Weź niepełną grupę
}
```

---

## 10. Podsumowanie

### 10.1. Co udało się zrobić

- Pełna implementacja symulacji parku narodowego zgodna z wymaganiami
- Wszystkie trzy atrakcje z prawidłową logiką:
  - Most z ruchem jednokierunkowym
  - Wieża z ograniczeniami wiekowymi i ewakuacją
  - Prom z kolejkowaniem
- System grup z opieką nad dziećmi
- Obsługa VIP z priorytetami
- Dwie trasy zwiedzania
- Wydłużenie czasu dla grup z dziećmi <12 lat
- Losowe awarie i ewakuacje
- Pełna obsługa sygnałów (SIGINT, SIGCHLD, SIGTERM, SIGUSR1, SIGUSR2)
- Walidacja danych wejściowych
- Obsługa błędów z perror()
- Kolorowe wyjście terminala
- Logowanie do pliku
- Sprzątanie zasobów IPC
- Raportowanie przez FIFO

### 10.2. Elementy dodatkowe

- Kolorowanie wyjścia terminala dla lepszej czytelności
- System opiekunów z możliwością przejęcia opieki przez przewodnika
- Losowe awarie z ewakuacją
- Szczegółowe statystyki końcowe
- Czyszczenie starych zasobów IPC przy starcie

### 10.3. Możliwe usprawnienia

- Dodanie interfejsu graficznego (ncurses)
- Implementacja wątków zamiast procesów dla lepszej wydajności
- Dodanie konfiguracji z pliku
- Rozbudowa systemu logowania

---

## 11. Linki do kodu

### 11.1. Tworzenie i obsługa plików

| Funkcja | Lokalizacja | Opis |
|---------|-------------|------|
| open() | [kasjer.c#L65](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/52a5dfc40fee27e7720a516bb8dfbeb13e718f2c/kasjer.c#L65) | Otwarcie pliku logu |
| close() | [kasjer.c#L247](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/52a5dfc40fee27e7720a516bb8dfbeb13e718f2c/kasjer.c#L247) | Zamknięcie pliku |
| read() | [kasjer.c#L115](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/52a5dfc40fee27e7720a516bb8dfbeb13e718f2c/kasjer.c#L115) | Odczyt z FIFO |
| write() | [kasjer.c#L27](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/52a5dfc40fee27e7720a516bb8dfbeb13e718f2c/kasjer.c#L27) | Zapis do pliku logu |
| unlink() | [main.c#L33](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/52a5dfc40fee27e7720a516bb8dfbeb13e718f2c/main.c#L33) | Usunięcie FIFO |

### 11.2. Tworzenie procesów

| Funkcja | Lokalizacja | Opis |
|---------|-------------|------|
| fork() | [main.c#L528](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/52a5dfc40fee27e7720a516bb8dfbeb13e718f2c/main.c#L528) | Tworzenie procesu kasjera |
| fork() | [main.c#L552](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/52a5dfc40fee27e7720a516bb8dfbeb13e718f2c/main.c#L552) | Tworzenie procesów przewodników |
| fork() | [main.c#L539](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/52a5dfc40fee27e7720a516bb8dfbeb13e718f2c/main.c#L539) | Tworzenie procesów przewodników - reporterów |
| fork() | [main.c#L596](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/52a5dfc40fee27e7720a516bb8dfbeb13e718f2c/main.c#L596) | Tworzenie procesów turystów |
| fork() | [kasjer.c#L98](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/52a5dfc40fee27e7720a516bb8dfbeb13e718f2c/kasjer.c#L98) | Tworzenie procesu FIFO |
| execl() | [main.c#L534](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/52a5dfc40fee27e7720a516bb8dfbeb13e718f2c/main.c#L534) | Uruchomienie programu kasjer |
| execl() | [main.c#L560](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/52a5dfc40fee27e7720a516bb8dfbeb13e718f2c/main.c#L560) | Uruchomienie programu przewodnik |
| execl() | [main.c#L544](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/52a5dfc40fee27e7720a516bb8dfbeb13e718f2c/main.c#L544) | Uruchomienie programu przewodnik - reporter|
| execl() | [main.c#L626](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/52a5dfc40fee27e7720a516bb8dfbeb13e718f2c/main.c#L626) | Uruchomienie programu turysta |
| exit() | [common.h#L284](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/52a5dfc40fee27e7720a516bb8dfbeb13e718f2c/common.h#L284) | Zakończenie przy błędzie krytycznym |
| waitpid() | [main.c#L57](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/52a5dfc40fee27e7720a516bb8dfbeb13e718f2c/main.c#L57) | Oczekiwanie na procesy potomne |

### 11.3. Obsługa sygnałów

| Funkcja | Lokalizacja | Opis |
|---------|-------------|------|
| sigaction() | [main.c#L449](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/52a5dfc40fee27e7720a516bb8dfbeb13e718f2c/main.c#L449) | Rejestracja SIGINT |
| sigaction() | [main.c#L459](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/52a5dfc40fee27e7720a516bb8dfbeb13e718f2c/main.c#L459) | Rejestracja SIGCHLD |
| sigaction() | [turysta.c#L563](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/52a5dfc40fee27e7720a516bb8dfbeb13e718f2c/turysta.c#L563) | Rejestracja SIGUSR1 |
| sigaction() | [turysta.c#L573](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/52a5dfc40fee27e7720a516bb8dfbeb13e718f2c/turysta.c#L573) | Rejestracja SIGUSR2 |
| sigaction() | [turysta.c#L583](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/52a5dfc40fee27e7720a516bb8dfbeb13e718f2c/turysta.c#L583) | Rejestracja SIGTERM |
| kill() | [przewodnik.c#L99](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/52a5dfc40fee27e7720a516bb8dfbeb13e718f2c/przewodnik.c#L99) | Wysłanie SIGUSR1 (ewakuacja wieży) |
| kill() | [przewodnik.c#L79](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/52a5dfc40fee27e7720a516bb8dfbeb13e718f2c/przewodnik.c#L79) | Wysłanie SIGUSR2 (ewakuacja ogólna) |
| kill() | [main.c#L705](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/52a5dfc40fee27e7720a516bb8dfbeb13e718f2c/main.c#L705) | Wysłanie SIGTERM do kasjera |
| sigemptyset() | [turysta.c#L558](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/52a5dfc40fee27e7720a516bb8dfbeb13e718f2c/turysta.c#L558) | Inicjalizacja maski sygnałów |

### 11.4. Synchronizacja procesów (semafory)

| Funkcja | Lokalizacja | Opis |
|---------|-------------|------|
| ftok() | [main.c#L494](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/52a5dfc40fee27e7720a516bb8dfbeb13e718f2c/main.c#L494) | Generowanie klucza IPC |
| semget() | [main.c#L494](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/52a5dfc40fee27e7720a516bb8dfbeb13e718f2c/main.c#L494) | Utworzenie zestawu semaforów |
| semctl() | [main.c#L165](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/52a5dfc40fee27e7720a516bb8dfbeb13e718f2c/main.c#L165) | Inicjalizacja semaforów |
| semctl() | [main.c#L100](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/52a5dfc40fee27e7720a516bb8dfbeb13e718f2c/main.c#L100) | Usunięcie semaforów |
| semop() | [common.h#L294](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/52a5dfc40fee27e7720a516bb8dfbeb13e718f2c/common.h#L294) | Operacja P (sem_lock) |
| semop() | [common.h#L328](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/52a5dfc40fee27e7720a516bb8dfbeb13e718f2c/common.h#L328) | Operacja V (sem_unlock) |
| semop() | [common.h#L343](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/52a5dfc40fee27e7720a516bb8dfbeb13e718f2c/common.h#L343) | Próba bez blokowania (sem_trylock) |
| semtimedop() | [common.h#L390](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/52a5dfc40fee27e7720a516bb8dfbeb13e718f2c/common.h#L390) | Czekanie z timeoutem |

### 11.5. Łącza nazwane (FIFO)

| Funkcja | Lokalizacja | Opis |
|---------|-------------|------|
| mkfifo() | [main.c#514](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/52a5dfc40fee27e7720a516bb8dfbeb13e718f2c/main.c#L514) | Utworzenie FIFO |
| open() | [przewodnik.c#L665](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/52a5dfc40fee27e7720a516bb8dfbeb13e718f2c/przewodnik.c#L665) | Otwarcie FIFO do zapisu |
| write() | [przewodnik.c#L673](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/52a5dfc40fee27e7720a516bb8dfbeb13e718f2c/przewodnik.c#L673) | Zapis raportu do FIFO |
| read() | [kasjer.c#L115](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/52a5dfc40fee27e7720a516bb8dfbeb13e718f2c/kasjer.c#L115) | Odczyt z FIFO |

### 11.6. Pamięć dzielona

| Funkcja | Lokalizacja | Opis |
|---------|-------------|------|
| ftok() | [main.c#L494](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/52a5dfc40fee27e7720a516bb8dfbeb13e718f2c/main.c#L494) | Generowanie klucza IPC |
| shmget() | [main.c#L481](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/52a5dfc40fee27e7720a516bb8dfbeb13e718f2c/main.c#L481) | Utworzenie segmentu pamięci |
| shmat() | [main.c#L487](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/52a5dfc40fee27e7720a516bb8dfbeb13e718f2c/main.c#L487) | Dołączenie do pamięci (main) |
| shmat() | [kasjer.c#L47](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/52a5dfc40fee27e7720a516bb8dfbeb13e718f2c/kasjer.c#L47) | Dołączenie do pamięci (kasjer) |
| shmat() | [przewodnik.c#L282](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/52a5dfc40fee27e7720a516bb8dfbeb13e718f2c/przewodnik.c#L282) | Dołączenie do pamięci (przewodnik) |
| shmat() | [turysta.c#L606](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/52a5dfc40fee27e7720a516bb8dfbeb13e718f2c/turysta.c#L606) | Dołączenie do pamięci (turysta) |
| shmdt() | [turysta.c#L893](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/52a5dfc40fee27e7720a516bb8dfbeb13e718f2c/turysta.c#L893) | Odłączenie od pamięci |
| shmctl() | [main.c#L92](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/52a5dfc40fee27e7720a516bb8dfbeb13e718f2c/main.c#L92) | Usunięcie segmentu |

### 11.7. Kolejki komunikatów

| Funkcja | Lokalizacja | Opis |
|---------|-------------|------|
| ftok() | [main.c#L494](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/52a5dfc40fee27e7720a516bb8dfbeb13e718f2c/main.c#L494) | Generowanie klucza IPC |
| msgget() | [main.c#L502](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/52a5dfc40fee27e7720a516bb8dfbeb13e718f2c/main.c#L502) | Utworzenie kolejki głównej |
| msgget() | [main.c#L508](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/52a5dfc40fee27e7720a516bb8dfbeb13e718f2c/main.c#L508) | Utworzenie kolejki raportowej |
| msgsnd() | [turysta.c#L63](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/52a5dfc40fee27e7720a516bb8dfbeb13e718f2c/turysta.c#L63) | Wysłanie komunikatu wejścia |
| msgsnd() | [turysta.c#L874](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/52a5dfc40fee27e7720a516bb8dfbeb13e718f2c/turysta.c#L874) | Wysłanie powiadomienia wyjścia |
| msgsnd() | [przewodnik.c#L119](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/52a5dfc40fee27e7720a516bb8dfbeb13e718f2c/przewodnik.c#L119) | Wysłanie komunikatu wyjścia |
| msgrcv() | [kasjer.c#L149](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/52a5dfc40fee27e7720a516bb8dfbeb13e718f2c/kasjer.c#L149) | Odbiór komunikatów |
| msgrcv() | [przewodnik.c#L39](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/52a5dfc40fee27e7720a516bb8dfbeb13e718f2c/przewodnik.c#L39) | Odbiór powiadomień (reporter) |
| msgctl() | [main.c#L74](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/52a5dfc40fee27e7720a516bb8dfbeb13e718f2c/main.c#L74) | Usunięcie kolejki |

### 11.8. Obsługa błędów

| Funkcja | Lokalizacja | Opis |
|---------|-------------|------|
| perror() | [common.h#L277](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/52a5dfc40fee27e7720a516bb8dfbeb13e718f2c/common.h#L277) | Funkcja report_error() |
| perror() | [common.h#L282](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/52a5dfc40fee27e7720a516bb8dfbeb13e718f2c/common.h#L282) | Funkcja fatal_error() |
| Walidacja danych | [main.c#L146](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/52a5dfc40fee27e7720a516bb8dfbeb13e718f2c/main.c#L146) | Funkcja get_input() |
| Obsługa EINTR | [common.h#L295](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/52a5dfc40fee27e7720a516bb8dfbeb13e718f2c/common.h#L295) | W sem_lock() |
| Obsługa EAGAIN | [main.c#L598](https://github.com/meneluero/Projekt-SO-Park-Narodowy/blob/52a5dfc40fee27e7720a516bb8dfbeb13e718f2c/main.c#L598) | Przy fork() |

---

**Autor:** Wiktor Kościółek

**Data:** 02.02.2026

**Repozytorium:** https://github.com/meneluero/Projekt-SO-Park-Narodowy
