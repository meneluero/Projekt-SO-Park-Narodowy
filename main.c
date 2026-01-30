#include "common.h"
#include <signal.h>
#include <string.h>
#include <sys/wait.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>

int shm_id = -1;
int sem_id = -1;
int msg_id = -1;
int cleanup_done = 0; 

int dummy_fifo_fd = -1;

void cleanup() {
    if (cleanup_done) return;
    cleanup_done = 1;

    if (dummy_fifo_fd != -1) {
        close(dummy_fifo_fd);
    }

    printf("\n" CLR_WHITE "[MAIN] Rozpoczęto sprzątanie zasobów..." CLR_RESET "\n");

    if (unlink(FIFO_PATH) == -1 && errno != ENOENT) {
        report_error("[MAIN] Błąd unlink FIFO");
    } else {
        printf(CLR_WHITE "[MAIN] FIFO usunięte." CLR_RESET "\n");
    }

    struct sigaction sa;
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, NULL);

    kill(0, SIGTERM);

    while (waitpid(-1, NULL, 0) > 0 || errno == EINTR);

    printf(CLR_WHITE "[MAIN] Procesy potomne posprzątane." CLR_RESET "\n");

    if (msg_id != -1) {
        if (msgctl(msg_id, IPC_RMID, NULL) == -1) {
            report_error("[MAIN] Błąd msgctl");
        } else {
            printf(CLR_WHITE "[MAIN] Kolejka usunięta." CLR_RESET "\n");
        }
    }

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

void handle_sigint(int sig) {
    printf("\n" CLR_WHITE "[MAIN] Otrzymano sygnał %d (Ctrl + C). Kończę program." CLR_RESET "\n", sig);
    exit(0); 

}

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
            while (getchar() != '\n');  

        }
    }
}

void init_semaphores(int sem_id) {
    union semun arg;

    printf(CLR_WHITE "[MAIN] Inicjalizacja %d semaforów..." CLR_RESET "\n", TOTAL_SEMAPHORES);

    arg.val = N_PARK_CAPACITY;
    if (semctl(sem_id, SEM_PARK_LIMIT, SETVAL, arg) == -1) {
        fatal_error("[MAIN] Błąd semctl SEM_PARK_LIMIT");
    }

    arg.val = 0;
    if (semctl(sem_id, SEM_PRZEWODNIK, SETVAL, arg) == -1) {
        fatal_error("[MAIN] Błąd semctl SEM_PRZEWODNIK");
    }

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

    arg.val = 0;
    if (semctl(sem_id, SEM_BRIDGE_WAIT_KA, SETVAL, arg) == -1) {
        fatal_error("[MAIN] Błąd semctl SEM_BRIDGE_WAIT_KA");
    }
    if (semctl(sem_id, SEM_BRIDGE_WAIT_AK, SETVAL, arg) == -1) {
        fatal_error("[MAIN] Błąd semctl SEM_BRIDGE_WAIT_AK");
    }

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

    arg.val = 1;
    if (semctl(sem_id, SEM_PROM_MUTEX, SETVAL, arg) == -1) {
        fatal_error("[MAIN] Błąd semctl SEM_PROM_MUTEX");
    }

    arg.val = 1;
    if (semctl(sem_id, SEM_FERRY_CONTROL, SETVAL, arg) == -1) {
        fatal_error("[MAIN] Błąd semctl SEM_FERRY_CONTROL");
    }

    arg.val = 0;
    if (semctl(sem_id, SEM_FERRY_BOARD, SETVAL, arg) == -1) {
        fatal_error("[MAIN] Błąd semctl SEM_FERRY_BOARD");
    }

    arg.val = 0;
    if (semctl(sem_id, SEM_FERRY_BOARD_VIP, SETVAL, arg) == -1) {
        fatal_error("[MAIN] Błąd semctl SEM_FERRY_BOARD_VIP");
    }

    arg.val = 0;
    if (semctl(sem_id, SEM_FERRY_ALL_ABOARD, SETVAL, arg) == -1) {
        fatal_error("[MAIN] Błąd semctl SEM_FERRY_ALL_ABOARD");
    }

    arg.val = 0;
    if (semctl(sem_id, SEM_FERRY_ARRIVE, SETVAL, arg) == -1) {
        fatal_error("[MAIN] Błąd semctl SEM_FERRY_ARRIVE");
    }

    arg.val = 0;
    if (semctl(sem_id, SEM_FERRY_DISEMBARK, SETVAL, arg) == -1) {
        fatal_error("[MAIN] Błąd semctl SEM_FERRY_DISEMBARK");
    }

    for (int i = 0; i < MAX_GROUPS; i++) {
        arg.val = 0;
        if (semctl(sem_id, SEM_GROUP_DONE(i), SETVAL, arg) == -1) {
            fatal_error("[MAIN] Błąd semctl SEM_GROUP_DONE");
        }
    }

    arg.val = 1;
    if (semctl(sem_id, SEM_GROUP_MUTEX, SETVAL, arg) == -1) {
        fatal_error("[MAIN] Błąd semctl SEM_GROUP_MUTEX");
    }

    for (int i = 0; i < M_GROUP_SIZE; i++) {
        arg.val = 0;  

        if (semctl(sem_id, SEM_TOURIST_ASSIGNED(i), SETVAL, arg) == -1) {
            fatal_error("[MAIN] Błąd semctl SEM_TOURIST_ASSIGNED");
        }
    }

    for (int i = 0; i < M_GROUP_SIZE; i++) {
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

    arg.val = M_GROUP_SIZE;
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

void init_shared_memory(struct ParkSharedMemory *park, int num_tourists) {
    printf(CLR_WHITE "[MAIN] Inicjalizacja pamięci dzielonej..." CLR_RESET "\n");

    memset(park, 0, sizeof(struct ParkSharedMemory));

    park->total_expected = num_tourists;
    printf(CLR_WHITE "[MAIN] Oczekiwana liczba turystów: %d" CLR_RESET "\n", num_tourists);

    park->bridge_direction = DIR_NONE;
    park->bridge_on_bridge = 0;
    park->bridge_waiting[0] = 0;
    park->bridge_waiting[1] = 0;
    park->ferry_position = 0;
    park->ferry_passengers = 0;
    park->ferry_expected = 0;
    park->ferry_disembarked = 0;
    park->ferry_current_group = -1;
    park->next_group_slot = 0;

    for (int i = 0; i < M_GROUP_SIZE; i++) {
        park->assigned_group_id[i] = -1;
    }

    for (int i = 0; i < M_GROUP_SIZE; i++) {
        park->assigned_member_index[i] = -1;
    }

    for (int i = 0; i < MAX_GROUPS; i++) {
        park->groups[i].active = 0;
        park->groups[i].guide_id = -1;
    }

    printf(CLR_WHITE "[MAIN] Pamięć dzielona zainicjowana pomyślnie." CLR_RESET "\n");
}

void cleanup_old_ipc() {
    printf(CLR_WHITE "[MAIN-INIT] Sprawdzanie starych zasobów IPC..." CLR_RESET "\n");

    int old_msg_id = msgget(MSG_KEY_ID, 0600);
    if (old_msg_id != -1) {
        msgctl(old_msg_id, IPC_RMID, NULL);
        printf(CLR_WHITE "[MAIN-INIT] Wykryto i usunięto starą kolejkę komunikatów." CLR_RESET "\n");
    }

    int old_shm_id = shmget(SHM_KEY_ID, sizeof(struct ParkSharedMemory), 0600);
    if (old_shm_id != -1) {
        shmctl(old_shm_id, IPC_RMID, NULL);
        printf(CLR_WHITE "[MAIN-INIT] Wykryto i usunięto starą pamięć dzieloną." CLR_RESET "\n");
    }

    int old_sem_id = semget(SEM_KEY_ID, TOTAL_SEMAPHORES, 0600);
    if (old_sem_id == -1) {
        old_sem_id = semget(SEM_KEY_ID, 92, 0600);
    }
    if (old_sem_id == -1) {
        old_sem_id = semget(SEM_KEY_ID, 11, 0600);
    }
    if (old_sem_id != -1) {
        semctl(old_sem_id, 0, IPC_RMID);
        printf(CLR_WHITE "[MAIN-INIT] Wykryto i usunięto stare semafory." CLR_RESET "\n");
    }

    unlink(FIFO_PATH);
}

int main() {

    cleanup_old_ipc();

    atexit(cleanup);

    struct sigaction sa_int;
    sa_int.sa_handler = handle_sigint;
    sigemptyset(&sa_int.sa_mask);
    sa_int.sa_flags = 0;
    sigaction(SIGINT, &sa_int, NULL);
    printf(CLR_BOLD CLR_WHITE "==========================" CLR_RESET "\n");
    printf(CLR_BOLD CLR_GREEN "SYMULACJA PARKU NARODOWEGO" CLR_RESET "\n");
    printf(CLR_BOLD CLR_WHITE "==========================" CLR_RESET "\n");
    int num_tourists = get_input("Podaj liczbę turystów", 5, 30000);
    int num_guides = get_input("Podaj liczbę przewodników", 1, MAX_GROUPS);

    printf("\n");

    shm_id = shmget(SHM_KEY_ID, sizeof(struct ParkSharedMemory), IPC_CREAT | 0600);
    if (shm_id == -1) {
        fatal_error("[MAIN] Błąd shmget");
    }
    printf(CLR_WHITE "[MAIN] Pamięć dzielona utworzona (ID: %d, rozmiar: %zu bajtów)." CLR_RESET "\n", shm_id, sizeof(struct ParkSharedMemory));

    struct ParkSharedMemory *park = (struct ParkSharedMemory*)shmat(shm_id, NULL, 0);
    if (park == (void*)-1) {
        fatal_error("[MAIN] Błąd shmat");
    }

    init_shared_memory(park, num_tourists);

    sem_id = semget(SEM_KEY_ID, TOTAL_SEMAPHORES, IPC_CREAT | 0600);
    if (sem_id == -1) {
        fatal_error("[MAIN] Błąd semget");
    }
    printf(CLR_WHITE "[MAIN] Zestaw semaforów utworzony (ID: %d, liczba: %d)." CLR_RESET "\n", sem_id, TOTAL_SEMAPHORES);

    init_semaphores(sem_id);

    msg_id = msgget(MSG_KEY_ID, IPC_CREAT | 0600);
    if (msg_id == -1) {
        fatal_error("[MAIN] Błąd msgget");
    }
    printf(CLR_WHITE "[MAIN] Kolejka komunikatów utworzona (ID: %d)." CLR_RESET "\n", msg_id);

    if (mkfifo(FIFO_PATH, 0660) == -1) {
        fatal_error("[MAIN] Błąd mkfifo");
    }
    printf(CLR_WHITE "[MAIN] FIFO utworzone (%s)." CLR_RESET "\n", FIFO_PATH);

    dummy_fifo_fd = open(FIFO_PATH, O_RDWR | O_NONBLOCK);
    if (dummy_fifo_fd == -1) {
        report_error("[MAIN] Ostrzeżenie: Nie udało się otworzyć dummy FIFO");
    }

    printf("\n" CLR_WHITE "[MAIN] Zatrudniam kasjera..." CLR_RESET "\n");

    pid_t kasjer_pid = fork();
    if (kasjer_pid == -1) {
        fatal_error("[MAIN] Błąd fork (kasjer)");
    }
    if (kasjer_pid == 0) {

        execl("./kasjer", "kasjer", "1", NULL);
        fatal_error("[MAIN] Błąd execl kasjer");
    }

    printf(CLR_WHITE "[MAIN] Zatrudniam %d przewodników..." CLR_RESET "\n", num_guides);

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
    for (int i = 1; i <= num_tourists; i++) {
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

        if (i % 100 == 0) {
            printf(CLR_WHITE "[MAIN] Wygenerowano %d/%d turystów" CLR_RESET "\n", i, num_tourists);
        }
    }

    if (created_tourists < num_tourists) {
        sem_lock(sem_id, SEM_STATS_MUTEX);
        park->total_expected = created_tourists;
        sem_unlock(sem_id, SEM_STATS_MUTEX);
        printf(CLR_WHITE "[MAIN] Zaktualizowano total_expected: %d (planowano %d)" CLR_RESET "\n", created_tourists, num_tourists);
    }

    printf("\n" CLR_WHITE "[MAIN] Wygenerowano %d turystów. Czekam na zakończenie zwiedzania..." CLR_RESET "\n", created_tourists);
    int remaining = created_tourists - finished_tourists;
    for (int i = 0; i < remaining; i++) {
        wait(NULL);
    }

    printf("\n" CLR_WHITE "[MAIN] Wszyscy turyści zakończyli procesy. Wysyłam sygnał do kasjera..." CLR_RESET "\n");

    kill(kasjer_pid, SIGTERM);

    printf(CLR_WHITE "[MAIN] Czekam na zakończenie kasjera (przetwarzanie ostatnich wiadomości)..." CLR_RESET "\n");
    waitpid(kasjer_pid, NULL, 0);

    printf(CLR_WHITE "[MAIN] Kasjer zakończył pracę. Generuję statystyki..." CLR_RESET "\n");

    printf("\n" CLR_BOLD CLR_WHITE "============== STATYSTYKI PARKU ==============" CLR_RESET "\n");
    printf(CLR_WHITE "Liczba przewodników:     %d" CLR_RESET "\n", num_guides);
    printf(CLR_WHITE "Wygenerowani turyści:    %d" CLR_RESET "\n", num_tourists);
    printf(CLR_WHITE "Weszło do parku:         %d" CLR_RESET "\n", park->total_entered);
    printf(CLR_WHITE "Wyszło z parku:          %d" CLR_RESET "\n", park->total_exited);
    printf(CLR_WHITE "Różnica (w parku):       %d" CLR_RESET "\n", park->total_entered - park->total_exited);
    printf(CLR_BOLD CLR_WHITE "----------------------------------------------" CLR_RESET "\n");

    if (park->total_entered == num_tourists && park->total_exited == num_tourists) {
        printf(CLR_GREEN "Status: Sukces - wszyscy przeszli przez park!" CLR_RESET "\n");
    } else if (park->total_entered == num_tourists) {
        printf(CLR_YELLOW "Status: Wszyscy weszli, ale nie wszyscy wyszli" CLR_RESET "\n");
    } else {
        printf(CLR_RED "Status: Błąd - nie wszyscy weszli do parku" CLR_RESET "\n");
    }
    printf(CLR_BOLD CLR_WHITE "==============================================" CLR_RESET "\n\n");

    shmdt(park);
    return 0;
}