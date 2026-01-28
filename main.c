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

    printf("\n[MAIN] Rozpoczęto sprzątanie zasobów...\n");

    if (unlink(FIFO_PATH) == -1 && errno != ENOENT) {
        perror("[MAIN] Błąd unlink FIFO");
    } else {
        printf("[MAIN] FIFO usunięte.\n");
    }

    struct sigaction sa;
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, NULL);

    kill(0, SIGTERM);

    while (waitpid(-1, NULL, 0) > 0 || errno == EINTR);

    printf("[MAIN] Procesy potomne posprzątane.\n");

    if (msg_id != -1) {
        if (msgctl(msg_id, IPC_RMID, NULL) == -1) {
            perror("[MAIN] Błąd msgctl");
        } else {
            printf("[MAIN] Kolejka usunięta.\n");
        }
    }

    if (shm_id != -1) {
        if (shmctl(shm_id, IPC_RMID, NULL) == -1) {
            perror("[MAIN] Błąd shmctl");
        } else {
            printf("[MAIN] Pamięć dzielona usunięta.\n");
        }
    }

    if (sem_id != -1) {
        if (semctl(sem_id, 0, IPC_RMID) == -1) {
            perror("[MAIN] Błąd semctl");
        } else {
            printf("[MAIN] Semafory usunięte.\n");
        }
    }

    printf("[MAIN] System czysty. Zamykanie.\n");
}

void handle_sigint(int sig) {
    printf("\n[MAIN] Otrzymano sygnał %d (Ctrl + C). Kończę program.\n", sig);
    exit(0); 

}

int get_input(const char* prompt, int min, int max) {
    int value;
    while (1) {
        printf("%s (%d - %d): ", prompt, min, max);
        if (scanf("%d", &value) == 1) {
            if (value >= min && value <= max) {
                return value;
            } else {
                printf("Błąd: Wartość musi być z przedziału <%d, %d>!\n", min, max);
            }
        } else {
            printf("Błąd: To nie jest liczba!\n");
            while (getchar() != '\n');  

        }
    }
}

void init_semaphores(int sem_id) {
    union semun arg;

    printf("[MAIN] Inicjalizacja %d semaforów...\n", TOTAL_SEMAPHORES);

    arg.val = N_PARK_CAPACITY;
    if (semctl(sem_id, SEM_PARK_LIMIT, SETVAL, arg) == -1) {
        perror("[MAIN] Błąd semctl SEM_PARK_LIMIT");
        exit(1);
    }

    arg.val = 0;
    if (semctl(sem_id, SEM_PRZEWODNIK, SETVAL, arg) == -1) {
        perror("[MAIN] Błąd semctl SEM_PRZEWODNIK");
        exit(1);
    }

    arg.val = 1;
    if (semctl(sem_id, SEM_QUEUE_MUTEX, SETVAL, arg) == -1) {
        perror("[MAIN] Błąd semctl SEM_QUEUE_MUTEX");
        exit(1);
    }

    arg.val = 1;
    if (semctl(sem_id, SEM_STATS_MUTEX, SETVAL, arg) == -1) {
        perror("[MAIN] Błąd semctl SEM_STATS_MUTEX");
        exit(1);
    }

    arg.val = X1_BRIDGE_CAP;
    if (semctl(sem_id, SEM_MOST_LIMIT, SETVAL, arg) == -1) {
        perror("[MAIN] Błąd semctl SEM_MOST_LIMIT");
        exit(1);
    }

    arg.val = 1;
    if (semctl(sem_id, SEM_MOST_MUTEX, SETVAL, arg) == -1) {
        perror("[MAIN] Błąd semctl SEM_MOST_MUTEX");
        exit(1);
    }

    arg.val = 0;
    if (semctl(sem_id, SEM_BRIDGE_WAIT_KA, SETVAL, arg) == -1) {
        perror("[MAIN] Błąd semctl SEM_BRIDGE_WAIT_KA");
        exit(1);
    }
    if (semctl(sem_id, SEM_BRIDGE_WAIT_AK, SETVAL, arg) == -1) {
        perror("[MAIN] Błąd semctl SEM_BRIDGE_WAIT_AK");
        exit(1);
    }

    arg.val = X2_TOWER_CAP;
    if (semctl(sem_id, SEM_WIEZA_LIMIT, SETVAL, arg) == -1) {
        perror("[MAIN] Błąd semctl SEM_WIEZA_LIMIT");
        exit(1);
    }

    arg.val = 1;
    if (semctl(sem_id, SEM_WIEZA_MUTEX, SETVAL, arg) == -1) {
        perror("[MAIN] Błąd semctl SEM_WIEZA_MUTEX");
        exit(1);
    }

    arg.val = 1;
    if (semctl(sem_id, SEM_PROM_MUTEX, SETVAL, arg) == -1) {
        perror("[MAIN] Błąd semctl SEM_PROM_MUTEX");
        exit(1);
    }

    arg.val = 1;
    if (semctl(sem_id, SEM_FERRY_CONTROL, SETVAL, arg) == -1) {
        perror("[MAIN] Błąd semctl SEM_FERRY_CONTROL");
        exit(1);
    }

    arg.val = 0;
    if (semctl(sem_id, SEM_FERRY_BOARD, SETVAL, arg) == -1) {
        perror("[MAIN] Błąd semctl SEM_FERRY_BOARD");
        exit(1);
    }

    arg.val = 0;
    if (semctl(sem_id, SEM_FERRY_ALL_ABOARD, SETVAL, arg) == -1) {
        perror("[MAIN] Błąd semctl SEM_FERRY_ALL_ABOARD");
        exit(1);
    }

    arg.val = 0;
    if (semctl(sem_id, SEM_FERRY_ARRIVE, SETVAL, arg) == -1) {
        perror("[MAIN] Błąd semctl SEM_FERRY_ARRIVE");
        exit(1);
    }

    arg.val = 0;
    if (semctl(sem_id, SEM_FERRY_DISEMBARK, SETVAL, arg) == -1) {
        perror("[MAIN] Błąd semctl SEM_FERRY_DISEMBARK");
        exit(1);
    }

    for (int i = 0; i < MAX_GROUPS; i++) {
        arg.val = 0;
        if (semctl(sem_id, SEM_GROUP_DONE(i), SETVAL, arg) == -1) {
            perror("[MAIN] Błąd semctl SEM_GROUP_DONE");
            exit(1);
        }
    }

    arg.val = 1;
    if (semctl(sem_id, SEM_GROUP_MUTEX, SETVAL, arg) == -1) {
        perror("[MAIN] Błąd semctl SEM_GROUP_MUTEX");
        exit(1);
    }

    for (int i = 0; i < M_GROUP_SIZE; i++) {
        arg.val = 0;  

        if (semctl(sem_id, SEM_TOURIST_ASSIGNED(i), SETVAL, arg) == -1) {
            perror("[MAIN] Błąd semctl SEM_TOURIST_ASSIGNED");
            exit(1);
        }
    }

    for (int i = 0; i < M_GROUP_SIZE; i++) {
        arg.val = 0;  

        if (semctl(sem_id, SEM_TOURIST_READ_DONE(i), SETVAL, arg) == -1) {
            perror("[MAIN] Błąd semctl SEM_TOURIST_READ_DONE");
            exit(1);
        }
    }

    for (int g = 0; g < MAX_GROUPS; g++) {
        for (int m = 0; m < M_GROUP_SIZE; m++) {
            arg.val = 0;
            if (semctl(sem_id, SEM_MEMBER_GO(g, m), SETVAL, arg) == -1) {
                perror("[MAIN] Błąd semctl SEM_MEMBER_GO");
                exit(1);
            }
        }
    }

    arg.val = M_GROUP_SIZE;
    if (semctl(sem_id, SEM_QUEUE_SLOTS, SETVAL, arg) == -1) {
        perror("[MAIN] Błąd semctl SEM_QUEUE_SLOTS");
        exit(1);
    }

    arg.val = MAX_GROUPS;
    if (semctl(sem_id, SEM_GROUP_SLOTS, SETVAL, arg) == -1) {
        perror("[MAIN] Błąd semctl SEM_GROUP_SLOTS");
        exit(1);
    }

    arg.val = 0;
    if (semctl(sem_id, SEM_TOWER_WAIT, SETVAL, arg) == -1) {
        perror("[MAIN] Błąd semctl SEM_TOWER_WAIT");
        exit(1);
    }

    printf("[MAIN] Semafory zainicjalizowane pomyślnie.\n");
}

void init_shared_memory(struct ParkSharedMemory *park, int num_tourists) {
    printf("[MAIN] Inicjalizacja pamięci dzielonej...\n");

    memset(park, 0, sizeof(struct ParkSharedMemory));

    park->total_expected = num_tourists;
    printf("[MAIN] Oczekiwana liczba turystów: %d\n", num_tourists);

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

    printf("[MAIN] Pamięć dzielona zainicjowana pomyślnie.\n");
}

void cleanup_old_ipc() {
    printf("[MAIN-INIT] Sprawdzanie starych zasobów IPC...\n");

    int old_msg_id = msgget(MSG_KEY_ID, 0600);
    if (old_msg_id != -1) {
        msgctl(old_msg_id, IPC_RMID, NULL);
        printf("[MAIN-INIT] Wykryto i usunięto starą kolejkę komunikatów.\n");
    }

    int old_shm_id = shmget(SHM_KEY_ID, sizeof(struct ParkSharedMemory), 0600);
    if (old_shm_id != -1) {
        shmctl(old_shm_id, IPC_RMID, NULL);
        printf("[MAIN-INIT] Wykryto i usunięto starą pamięć dzieloną.\n");
    }

    int old_sem_id = semget(SEM_KEY_ID, TOTAL_SEMAPHORES, 0600);
    if (old_sem_id == -1) {
        old_sem_id = semget(SEM_KEY_ID, 11, 0600);
    }
    if (old_sem_id != -1) {
        semctl(old_sem_id, 0, IPC_RMID);
        printf("[MAIN-INIT] Wykryto i usunięto stare semafory.\n");
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

    printf("SYMULACJA PARKU NARODOWEGO\n");
    int num_tourists = get_input("Podaj liczbę turystów", 5, 30000);
    int num_guides = get_input("Podaj liczbę przewodników", 2, MAX_GROUPS);

    printf("\n");

    shm_id = shmget(SHM_KEY_ID, sizeof(struct ParkSharedMemory), IPC_CREAT | 0600);
    if (shm_id == -1) {
        perror("[MAIN] Błąd shmget");
        exit(1);
    }
    printf("[MAIN] Pamięć dzielona utworzona (ID: %d, rozmiar: %zu bajtów).\n", shm_id, sizeof(struct ParkSharedMemory));

    struct ParkSharedMemory *park = (struct ParkSharedMemory*)shmat(shm_id, NULL, 0);
    if (park == (void*)-1) {
        perror("[MAIN] Błąd shmat");
        exit(1);
    }

    init_shared_memory(park, num_tourists);

    sem_id = semget(SEM_KEY_ID, TOTAL_SEMAPHORES, IPC_CREAT | 0600);
    if (sem_id == -1) {
        perror("[MAIN] Błąd semget");
        exit(1);
    }
    printf("[MAIN] Zestaw semaforów utworzony (ID: %d, liczba: %d).\n", sem_id, TOTAL_SEMAPHORES);

    init_semaphores(sem_id);

    msg_id = msgget(MSG_KEY_ID, IPC_CREAT | 0600);
    if (msg_id == -1) {
        perror("[MAIN] Błąd msgget");
        exit(1);
    }
    printf("[MAIN] Kolejka komunikatów utworzona (ID: %d).\n", msg_id);

    if (mkfifo(FIFO_PATH, 0660) == -1) {
        perror("[MAIN] Błąd mkfifo");
        exit(1);
    }
    printf("[MAIN] FIFO utworzone (%s).\n", FIFO_PATH);

    dummy_fifo_fd = open(FIFO_PATH, O_RDWR | O_NONBLOCK);
    if (dummy_fifo_fd == -1) {
        perror("[MAIN] Ostrzeżenie: Nie udało się otworzyć dummy FIFO");
    }

    printf("\n[MAIN] Zatrudniam kasjera...\n");

    pid_t kasjer_pid = fork();
    if (kasjer_pid == -1) {
        perror("[MAIN] Błąd fork (kasjer)");
        exit(1);
    }
    if (kasjer_pid == 0) {

        execl("./kasjer", "kasjer", "1", NULL);
        perror("[MAIN] Błąd execl kasjer");
        exit(1);
    }

    printf("[MAIN] Zatrudniam %d przewodników...\n", num_guides);

    for (int i = 1; i <= num_guides; i++) {
        pid_t pid = fork();
        if (pid == -1) {
            perror("[MAIN] Błąd fork (przewodnik)");
            exit(1);
        }
        if (pid == 0) {

            char id_buff[16];
            sprintf(id_buff, "%d", i);
            execl("./przewodnik", "przewodnik", id_buff, NULL);
            perror("[MAIN] Błąd execl przewodnik");
            exit(1);
        }

    }

    printf("\n[MAIN] System gotowy. Naciśnij Ctrl + C aby zakończyć.\n\n");

    printf("[MAIN] Rozpoczynam generowanie %d turystów...\n", num_tourists);

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
                perror("[MAIN] Błąd fork (turysta) - nie można zwolnić procesów");
                fprintf(stderr, "[MAIN] Kontynuuję z %d turystami.\n", created_tourists);
                break;
            }
            perror("[MAIN] Błąd fork (turysta) - błąd krytyczny");
            fprintf(stderr, "[MAIN] Kontynuuję z %d turystami.\n", created_tourists);
            break;
        }

        if (pid == 0) {
            char id_buff[16];
            sprintf(id_buff, "%d", i);
            execl("./turysta", "turysta", id_buff, NULL);
            perror("[MAIN-CHILD] Błąd execl turysta");
            exit(1);
        }

        created_tourists++;

        if (i % 100 == 0) {
            printf("[MAIN] Wygenerowano %d/%d turystów\n", i, num_tourists);
        }
    }

    if (created_tourists < num_tourists) {
        sem_lock(sem_id, SEM_STATS_MUTEX);
        park->total_expected = created_tourists;
        sem_unlock(sem_id, SEM_STATS_MUTEX);
        printf("[MAIN] Zaktualizowano total_expected: %d (planowano %d)\n", created_tourists, num_tourists);
    }

    printf("\n[MAIN] Wygenerowano %d turystów. Czekam na zakończenie zwiedzania...\n", created_tourists);
    int remaining = created_tourists - finished_tourists;
    for (int i = 0; i < remaining; i++) {
        wait(NULL);
    }

    printf("\n[MAIN] Wszyscy turyści zakończyli procesy. Wysyłam sygnał do kasjera...\n");

    kill(kasjer_pid, SIGTERM);

    printf("[MAIN] Czekam na zakończenie kasjera (przetwarzanie ostatnich wiadomości)...\n");
    waitpid(kasjer_pid, NULL, 0);

    printf("[MAIN] Kasjer zakończył pracę. Generuję statystyki...\n");

    printf("\n============== STATYSTYKI PARKU ==============\n");
    printf("Liczba przewodników:     %d\n", num_guides);
    printf("Wygenerowani turyści:    %d\n", num_tourists);
    printf("Weszło do parku:         %d\n", park->total_entered);
    printf("Wyszło z parku:          %d\n", park->total_exited);
    printf("Różnica (w parku):       %d\n", park->total_entered - park->total_exited);
    printf("----------------------------------------------\n");

    if (park->total_entered == num_tourists && park->total_exited == num_tourists) {
        printf("Status: Sukces - wszyscy przeszli przez park!\n");
    } else if (park->total_entered == num_tourists) {
        printf("Status: Wszyscy weszli, ale nie wszyscy wyszli\n");
    } else {
        printf("Status: Błąd - nie wszyscy weszli do parku\n");
    }
    printf("==============================================\n\n");

    shmdt(park);
    return 0;
}