#include "common.h"
#include <signal.h>
#include <string.h>
#include <sys/wait.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>

// zmienne globalne do przechowywania id zasobow ipc
int shm_id = -1;
int sem_id = -1;
int msg_id = -1;
int report_msg_id = -1;
int cleanup_done = 0; 

int dummy_fifo_fd = -1;
volatile sig_atomic_t child_reap_in_progress = 0;

// funkcja sprzatajaca zasoby systemowe przed zamknieciem
void cleanup() {
    if (cleanup_done) return;
    cleanup_done = 1;

    if (dummy_fifo_fd != -1) {
        if (close(dummy_fifo_fd) == -1) {
            report_error("[MAIN] Błąd close dummy FIFO");
        }
    }

    printf("\n" CLR_WHITE "[MAIN] Rozpoczęto sprzątanie zasobów..." CLR_RESET "\n");

    // usuwanie pliku fifo
    if (unlink(FIFO_PATH) == -1 && errno != ENOENT) {
        report_error("[MAIN] Błąd unlink FIFO");
    } else {
        printf(CLR_WHITE "[MAIN] FIFO usunięte." CLR_RESET "\n");
    }

    // ignorowanie sigterm podczas sprzatania
    struct sigaction sa;
    sa.sa_handler = SIG_IGN;
    if (sigemptyset(&sa.sa_mask) == -1) {
        report_error("[MAIN] Błąd sigemptyset w cleanup");
    }
    sa.sa_flags = 0;
    if (sigaction(SIGTERM, &sa, NULL) == -1) {
        report_error("[MAIN] Błąd sigaction(SIGTERM) w cleanup");
    }

    // wyslanie sigterm do wszystkich procesow w grupie
    if (kill(0, SIGTERM) == -1) {
        report_error("[MAIN] Błąd kill(SIGTERM) dla grupy procesów");
    }

    // czekanie na zakonczenie procesow potomnych
    while (1) {
        pid_t pid = waitpid(-1, NULL, 0);
        if (pid > 0) {
            continue;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno != ECHILD) {
            report_error("[MAIN] Błąd waitpid podczas sprzątania");
        }
        break;
    }

    printf(CLR_WHITE "[MAIN] Procesy potomne posprzątane." CLR_RESET "\n");

    // usuwanie kolejek komunikatow
    if (msg_id != -1) {
        if (msgctl(msg_id, IPC_RMID, NULL) == -1) {
            report_error("[MAIN] Błąd msgctl");
        } else {
            printf(CLR_WHITE "[MAIN] Kolejka usunięta." CLR_RESET "\n");
        }
    }

    // usuwanie pamieci dzielonej
    if (report_msg_id != -1) {
        if (msgctl(report_msg_id, IPC_RMID, NULL) == -1) {
            report_error("[MAIN] Błąd msgctl (report queue)");
        } else {
            printf(CLR_WHITE "[MAIN] Kolejka raportowa usunięta." CLR_RESET "\n");
        }
    }

    // usuwanie semaforow
    if (shm_id != -1) {
        if (shmctl(shm_id, IPC_RMID, NULL) == -1) {
            report_error("[MAIN] Błąd shmctl");
        } else {
            printf(CLR_WHITE "[MAIN] Pamięć dzielona usunięta." CLR_RESET "\n");
        }
    }

    if (sem_id != -1) {
        if (semctl(sem_id, 0, IPC_RMID) == -1) {
            report_error("[MAIN] Błąd semctl");
        } else {
            printf(CLR_WHITE "[MAIN] Semafory usunięte." CLR_RESET "\n");
        }
    }

    printf(CLR_WHITE "[MAIN] System czysty. Zamykanie." CLR_RESET "\n");
}

// obsluga ctrl+c
void handle_sigint(int sig) {
    (void)sig;
    const char msg[] = "\n\033[0;37m[MAIN] Otrzymano SIGINT (Ctrl + C). Kończę program.\033[0m\n";
    if (write(STDOUT_FILENO, msg, sizeof(msg) - 1) == -1) {
        report_error("[MAIN] Błąd write w handlerze SIGINT");
    }
    exit(0); // to wywola atexit(cleanup)
}

// obsluga zakonczenia procesu zombie
void handle_sigchld(int sig) {
    (void)sig;
    int saved_errno = errno;
    child_reap_in_progress = 1;
    while (1) {
        pid_t pid = waitpid(-1, NULL, WNOHANG);
        if (pid > 0) {
            continue;
        }
        if (pid == 0) {
            break;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno != ECHILD) {
            report_error("[MAIN] Błąd waitpid(WNOHANG) w SIGCHLD");
        }
        break;
    }
    child_reap_in_progress = 0;
    errno = saved_errno;
}

// pobieranie danych od uzytkownika z walidacja
int get_input(const char* prompt, int min, int max) {
    int value;
    while (1) {
        printf(CLR_WHITE "%s (%d - %d): " CLR_RESET, prompt, min, max);
        if (scanf("%d", &value) == 1) {
            if (value >= min && value <= max) {
                return value;
            } else {
                printf(CLR_RED "Błąd: Wartość musi być z przedziału <%d, %d>!" CLR_RESET "\n", min, max);
            }
        } else {
            printf(CLR_RED "Błąd: To nie jest liczba!" CLR_RESET "\n");
            while (getchar() != '\n'); // czyszczenie bufora wejscia

        }
    }
}

// inicjalizacja wartosci poczatkowych semaforow
void init_semaphores(int sem_id) {
    union semun arg;

    printf(CLR_WHITE "[MAIN] Inicjalizacja %d semaforów..." CLR_RESET "\n", TOTAL_SEMAPHORES);

    // ustawienie limitu wejsc do parku
    arg.val = N_PARK_CAPACITY;
    if (semctl(sem_id, SEM_PARK_LIMIT, SETVAL, arg) == -1) {
        fatal_error("[MAIN] Błąd semctl SEM_PARK_LIMIT");
    }

    arg.val = 0;
    if (semctl(sem_id, SEM_PRZEWODNIK, SETVAL, arg) == -1) {
        fatal_error("[MAIN] Błąd semctl SEM_PRZEWODNIK");
    }

    // mutexy
    arg.val = 1;
    if (semctl(sem_id, SEM_QUEUE_MUTEX, SETVAL, arg) == -1) {
        fatal_error("[MAIN] Błąd semctl SEM_QUEUE_MUTEX");
    }

    arg.val = 1;
    if (semctl(sem_id, SEM_STATS_MUTEX, SETVAL, arg) == -1) {
        fatal_error("[MAIN] Błąd semctl SEM_STATS_MUTEX");
    }

    arg.val = X1_BRIDGE_CAP;
    if (semctl(sem_id, SEM_MOST_LIMIT, SETVAL, arg) == -1) {
        fatal_error("[MAIN] Błąd semctl SEM_MOST_LIMIT");
    }

    arg.val = 1;
    if (semctl(sem_id, SEM_MOST_MUTEX, SETVAL, arg) == -1) {
        fatal_error("[MAIN] Błąd semctl SEM_MOST_MUTEX");
    }

    // inicjalizacja kolejek do mostu
    arg.val = 0;
    if (semctl(sem_id, SEM_BRIDGE_WAIT_KA, SETVAL, arg) == -1) {
        fatal_error("[MAIN] Błąd semctl SEM_BRIDGE_WAIT_KA");
    }
    if (semctl(sem_id, SEM_BRIDGE_WAIT_AK, SETVAL, arg) == -1) {
        fatal_error("[MAIN] Błąd semctl SEM_BRIDGE_WAIT_AK");
    }

    // inicjalizacja wiezy
    arg.val = X2_TOWER_CAP;
    if (semctl(sem_id, SEM_WIEZA_LIMIT, SETVAL, arg) == -1) {
        fatal_error("[MAIN] Błąd semctl SEM_WIEZA_LIMIT");
    }

    arg.val = 1;
    if (semctl(sem_id, SEM_WIEZA_MUTEX, SETVAL, arg) == -1) {
        fatal_error("[MAIN] Błąd semctl SEM_WIEZA_MUTEX");
    }

    arg.val = X2_TOWER_CAP;
    if (semctl(sem_id, SEM_TOWER_STAIRS_UP, SETVAL, arg) == -1) {
        fatal_error("[MAIN] Błąd semctl SEM_TOWER_STAIRS_UP");
    }

    arg.val = X2_TOWER_CAP;
    if (semctl(sem_id, SEM_TOWER_STAIRS_DOWN, SETVAL, arg) == -1) {
        fatal_error("[MAIN] Błąd semctl SEM_TOWER_STAIRS_DOWN");
    }

    // inicjalizacja promu
    arg.val = 1;
    if (semctl(sem_id, SEM_PROM_MUTEX, SETVAL, arg) == -1) {
        fatal_error("[MAIN] Błąd semctl SEM_PROM_MUTEX");
    }

    arg.val = 0;
    if (semctl(sem_id, SEM_FERRY_WAIT_KA, SETVAL, arg) == -1) {
        fatal_error("[MAIN] Błąd semctl SEM_FERRY_WAIT_KA");
    }
    if (semctl(sem_id, SEM_FERRY_WAIT_AK, SETVAL, arg) == -1) {
        fatal_error("[MAIN] Błąd semctl SEM_FERRY_WAIT_AK");
    }
    if (semctl(sem_id, SEM_FERRY_VIP_WAIT_KA, SETVAL, arg) == -1) {
        fatal_error("[MAIN] Błąd semctl SEM_FERRY_VIP_WAIT_KA");
    }
    if (semctl(sem_id, SEM_FERRY_VIP_WAIT_AK, SETVAL, arg) == -1) {
        fatal_error("[MAIN] Błąd semctl SEM_FERRY_VIP_WAIT_AK");
    }

    arg.val = X3_FERRY_CAP;
    if (semctl(sem_id, SEM_FERRY_CAP, SETVAL, arg) == -1) {
        fatal_error("[MAIN] Błąd semctl SEM_FERRY_CAP");
    }

    // inicjalizacja semaforow dla grup
    for (int i = 0; i < MAX_GROUPS; i++) {
        arg.val = 0;
        if (semctl(sem_id, SEM_GROUP_DONE(i), SETVAL, arg) == -1) {
            fatal_error("[MAIN] Błąd semctl SEM_GROUP_DONE");
        }
    }

    for (int i = 0; i < MAX_GROUPS; i++) {
        arg.val = 0;
        if (semctl(sem_id, SEM_BRIDGE_GUIDE_READY(i), SETVAL, arg) == -1) {
            fatal_error("[MAIN] Błąd semctl SEM_BRIDGE_GUIDE_READY");
        }
    }

    for (int i = 0; i < MAX_GROUPS; i++) {
        arg.val = 0;
        if (semctl(sem_id, SEM_FERRY_GUIDE_READY(i), SETVAL, arg) == -1) {
            fatal_error("[MAIN] Błąd semctl SEM_FERRY_GUIDE_READY");
        }
    }

    arg.val = 1;
    if (semctl(sem_id, SEM_GROUP_MUTEX, SETVAL, arg) == -1) {
        fatal_error("[MAIN] Błąd semctl SEM_GROUP_MUTEX");
    }

    // inicjalizacja semaforow turystow
    for (int i = 0; i < N_PARK_CAPACITY; i++) {
        arg.val = 0;  

        if (semctl(sem_id, SEM_TOURIST_ASSIGNED(i), SETVAL, arg) == -1) {
            fatal_error("[MAIN] Błąd semctl SEM_TOURIST_ASSIGNED");
        }
    }

    for (int i = 0; i < N_PARK_CAPACITY; i++) {
        arg.val = 0;  

        if (semctl(sem_id, SEM_TOURIST_READ_DONE(i), SETVAL, arg) == -1) {
            fatal_error("[MAIN] Błąd semctl SEM_TOURIST_READ_DONE");
        }
    }

    for (int g = 0; g < MAX_GROUPS; g++) {
        for (int m = 0; m < M_GROUP_SIZE; m++) {
            arg.val = 0;
            if (semctl(sem_id, SEM_MEMBER_GO(g, m), SETVAL, arg) == -1) {
                fatal_error("[MAIN] Błąd semctl SEM_MEMBER_GO");
            }
        }
    }

    arg.val = N_PARK_CAPACITY;
    if (semctl(sem_id, SEM_QUEUE_SLOTS, SETVAL, arg) == -1) {
        fatal_error("[MAIN] Błąd semctl SEM_QUEUE_SLOTS");
    }

    arg.val = MAX_GROUPS;
    if (semctl(sem_id, SEM_GROUP_SLOTS, SETVAL, arg) == -1) {
        fatal_error("[MAIN] Błąd semctl SEM_GROUP_SLOTS");
    }

    arg.val = 0;
    if (semctl(sem_id, SEM_TOWER_WAIT, SETVAL, arg) == -1) {
        fatal_error("[MAIN] Błąd semctl SEM_TOWER_WAIT");
    }

    arg.val = 0;
    if (semctl(sem_id, SEM_TOWER_VIP_WAIT, SETVAL, arg) == -1) {
        fatal_error("[MAIN] Błąd semctl SEM_TOWER_VIP_WAIT");
    }

    arg.val = 0;
    if (semctl(sem_id, SEM_TOWER_NORMAL_WAIT, SETVAL, arg) == -1) {
        fatal_error("[MAIN] Błąd semctl SEM_TOWER_NORMAL_WAIT");
    }

    arg.val = 1;
    if (semctl(sem_id, SEM_CASH_QUEUE_MUTEX, SETVAL, arg) == -1) {
        fatal_error("[MAIN] Błąd semctl SEM_CASH_QUEUE_MUTEX");
    }

    arg.val = N_PARK_CAPACITY;
    if (semctl(sem_id, SEM_CASH_QUEUE_SLOTS, SETVAL, arg) == -1) {
        fatal_error("[MAIN] Błąd semctl SEM_CASH_QUEUE_SLOTS");
    }

    printf(CLR_WHITE "[MAIN] Semafory zainicjalizowane pomyślnie." CLR_RESET "\n");
}

// inicjalizacja pamieci dzielonej
void init_shared_memory(struct ParkSharedMemory *park, int num_tourists, int park_duration) {
    printf(CLR_WHITE "[MAIN] Inicjalizacja pamięci dzielonej..." CLR_RESET "\n");

    memset(park, 0, sizeof(struct ParkSharedMemory));

    park->park_open_time = time(NULL);
    park->park_closing_time = park->park_open_time + park_duration;
    park->park_closed = 0;

    park->total_expected = num_tourists;
    printf(CLR_WHITE "[MAIN] Oczekiwana liczba turystów: %d" CLR_RESET "\n", num_tourists);
    printf(CLR_WHITE "[MAIN] Park otwarty od teraz (Tp), zamknięcie za %d sekund (Tk)" CLR_RESET "\n", park_duration);

    // zerowanie licznikow atrakcji
    park->bridge_direction = DIR_NONE;
    park->bridge_on_bridge = 0;
    park->bridge_waiting[0] = 0;
    park->bridge_waiting[1] = 0;
    park->queue_head = 0;
    park->queue_tail = 0;
    park->ferry_position = 0;
    park->ferry_passengers = 0;
    park->ferry_expected = 0;
    park->ferry_disembarked = 0;
    park->ferry_current_group = -1;
    park->ferry_on_ferry = 0;
    park->ferry_direction = DIR_NONE;
    park->ferry_waiting_vip[0] = 0;
    park->ferry_waiting_vip[1] = 0;
    park->ferry_waiting_normal[0] = 0;
    park->ferry_waiting_normal[1] = 0;
    park->next_group_slot = 0;
    park->tower_waiting_vip = 0;
    park->tower_waiting_normal = 0;

    for (int i = 0; i < N_PARK_CAPACITY; i++) {
        park->assigned_group_id[i] = -1;
    }

    for (int i = 0; i < N_PARK_CAPACITY; i++) {
        park->assigned_member_index[i] = -1;
    }

    for (int i = 0; i < MAX_GROUPS; i++) {
        park->groups[i].active = 0;
        park->groups[i].guide_id = -1;
    }

    printf(CLR_WHITE "[MAIN] Pamięć dzielona zainicjowana pomyślnie." CLR_RESET "\n");
}

// usuwanie starych zasobow ipc jesli istnieja
void cleanup_old_ipc() {
    printf(CLR_WHITE "[MAIN-INIT] Sprawdzanie starych zasobów IPC..." CLR_RESET "\n");

    int old_msg_id = msgget(ftok(FTOK_PATH, FTOK_MSG_ID), 0600);
    if (old_msg_id != -1) {
        msgctl(old_msg_id, IPC_RMID, NULL);
        printf(CLR_WHITE "[MAIN-INIT] Wykryto i usunięto starą kolejkę komunikatów." CLR_RESET "\n");
    }

    int old_report_id = msgget(ftok(FTOK_PATH, FTOK_MSG_REPORT_ID), 0600);
    if (old_report_id != -1) {
        msgctl(old_report_id, IPC_RMID, NULL);
        printf(CLR_WHITE "[MAIN-INIT] Wykryto i usunięto starą kolejkę raportową." CLR_RESET "\n");
    }

    int old_shm_id = shmget(ftok(FTOK_PATH, FTOK_SHM_ID), 1, 0600);
    if (old_shm_id != -1) {
        shmctl(old_shm_id, IPC_RMID, NULL);
        printf(CLR_WHITE "[MAIN-INIT] Wykryto i usunięto starą pamięć dzieloną." CLR_RESET "\n");
    }

    int old_sem_id = semget(ftok(FTOK_PATH, FTOK_SEM_ID), 1, 0600);
    if (old_sem_id != -1) {
        if (semctl(old_sem_id, 0, IPC_RMID) == -1) {
            report_error("[MAIN-INIT] Błąd semctl IPC_RMID (stare semafory)");
        } else {
            printf(CLR_WHITE "[MAIN-INIT] Wykryto i usunięto stare semafory." CLR_RESET "\n");
        }
    }

    unlink(FIFO_PATH);
}

int main() {

    // sprzatanie przed startem
    cleanup_old_ipc();

    // rejestracja funkcji sprzatajacej na wyjsciu
    atexit(cleanup);

    // rejestracja obslugi sygnalow
    struct sigaction sa_int;
    sa_int.sa_handler = handle_sigint;
    if (sigemptyset(&sa_int.sa_mask) == -1) {
        fatal_error("[MAIN] Błąd sigemptyset(SIGINT)");
    }
    sa_int.sa_flags = 0;
    if (sigaction(SIGINT, &sa_int, NULL) == -1) {
        fatal_error("[MAIN] Błąd sigaction(SIGINT)");
    }

    struct sigaction sa_chld;
    sa_chld.sa_handler = handle_sigchld;
    if (sigemptyset(&sa_chld.sa_mask) == -1) {
        fatal_error("[MAIN] Błąd sigemptyset(SIGCHLD)");
    }
    sa_chld.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa_chld, NULL) == -1) {
        fatal_error("[MAIN] Błąd sigaction(SIGCHLD)");
    }

    // interfejs uzytkownika - parametry symulacji
    printf(CLR_BOLD CLR_WHITE "==========================" CLR_RESET "\n");
    printf(CLR_BOLD CLR_GREEN "SYMULACJA PARKU NARODOWEGO" CLR_RESET "\n");
    printf(CLR_BOLD CLR_WHITE "==========================" CLR_RESET "\n");
    int num_tourists = get_input("Podaj liczbę turystów", 5, 30000);
    int num_guides = get_input("Podaj liczbę przewodników", 1, MAX_GROUPS);
    int park_duration = get_input("Podaj czas otwarcia parku w sekundach (Tk)", 10, 600);

    // walidacja pojemnosci promu
    if (X3_FERRY_CAP < M_GROUP_SIZE + 1) {
        fprintf(stderr, CLR_RED "[MAIN] Błąd konfiguracji: X3_FERRY_CAP (%d) < M_GROUP_SIZE+1 (%d). "
                                "Prom nie pomieści grupy z przewodnikiem.\n" CLR_RESET, X3_FERRY_CAP, M_GROUP_SIZE + 1);
        exit(1);
    }

    printf("\n");

    // tworzenie zasobow ipc (pamiec, semafory, kolejki)
    shm_id = shmget(ftok(FTOK_PATH, FTOK_SHM_ID), sizeof(struct ParkSharedMemory), IPC_CREAT | 0600);
    if (shm_id == -1) {
        fatal_error("[MAIN] Błąd shmget");
    }
    printf(CLR_WHITE "[MAIN] Pamięć dzielona utworzona (ID: %d, rozmiar: %zu bajtów)." CLR_RESET "\n", shm_id, sizeof(struct ParkSharedMemory));

    struct ParkSharedMemory *park = (struct ParkSharedMemory*)shmat(shm_id, NULL, 0);
    if (park == (void*)-1) {
        fatal_error("[MAIN] Błąd shmat");
    }

    init_shared_memory(park, num_tourists, park_duration);

    sem_id = semget(ftok(FTOK_PATH, FTOK_SEM_ID), TOTAL_SEMAPHORES, IPC_CREAT | 0600);
    if (sem_id == -1) {
        fatal_error("[MAIN] Błąd semget");
    }
    printf(CLR_WHITE "[MAIN] Zestaw semaforów utworzony (ID: %d, liczba: %d)." CLR_RESET "\n", sem_id, TOTAL_SEMAPHORES);

    init_semaphores(sem_id);

    msg_id = msgget(ftok(FTOK_PATH, FTOK_MSG_ID), IPC_CREAT | 0600);
    if (msg_id == -1) {
        fatal_error("[MAIN] Błąd msgget");
    }
    printf(CLR_WHITE "[MAIN] Kolejka komunikatów utworzona (ID: %d)." CLR_RESET "\n", msg_id);

    report_msg_id = msgget(ftok(FTOK_PATH, FTOK_MSG_REPORT_ID), IPC_CREAT | 0600);
    if (report_msg_id == -1) {
        fatal_error("[MAIN] Błąd msgget (report queue)");
    }
    printf(CLR_WHITE "[MAIN] Kolejka raportowa utworzona (ID: %d)." CLR_RESET "\n", report_msg_id);

    if (mkfifo(FIFO_PATH, 0600) == -1) {
        fatal_error("[MAIN] Błąd mkfifo");
    }
    printf(CLR_WHITE "[MAIN] FIFO utworzone (%s)." CLR_RESET "\n", FIFO_PATH);

    // otwarcie fifo w trybie nonblock zeby nie blokowac
    dummy_fifo_fd = open(FIFO_PATH, O_RDWR | O_NONBLOCK);
    if (dummy_fifo_fd == -1) {
        report_error("[MAIN] Ostrzeżenie: Nie udało się otworzyć dummy FIFO");
    }

    printf("\n" CLR_WHITE "[MAIN] Zatrudniam kasjera..." CLR_RESET "\n");

    // uruchomienie procesu kasjera
    pid_t kasjer_pid = fork();
    if (kasjer_pid == -1) {
        fatal_error("[MAIN] Błąd fork (kasjer)");
    }
    if (kasjer_pid == 0) {

        execl("./kasjer", "kasjer", "1", NULL);
        fatal_error("[MAIN] Błąd execl kasjer");
    }

    printf(CLR_WHITE "[MAIN] Uruchamiam przewodnika-raportera..." CLR_RESET "\n");
    pid_t reporter_pid = fork();
    if (reporter_pid == -1) {
        fatal_error("[MAIN] Błąd fork (przewodnik-raporter)");
    }
    if (reporter_pid == 0) {
        execl("./przewodnik", "przewodnik", "reporter", NULL);
        fatal_error("[MAIN] Błąd execl przewodnik-raporter");
    }

    printf(CLR_WHITE "[MAIN] Zatrudniam %d przewodników..." CLR_RESET "\n", num_guides);

    // uruchomienie procesow przewodnikow
    for (int i = 1; i <= num_guides; i++) {
        pid_t pid = fork();
        if (pid == -1) {
            fatal_error("[MAIN] Błąd fork (przewodnik)");
        }
        if (pid == 0) {

            char id_buff[16];
            sprintf(id_buff, "%d", i);
            execl("./przewodnik", "przewodnik", id_buff, NULL);
            fatal_error("[MAIN] Błąd execl przewodnik");
        }

    }

    printf("\n" CLR_WHITE "[MAIN] System gotowy. Naciśnij Ctrl + C aby zakończyć." CLR_RESET "\n\n");

    printf(CLR_WHITE "[MAIN] Rozpoczynam generowanie %d turystów..." CLR_RESET "\n", num_tourists);

    srand(time(NULL));

    int created_tourists = 0;
    int finished_tourists = 0;
    int rejected_after_tk = 0;
    #define REJECT_SHOW_COUNT 100

    // petla generujaca turystow
    for (int i = 1; i <= num_tourists; i++) {

        // sprawdzenie czasu zamkniecia parku
        if (!park->park_closed && time(NULL) >= park->park_closing_time) {
            printf(CLR_YELLOW "\n[MAIN] Park zamknięty! Nowi turyści będą odrzucani." CLR_RESET "\n");
            park->park_closed = 1;
        }

        
        if (park->park_closed) {
            rejected_after_tk++;
            if (rejected_after_tk <= REJECT_SHOW_COUNT) {
                // opcjonalne logowanie odrzuconych
            } else {
                continue; // pomijanie tworzenia procesu
            }
        }

        pid_t pid = fork();
        if (pid == -1) {
            if (errno == EAGAIN || errno == ENOMEM) {
                // obsluga bledu braku zasobow na procesy
                int reaped = 0;
                while (waitpid(-1, NULL, WNOHANG) > 0) {
                    finished_tourists++;
                    reaped++;
                }
                if (reaped > 0) {
                    i--; // ponowienie proby
                    continue;
                }
                if (waitpid(-1, NULL, 0) > 0) {
                    finished_tourists++;
                    i--;
                    continue;
                }
            report_error("[MAIN] Błąd fork (turysta) - nie można zwolnić procesów");
                fprintf(stderr, CLR_RED "[MAIN] Kontynuuję z %d turystami." CLR_RESET "\n", created_tourists);
                break;
            }
            report_error("[MAIN] Błąd fork (turysta) - błąd krytyczny");
            fprintf(stderr, CLR_RED "[MAIN] Kontynuuję z %d turystami." CLR_RESET "\n", created_tourists);
            break;
        }

        if (pid == 0) {
            char id_buff[16];
            sprintf(id_buff, "%d", i);
            execl("./turysta", "turysta", id_buff, NULL);
            fatal_error("[MAIN-CHILD] Błąd execl turysta");
        }

        created_tourists++;

        if (!park->park_closed) {
            //sim_sleep(TOURIST_ARRIVAL_MIN, TOURIST_ARRIVAL_MAX, 0);
        }

        if (i % 100 == 0) {
            printf(CLR_WHITE "[MAIN] Wygenerowano %d/%d turystów" CLR_RESET "\n", i, num_tourists);
        }
    }

    // korekta liczby oczekiwanych turystow jesli nie udalo sie stworzyc wszystkich
    if (created_tourists < num_tourists) {
        int not_created = num_tourists - created_tourists;
        sem_lock(sem_id, SEM_STATS_MUTEX);
        park->total_expected -= not_created;
        sem_unlock(sem_id, SEM_STATS_MUTEX);
        printf(CLR_WHITE "[MAIN] Zaktualizowano total_expected: %d (nie stworzono %d turystów)" CLR_RESET "\n", park->total_expected, not_created);
    }

    // obsluga resztek w kolejce po zamknieciu
    if (park->park_closed) {
        sem_lock(sem_id, SEM_QUEUE_MUTEX);
        int in_queue = park->people_in_queue;
        sem_unlock(sem_id, SEM_QUEUE_MUTEX);
        if (in_queue > 0) {
            printf(CLR_YELLOW "[MAIN] Park zamknięty, w kolejce %d osób - budzę przewodnika dla niepełnej grupy." CLR_RESET "\n", in_queue);
            sem_unlock(sem_id, SEM_PRZEWODNIK);
        }
    }

    printf("\n" CLR_WHITE "[MAIN] Wygenerowano %d turystów. Czekam na zakończenie zwiedzania..." CLR_RESET "\n", created_tourists);
    
    // blokowanie sygnalu sigchld na czas czekania
    sigset_t block_mask;
    sigset_t prev_mask;
    if (sigemptyset(&block_mask) == -1) {
        fatal_error("[MAIN] Błąd sigemptyset(block_mask)");
    }
    if (sigaddset(&block_mask, SIGCHLD) == -1) {
        fatal_error("[MAIN] Błąd sigaddset(SIGCHLD)");
    }
    if (sigprocmask(SIG_BLOCK, &block_mask, &prev_mask) == -1) {
        fatal_error("[MAIN] Błąd sigprocmask(SIG_BLOCK)");
    }

    // petla oczekiwania na wyjscie wszystkich turystow
    while (1) {
        sem_lock(sem_id, SEM_STATS_MUTEX);
        int exited = park->total_exited;
        int entered = park->total_entered;
        int expected = park->total_expected;
        int in_park = park->people_in_park;
        sem_unlock(sem_id, SEM_STATS_MUTEX);

        if (exited >= expected) {
            break; // wszyscy wyszli
        }
        if (entered >= expected && in_park == 0) {
            break; // wszyscy ktorzy weszli juz wyszli
        }

        // czekanie na sygnal
        if (sigsuspend(&prev_mask) == -1 && errno != EINTR) {
            report_error("[MAIN] Błąd sigsuspend");
        }
    }

    if (sigprocmask(SIG_SETMASK, &prev_mask, NULL) == -1) {
        report_error("[MAIN] Błąd sigprocmask(SIG_SETMASK)");
    }

    printf("\n" CLR_WHITE "[MAIN] Wszyscy turyści zakończyli procesy. Wysyłam sygnał do kasjera..." CLR_RESET "\n");

    // zamykanie kasjera
    if (kill(kasjer_pid, SIGTERM) == -1) {
        report_error("[MAIN] Błąd kill(SIGTERM) kasjer");
    }

    printf(CLR_WHITE "[MAIN] Czekam na zakończenie kasjera (przetwarzanie ostatnich wiadomości)..." CLR_RESET "\n");
    if (waitpid(kasjer_pid, NULL, 0) == -1 && errno != ECHILD) {
        report_error("[MAIN] Błąd waitpid(kasjer)");
    }

    printf(CLR_WHITE "[MAIN] Kasjer zakończył pracę. Generuję statystyki..." CLR_RESET "\n");

    // raport koncowy
    printf("\n" CLR_BOLD CLR_WHITE "============== STATYSTYKI PARKU ==============" CLR_RESET "\n");
    {
        time_t open_t = park->park_open_time;
        time_t close_t = park->park_closing_time;
        struct tm *ot = localtime(&open_t);
        char open_buf[32];
        strftime(open_buf, sizeof(open_buf), "%H:%M:%S", ot);
        struct tm *clt = localtime(&close_t);
        char close_buf[32];
        strftime(close_buf, sizeof(close_buf), "%H:%M:%S", clt);
        printf(CLR_WHITE "Godzina otwarcia (Tp):   %s" CLR_RESET "\n", open_buf);
        printf(CLR_WHITE "Godzina zamknięcia (Tk): %s" CLR_RESET "\n", close_buf);
        printf(CLR_WHITE "Czas otwarcia:           %d sekund" CLR_RESET "\n", park_duration);
    }
    printf(CLR_WHITE "Liczba przewodników:     %d" CLR_RESET "\n", num_guides);
    printf(CLR_WHITE "Wygenerowani turyści:    %d" CLR_RESET "\n", num_tourists);
    printf(CLR_WHITE "Weszło do parku:         %d" CLR_RESET "\n", park->total_entered);
    printf(CLR_WHITE "Wyszło z parku:          %d" CLR_RESET "\n", park->total_exited);
    printf(CLR_WHITE "Różnica (w parku):       %d" CLR_RESET "\n", park->total_entered - park->total_exited);
    printf(CLR_WHITE "Bilety płatne:           %d" CLR_RESET "\n", park->paid_entries);
    printf(CLR_WHITE "Wejścia darmowe VIP:     %d" CLR_RESET "\n", park->free_entries_vip);
    printf(CLR_WHITE "Wejścia darmowe dzieci:  %d" CLR_RESET "\n", park->free_entries_children);
    printf(CLR_WHITE "Nie stworzeni:           %d" CLR_RESET "\n", num_tourists - created_tourists);
    printf(CLR_WHITE "Odrzuceni po Tk:         %d" CLR_RESET "\n", park->rejected_after_close);
    printf(CLR_WHITE "Przychód (PLN):          %d" CLR_RESET "\n", park->total_revenue);
    printf(CLR_BOLD CLR_WHITE "----------------------------------------------" CLR_RESET "\n");

    if (park->total_entered == park->total_exited && park->people_in_park == 0) {
        printf(CLR_GREEN "Status: Sukces - wszyscy weszli i wyszli z parku!" CLR_RESET "\n");
    } else if (park->total_entered > park->total_exited) {
        printf(CLR_YELLOW "Status: Nie wszyscy wyszli z parku (w parku: %d)" CLR_RESET "\n", park->people_in_park);
    } else {
        printf(CLR_GREEN "Status: Park zamknięty, ruch zakończony." CLR_RESET "\n");
    }
    printf(CLR_BOLD CLR_WHITE "==============================================" CLR_RESET "\n\n");

    if (shmdt(park) == -1) {
        report_error("[MAIN] Błąd shmdt");
    }
    return 0;
}