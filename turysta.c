#include "common.h"

int main(int argc, char* argv[]) {
    //argv[1] to bedzie numer naszego turysty przekazanego przez maina
    if (argc < 2) {
        printf("[TURYSTA] Błąd: Brak ID turysty! Uruchamiaj przez main.\n");
        exit(1);
    }

    int id = atoi(argv[1]); //zamieniamy tekst na liczbe

    //przylaczenie pamieci dzielonej
    //turysta nie tworzy (IPC_CREAT), on tylko pobiera (shmget) istniejaca
    int shm_id = shmget(SHM_KEY_ID, sizeof(struct ParkSharedMemory), 0666);
    int sem_id = semget(SEM_KEY_ID, 5, 0666);

    if (shm_id == -1 || sem_id == -1){
        perror("[TURYSTA] Nie mogę znaleźć zasobów.");
        exit(1);
    }

    //rzutowanie wskaznika
    struct ParkSharedMemory *park = (struct ParkSharedMemory*)shmat(shm_id, NULL, 0);

    // logika wejscia
    printf("[TURYSTA %d] Jestem przed kasą. Czekam na bilet...\n", id);

    // to jest bramka jesli park jest pelny, proces tu zasnie i poczeka
    sem_lock(sem_id, 0);

    // sekcja krytyczna, jestesmy w parku
    park->people_in_park++; 
    printf("[TURYSTA %d] Wszedłem! (Liczba osób: %d/%d)\n", id, park->people_in_park, N_PARK_CAPACITY);

    //symulujemy ze turysta cos robi
    sleep(2);

    // logika wyjscia
    printf("[TURYSTA %d] Koniec zwiedzania. Wychodzę.\n", id);
    park->people_in_park--;

    // zwalniamy miejsce dla kogos w kolejce
    sem_unlock(sem_id, 0);
    
    //odlaczenie
    shmdt(park);

    return 0;
}