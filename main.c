#include "common.h"
#include <signal.h>
#include <string.h>

// zmienne globalne
int shm_id = -1;
int sem_id = -1;
int msg_id = -1;
int cleanup_done = 0; // flaga aby cleanup wykonal sie tylko raz

void cleanup() {
    if (cleanup_done) return; // juz bylo sprzatanie
    cleanup_done = 1;
    
    printf("\n[MAIN] Sprzątanie zasobów systemowych...\n");
    
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

int main() {
    // rejestracja funkcji sprzatajacej przy normalnym wyjsciu
    atexit(cleanup);
    
    // rejestracja obslugi ctrl+c
    signal(SIGINT, handle_sigint);
    
    printf("[MAIN] Uruchamianie symulacji Parku Narodowego...\n");
    
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
    
    // -----------------------------------------------------------
    // tworzenie semaforow
    // -----------------------------------------------------------
    // zestaw 5 semaforow:
    // 0: kasa (pojemnosc parku N)
    // 1: przewodnik (czeka na grupe)
    // 2: zbiorka (turyści czekaja na przewodnika)
    // 3: most (do uzupelnienia)
    // 4: pomocniczy (do uzupelnienia)
    sem_id = semget(SEM_KEY_ID, 5, IPC_CREAT | 0666);
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
    semctl(sem_id, 4, SETVAL, arg);
    
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
    printf("[MAIN] Zatrudniam %d przewodników...\n", P_guides);
    
    for (int i = 1; i <= P_guides; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            // proces dziecko -> zamienia sie w przewodnika
            char id_buff[10];
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
    printf("[MAIN] Uruchamiam 10 turystów...\n");
    
    for (int i = 1; i <= 10; i++) {
        pid_t pid = fork(); // rozwidlenie procesu
        
        if (pid == 0) {
            // jestesmy w procesie dziecku
            
            char id_buff[10];
            sprintf(id_buff, "%d", i); // zamiana liczby i na napis
            
            // execl podmienia kod procesu na plik "turysta"
            execl("./turysta", "turysta", id_buff, NULL);
            
            // jesli execl zawiodl
            perror("[MAIN-CHILD] Błąd execl turysta");
            exit(1);
        }
        
        // male opoznienie zeby komunikaty byly bardziej czytelne
        usleep(500000); // 500ms
    }
    
    // -----------------------------------------------------------
    // petla glowna - program po prostu "zyje"
    // -----------------------------------------------------------
    while (1) {
        sleep(1); // czekamy
    }
    
    // odlaczenie pamieci (nigdy nie powinno sie wykonac w petli nieskonczonej)
    shmdt(park);
    
    return 0;
}