#include "common.h"
#include <signal.h>
#include <string.h>
#include <sys/wait.h>

// zmienne globalne
int shm_id = -1;
int sem_id = -1;
int msg_id = -1;
int cleanup_done = 0; // flaga aby cleanup wykonal sie tylko raz

void cleanup() {
    if (cleanup_done) return; // juz bylo sprzatanie
    cleanup_done = 1;
    
    printf("\n[MAIN] Sprzątanie zasobów systemowych...\n");

    kill(0, SIGTERM); // zabijamy wszystkie procesy potomne
    
    // usuniecie kolejki komunikatow
    if (msg_id != -1) {
        if (msgctl(msg_id, IPC_RMID, NULL) == -1) {
            perror("[MAIN] Błąd usuwania kolejki komunikatów");
        } else {
            printf("[MAIN] Kolejka komunikatów usunięta.\n");
        }
    }
    
    // usuniecie pamieci dzielonej
    if (shm_id != -1) {
        if (shmctl(shm_id, IPC_RMID, NULL) == -1) {
            perror("[MAIN] Błąd usuwania pamięci dzielonej");
        } else {
            printf("[MAIN] Pamięć dzielona usunięta.\n");
        }
    }
    
    // usuniecie semaforow
    if (sem_id != -1) {
        if (semctl(sem_id, 0, IPC_RMID) == -1) {
            perror("[MAIN] Błąd usuwania semaforów");
        } else {
            printf("[MAIN] Semafory usunięte.\n");
        }
    }
    
    printf("[MAIN] Sprzątanie zakończone.\n");
}

void handle_sigint(int sig) {
    printf("\n[MAIN] Otrzymano sygnał %d (Ctrl + C). Kończę program.\n", sig);
    exit(0); // exit() automatycznie wywoła atexit(cleanup)
}

// funkcja pomocnicza do pobierania liczb od uzytkownika
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
            while (getchar() != '\n'); // czyszczenie bufora wejscia
        }
    }
}

int main() {
    // proba pobrania i usuniecia starej kolejki komunikatow
    int old_msg_id = msgget(MSG_KEY_ID, 0666);
    if (old_msg_id != -1) {
        // jesli istnieje usuwamy ja
        msgctl(old_msg_id, IPC_RMID, NULL);
        printf("[MAIN-INIT] Wykryto i usunięto starą kolejkę komunikatów.\n");
    }

    // proba pobrania i usuniecia starej pamieci dzielonej
    int old_shm_id = shmget(SHM_KEY_ID, sizeof(struct ParkSharedMemory), 0666);
    if (old_shm_id != -1) {
        shmctl(old_shm_id, IPC_RMID, NULL);
        printf("[MAIN-INIT] Wykryto i usunięto starą pamięć dzieloną.\n");
    }

    // proba pobrania i usuniecia starych semaforow
    int old_sem_id = semget(SEM_KEY_ID, 11, 0666); 
    if (old_sem_id != -1) {
        semctl(old_sem_id, 0, IPC_RMID);
        printf("[MAIN-INIT] Wykryto i usunięto stare semafory.\n");
    }

    // rejestracja funkcji sprzatajacej przy normalnym wyjsciu
    atexit(cleanup);
    
    // rejestracja obslugi ctrl+c
    signal(SIGINT, handle_sigint);
    
    int num_tourists = get_input("Podaj liczbę turystów", 5, 100);
    int num_guides = get_input("Podaj liczbę przewodników", 1, 10);
    
    // -----------------------------------------------------------
    // tworzenie pamieci dzielonej
    // -----------------------------------------------------------
    shm_id = shmget(SHM_KEY_ID, sizeof(struct ParkSharedMemory), IPC_CREAT | 0666);
    if (shm_id == -1) {
        perror("[MAIN] Błąd shmget");
        exit(1);
    }
    printf("[MAIN] Pamięć dzielona utworzona (ID: %d).\n", shm_id);
    
    // przylaczenie pamieci do procesu
    struct ParkSharedMemory *park = (struct ParkSharedMemory*)shmat(shm_id, NULL, 0);
    if (park == (void*)-1) {
        perror("[MAIN] Błąd shmat");
        exit(1);
    }
    
    // inicjalizacja poczatkowa wartosci w pamieci dzielonej
    memset(park, 0, sizeof(struct ParkSharedMemory)); // wyzerowanie calej struktury
    park->ferry_position = 0; // prom na brzegu A
    

    // tworzenie semaforow
    sem_id = semget(SEM_KEY_ID, 11, IPC_CREAT | 0666);
    if (sem_id == -1) {
        perror("[MAIN] Błąd semget");
        exit(1);
    }
    printf("[MAIN] Zestaw semaforów utworzony (ID: %d).\n", sem_id);
    
    // ustawienie wartosci poczatkowych semaforow
    union semun arg;
    
    // kasa - wpuszcza N osob
    arg.val = N_PARK_CAPACITY;
    semctl(sem_id, 0, SETVAL, arg);
    
    // przewodnik - czeka na sygnal (0 = spi)
    arg.val = 0;
    semctl(sem_id, 1, SETVAL, arg);
    
    // zbiorka - turysci czekaja na przewodnika (0 = wszyscy spia)
    arg.val = 0;
    semctl(sem_id, 2, SETVAL, arg);
    
    // reszta na razie 0 (do uzupelnienia)
    arg.val = 0;
    semctl(sem_id, 3, SETVAL, arg);

    // mutex dla mostu (musi byc otwarty na wejsciu dlatego 1)
    arg.val = 1;
    semctl(sem_id, SEM_MOST_MUTEX, SETVAL, arg);

    // limit pojemnosci wiezy
    arg.val = X2_TOWER_CAP;
    semctl(sem_id, SEM_WIEZA_LIMIT, SETVAL, arg);

    // mutex dla danych wiezy
    arg.val = 1;
    semctl(sem_id, SEM_WIEZA_MUTEX, SETVAL, arg);

    // limit pojemnosci promu
    arg.val = X3_FERRY_CAP;
    semctl(sem_id, SEM_PROM_LIMIT, SETVAL, arg);

    // mutex dla danych promu
    arg.val = 1;
    semctl(sem_id, SEM_PROM_MUTEX, SETVAL, arg);

    // mutex dla kolejki
    arg.val = 1;
    semctl(sem_id, SEM_QUEUE_MUTEX, SETVAL, arg);

    // mutex dla statystyk
    arg.val = 1;
    semctl(sem_id, SEM_STATS_MUTEX, SETVAL, arg);
    
    // -----------------------------------------------------------
    // tworzenie kolejki komunikatow (IPC - drugi mechanizm!)
    // -----------------------------------------------------------
    msg_id = msgget(MSG_KEY_ID, IPC_CREAT | 0666);
    if (msg_id == -1) {
        perror("[MAIN] Błąd msgget");
        exit(1);
    }
    printf("[MAIN] Kolejka komunikatów utworzona (ID: %d).\n", msg_id);
    
    // -----------------------------------------------------------
    // uruchomienie kasjera
    // -----------------------------------------------------------
    printf("[MAIN] Zatrudniam kasjera...\n");
    
    pid_t kasjer_pid = fork();
    if (kasjer_pid == 0) {
        // proces dziecko -> zamienia sie w kasjera
        execl("./kasjer", "kasjer", "1", NULL);
        perror("[MAIN] Błąd execl kasjer");
        exit(1);
    }
    
    usleep(200000); // 200ms przerwy zeby kasjer sie uruchomil
    
    // -----------------------------------------------------------
    // uruchomienie przewodnikow
    // -----------------------------------------------------------
    printf("[MAIN] Zatrudniam %d przewodników...\n", num_guides);
    
    for (int i = 1; i <= num_guides; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            // proces dziecko -> zamienia sie w przewodnika
            char id_buff[16];
            sprintf(id_buff, "%d", i);
            execl("./przewodnik", "przewodnik", id_buff, NULL);
            perror("[MAIN] Błąd execl przewodnik");
            exit(1);
        }
        usleep(100000); // 100ms
    }
    
    printf("[MAIN] System gotowy. Naciśnij Ctrl + C aby zakończyć.\n");
    
    // -----------------------------------------------------------
    // uruchomienie turystow
    // -----------------------------------------------------------
    printf("[MAIN] Rozpoczynam generowanie %d turystów...\n", num_tourists);
    
    // petla tworzenia turystow z losowym opoznieniem
    for (int i = 1; i <= num_tourists; i++) {
        pid_t pid = fork();
        
        if (pid == 0) {
            char id_buff[16];
            sprintf(id_buff, "%d", i);
            execl("./turysta", "turysta", id_buff, NULL);
            perror("[MAIN-CHILD] Błąd execl turysta");
            exit(1);
        }
        
        // losowe opoznienie
        usleep(200000 + (rand() % 800000));
        
        if (i % 5 == 0) printf("[MAIN] Wygenerowano %d/%d turystów\n", i, num_tourists);
    }

    printf("[MAIN] Wszyscy turyści weszli. Czekam na zakończenie zwiedzania...\n");

    // czekamy az wszyscy turysci zakoncza procesy
    for (int i = 0; i < num_tourists; i++) {
        wait(NULL);
    }
    
    printf("\n[MAIN] Wszyscy turyści opuścili park. Koniec symulacji.\n");
    
    // odlaczenie pamieci (nigdy nie powinno sie wykonac w petli nieskonczonej)
    shmdt(park);
    
    return 0;
}