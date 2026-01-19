#include "common.h"
#include <signal.h>
#include <string.h>
#include <sys/wait.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>

// zmienne globalne

int shm_id = -1;
int sem_id = -1;
int msg_id = -1;
int cleanup_done = 0; // flaga aby cleanup wykonal sie tylko raz
int dummy_fifo_fd = -1;

// funkcja sprzatajaca
void cleanup() {
    if (cleanup_done) return;
    cleanup_done = 1;

    if (dummy_fifo_fd != -1) {
        close(dummy_fifo_fd);
    }

    printf("\n[MAIN] Rozpoczęto sprzątanie zasobów...\n");

    // usuniecie FIFO
    if (unlink(FIFO_PATH) == -1 && errno != ENOENT) {
        perror("[MAIN] Błąd unlink FIFO");
    } else {
        printf("[MAIN] FIFO usunięte.\n");
    }

    // ignorujemy sigterm zeby nie zabic siebie
    struct sigaction sa;
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, NULL);

    // zabicie wszystkich procesow w grupie
    kill(0, SIGTERM);

    // czekanie na zakonczenie dzieci
    while (waitpid(-1, NULL, 0) > 0 || errno == EINTR);
    
    printf("[MAIN] Procesy potomne posprzątane.\n");

    // usuwanie ipc z obsluga bledow
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

// handler ctrl + c
void handle_sigint(int sig) {
    printf("\n[MAIN] Otrzymano sygnał %d (Ctrl + C). Kończę program.\n", sig);
    exit(0); // exit() automatycznie wywola atexit(cleanup)
}

// pobieranie danych od uzytkownika
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
            while (getchar() != '\n');  // czyszczenie bufora wejscia
        }
    }
}

void init_semaphores(int sem_id) {
    union semun arg;
    
    printf("[MAIN] Inicjalizacja %d semaforów...\n", TOTAL_SEMAPHORES);
    
    // semafory podstawowe 

    // pojemnosc parku
    arg.val = N_PARK_CAPACITY;
    if (semctl(sem_id, SEM_PARK_LIMIT, SETVAL, arg) == -1) {
        perror("[MAIN] Błąd semctl SEM_PARK_LIMIT");
        exit(1);
    }
    
    // budzenie przewodnika (0 = spi)
    arg.val = 0;
    if (semctl(sem_id, SEM_PRZEWODNIK, SETVAL, arg) == -1) {
        perror("[MAIN] Błąd semctl SEM_PRZEWODNIK");
        exit(1);
    }
    
    // mutex kolejki oczekujacych
    arg.val = 1;
    if (semctl(sem_id, SEM_QUEUE_MUTEX, SETVAL, arg) == -1) {
        perror("[MAIN] Błąd semctl SEM_QUEUE_MUTEX");
        exit(1);
    }
    
    // mutex statystyk
    arg.val = 1;
    if (semctl(sem_id, SEM_STATS_MUTEX, SETVAL, arg) == -1) {
        perror("[MAIN] Błąd semctl SEM_STATS_MUTEX");
        exit(1);
    }
    
    //semafory atrakcji

    // limit osob na moscie
    arg.val = X1_BRIDGE_CAP;
    if (semctl(sem_id, SEM_MOST_LIMIT, SETVAL, arg) == -1) {
        perror("[MAIN] Błąd semctl SEM_MOST_LIMIT");
        exit(1);
    }
    
    // mutex danych mostu
    arg.val = 1;
    if (semctl(sem_id, SEM_MOST_MUTEX, SETVAL, arg) == -1) {
        perror("[MAIN] Błąd semctl SEM_MOST_MUTEX");
        exit(1);
    }
    
    // limit osob na wiezy
    arg.val = X2_TOWER_CAP;
    if (semctl(sem_id, SEM_WIEZA_LIMIT, SETVAL, arg) == -1) {
        perror("[MAIN] Błąd semctl SEM_WIEZA_LIMIT");
        exit(1);
    }
    
    // mutex danych wiezy
    arg.val = 1;
    if (semctl(sem_id, SEM_WIEZA_MUTEX, SETVAL, arg) == -1) {
        perror("[MAIN] Błąd semctl SEM_WIEZA_MUTEX");
        exit(1);
    }
    
    // limit osob na promie
    arg.val = X3_FERRY_CAP;
    if (semctl(sem_id, SEM_PROM_LIMIT, SETVAL, arg) == -1) {
        perror("[MAIN] Błąd semctl SEM_PROM_LIMIT");
        exit(1);
    }
    
    // mutex danych promu
    arg.val = 1;
    if (semctl(sem_id, SEM_PROM_MUTEX, SETVAL, arg) == -1) {
        perror("[MAIN] Błąd semctl SEM_PROM_MUTEX");
        exit(1);
    }
    
    // sygnal ze prom doplynal
    arg.val = 0;
    if (semctl(sem_id, SEM_PROM_ARRIVED, SETVAL, arg) == -1) {
        perror("[MAIN] Błąd semctl SEM_PROM_ARRIVED");
        exit(1);
    }
    
    // semafory grupowe
    for (int i = 0; i < MAX_GROUPS; i++) {
        // turysci czekaja na sygnal od przewodnika
        arg.val = 0;
        if (semctl(sem_id, SEM_GROUP_START(i), SETVAL, arg) == -1) {
            perror("[MAIN] Błąd semctl SEM_GROUP_START");
            exit(1);
        }
        
        // przewodnik czeka na turystow
        arg.val = 0;
        if (semctl(sem_id, SEM_GROUP_DONE(i), SETVAL, arg) == -1) {
            perror("[MAIN] Błąd semctl SEM_GROUP_DONE");
            exit(1);
        }
    }
    
    // mutex przydzielania slotow grup
    arg.val = 1;
    if (semctl(sem_id, SEM_GROUP_MUTEX, SETVAL, arg) == -1) {
        perror("[MAIN] Błąd semctl SEM_GROUP_MUTEX");
        exit(1);
    }
    
    printf("[MAIN] Semafory zainicjalizowane pomyślnie.\n");
}

// funkcja inicjalizacji pamieci dzielonej
void init_shared_memory(struct ParkSharedMemory *park) {
    printf("[MAIN] Inicjalizacja pamięci dzielonej...\n");
    
    // wyzerowanie calej struktury
    memset(park, 0, sizeof(struct ParkSharedMemory));
    
    // ustawienie wartosci poczatkowych
    park->bridge_direction = DIR_NONE;
    park->ferry_position = 0;
    park->ferry_moving = 0;
    park->ferry_group_id = -1;
    park->next_group_slot = 0;
    
    // inicjalizacja slotow grup jako nieaktywnych
    for (int i = 0; i < MAX_GROUPS; i++) {
        park->groups[i].active = 0;
        park->groups[i].guide_id = -1;
    }
    
    printf("[MAIN] Pamięć dzielona zainicjowana pomyślnie.\n");
}

// czyszczenie starych zasobow ipc
void cleanup_old_ipc() {
    printf("[MAIN-INIT] Sprawdzanie starych zasobów IPC...\n");
    
    // stara kolejka komunikatow
    int old_msg_id = msgget(MSG_KEY_ID, 0600);
    if (old_msg_id != -1) {
        msgctl(old_msg_id, IPC_RMID, NULL);
        printf("[MAIN-INIT] Wykryto i usunięto starą kolejkę komunikatów.\n");
    }

    // stara pamiec dzielona
    int old_shm_id = shmget(SHM_KEY_ID, sizeof(struct ParkSharedMemory), 0600);
    if (old_shm_id != -1) {
        shmctl(old_shm_id, IPC_RMID, NULL);
        printf("[MAIN-INIT] Wykryto i usunięto starą pamięć dzieloną.\n");
    }

    // stare semafory
    int old_sem_id = semget(SEM_KEY_ID, TOTAL_SEMAPHORES, 0600);
    if (old_sem_id == -1) {
        old_sem_id = semget(SEM_KEY_ID, 11, 0600);
    }
    if (old_sem_id != -1) {
        semctl(old_sem_id, 0, IPC_RMID);
        printf("[MAIN-INIT] Wykryto i usunięto stare semafory.\n");
    }
    
    // stare fifo
    unlink(FIFO_PATH);
}

int main() {
    // czyszczenie starych zasobow
    cleanup_old_ipc();

    // rejestracja funkcji sprzatajacej przy normalnym wyjsciu
    atexit(cleanup);
    
    // rejestracja obslugi ctrl + c
    struct sigaction sa_int;
    sa_int.sa_handler = handle_sigint;
    sigemptyset(&sa_int.sa_mask);
    sa_int.sa_flags = 0;
    sigaction(SIGINT, &sa_int, NULL);

    // pobieranie danych od uzytkownika
    printf("SYMULACJA PARKU NARODOWEGO\n");
    int num_tourists = get_input("Podaj liczbę turystów", 5, 100);
    int num_guides = get_input("Podaj liczbę przewodników", 1, MAX_GROUPS);
    
    printf("\n");
    
    // tworzenie pamieci dzielonej
    shm_id = shmget(SHM_KEY_ID, sizeof(struct ParkSharedMemory), IPC_CREAT | 0600);
    if (shm_id == -1) {
        perror("[MAIN] Błąd shmget");
        exit(1);
    }
    printf("[MAIN] Pamięć dzielona utworzona (ID: %d, rozmiar: %zu bajtów).\n", shm_id, sizeof(struct ParkSharedMemory));
    
    // przylaczenie pamieci do procesu
    struct ParkSharedMemory *park = (struct ParkSharedMemory*)shmat(shm_id, NULL, 0);
    if (park == (void*)-1) {
        perror("[MAIN] Błąd shmat");
        exit(1);
    }
    
    // inicjalizacja pamieci dzielonej
    init_shared_memory(park);
    
    // tworzenie semaforow
    sem_id = semget(SEM_KEY_ID, TOTAL_SEMAPHORES, IPC_CREAT | 0600);
    if (sem_id == -1) {
        perror("[MAIN] Błąd semget");
        exit(1);
    }
    printf("[MAIN] Zestaw semaforów utworzony (ID: %d, liczba: %d).\n", sem_id, TOTAL_SEMAPHORES);
    
    // inicjalizacja semaforow
    init_semaphores(sem_id);
    
    // tworzenie kolejki komunikatow
    msg_id = msgget(MSG_KEY_ID, IPC_CREAT | 0600);
    if (msg_id == -1) {
        perror("[MAIN] Błąd msgget");
        exit(1);
    }
    printf("[MAIN] Kolejka komunikatów utworzona (ID: %d).\n", msg_id);

    // tworzenie fifo dla raportow przewodnikow
    if (mkfifo(FIFO_PATH, 0660) == -1) {
        perror("[MAIN] Błąd mkfifo");
        exit(1);
    }
    printf("[MAIN] FIFO utworzone (%s).\n", FIFO_PATH);
    
    dummy_fifo_fd = open(FIFO_PATH, O_WRONLY | O_NONBLOCK);
    if (dummy_fifo_fd == -1) {
        perror("[MAIN] Ostrzeżenie: Nie udało się otworzyć dummy FIFO");
    }

    // uruchomienie kasjera
    printf("\n[MAIN] Zatrudniam kasjera...\n");
    
    pid_t kasjer_pid = fork();
    if (kasjer_pid == -1) {
        perror("[MAIN] Błąd fork (kasjer)");
        exit(1);
    }
    if (kasjer_pid == 0) {
        // proces dziecko -> zamienia sie w kasjera
        execl("./kasjer", "kasjer", "1", NULL);
        perror("[MAIN] Błąd execl kasjer");
        exit(1);
    }
    
    //usleep(200000); // 200ms przerwy zeby kasjer sie uruchomil
    
    // uruchomienie przewodnikow
    printf("[MAIN] Zatrudniam %d przewodników...\n", num_guides);
    
    for (int i = 1; i <= num_guides; i++) {
        pid_t pid = fork();
        if (pid == -1) {
            perror("[MAIN] Błąd fork (przewodnik)");
            exit(1);
        }
        if (pid == 0) {
            // proces dziecko -> zamienia sie w przewodnika
            char id_buff[16];
            sprintf(id_buff, "%d", i);
            execl("./przewodnik", "przewodnik", id_buff, NULL);
            perror("[MAIN] Błąd execl przewodnik");
            exit(1);
        }
        //usleep(100000);
    }
    
    printf("\n[MAIN] System gotowy. Naciśnij Ctrl + C aby zakończyć.\n\n");
    
    // uruchomienie turystow
    printf("[MAIN] Rozpoczynam generowanie %d turystów...\n", num_tourists);
    
    // inicjalizacja generatora losowego
    srand(time(NULL));
    
    // petla tworzenia turystow z losowym opoznieniem
    for (int i = 1; i <= num_tourists; i++) {
        pid_t pid = fork();
        if (pid == -1) {
            perror("[MAIN] Błąd fork (turysta)");
            exit(1);
        }
        
        if (pid == 0) {
            char id_buff[16];
            sprintf(id_buff, "%d", i);
            execl("./turysta", "turysta", id_buff, NULL);
            perror("[MAIN-CHILD] Błąd execl turysta");
            exit(1);
        }
        
        // losowe opoznienie miedzy turystami
        //usleep(200000 + (rand() % 800000));
        
        // raport co 5 turystow
        if (i % 5 == 0) {
            printf("[MAIN] Wygenerowano %d/%d turystów\n", i, num_tourists);
        }
    }

    printf("\n[MAIN] Wszyscy turyści weszli. Czekam na zakończenie zwiedzania...\n");

    // czekamy az wszyscy turysci zakoncza procesy
    for (int i = 0; i < num_tourists; i++) {
        wait(NULL);
    }
    
    printf("\n[MAIN] Wszyscy turyści opuścili park. Koniec symulacji.\n");
    
    // odlaczenie pamieci dzielonej
    shmdt(park);
    return 0;
}