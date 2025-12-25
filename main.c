#include "common.h"
#include <signal.h>

//zmienne globalne
int shm_id = -1;
int sem_id = -1;
int msg_id = -1;

void cleanup() {
    printf("\n[MAIN] Sprzątanie zasobów systemowych... \n");

    if(shm_id != -1) {
        if (shmctl(shm_id, IPC_RMID, NULL) == -1){
            perror("[MAIN] Błąd usuwania pamięci dzielonej");
        } else {
            printf("[MAIN] Pamięć dzielona usunięta.\n");
        }
    }

    if(sem_id != -1) {
        if (shmctl(sem_id, 0, IPC_RMID) == -1){
            perror("[MAIN] Błąd usuwania semaforów");
        } else {
            printf("[MAIN] Semafory usunięte.\n");
        }
    }
    //do dokonczenia
}

void handle_sigint(int sig) {
    printf("\n[MAIN] Otrzymano sygnał %d (Ctrl + C). Kończę program.\n", sig);
    exit(0); // exit() automatycznie wywola atexit(cleanup) jesli tak ustawimy, ale tutaj zrobimy to jawnie w mainie albo przez wywołanie funkcji
}

int main() {
    // rejestracja funkcji sprzątającej przy normalnym wyjściu
    atexit(cleanup);
    // rejestracja obsługi ctrl + c
    signal(SIGINT, handle_sigint);

    printf("[MAIN] Uruchamianie symulacji Parku Narodowego...\n");

    // tworzymy pamiec dzielona
    // shmget tworzy segment pamięci o rozmiarze naszej struktury
    shm_id = shmget(SHM_KEY_ID, sizeof(struct ParkSharedMemory), IPC_CREAT | 0666);
    if (shm_id == -1) {
        perror("[MAIN] Blad shmget");
        exit(1);
    }
    printf("[MAIN] Pamiec dzielona utworzona (ID: %d).\n", shm_id);

    // przylaczenie pamieci do procesu
    struct ParkSharedMemory *park = (struct ParkSharedMemory*)shmat(shm_id, NULL, 0);
    if (park == (void*)-1) {
        perror("[MAIN] Blad shmat");
        exit(1);
    }
    
    // inicjalizacja poczatkowa wartosci w pamieci
    park->people_in_park = 0;
    park->bridge_current_count = 0;
    // ... reszta wyzeruje sie sama, ale warto byc jawnym w przyszlosci

    // tworenie semaforow
    // na razie tworzymy zestaw 5 semaforów na próbę
    // (0: kasa, 1: most, 2: wieża, 3: prom, 4: pomocniczy)
    sem_id = semget(SEM_KEY_ID, 5, IPC_CREAT | 0666);
    if (sem_id == -1) {
        perror("[MAIN] Błąd semget");
        exit(1);
    }
    printf("[MAIN] Zestaw semaforów utworzony (ID: %d).\n", sem_id);

    // petla glowna
    printf("[MAIN] System gotowy. Naciśnij Ctrl + C aby zakończyć.\n");
    
    while(1) {
        // tutaj w przyszłości będzie sprawdzanie stanu symulacji
        // na razie program po prostu "żyje" i trzyma zasoby
        sleep(1); 
    }

    return 0;
}