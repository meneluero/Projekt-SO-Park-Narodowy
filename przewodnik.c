#include "common.h"

int main(int argc, char* argv[]) {
    // walidacja
    if (argc < 2) {
        printf("[PRZEWODNIK] Błąd: Brak ID!\n");
        exit(1);
    }

    int id = atoi(argv[1]);

    // podlaczenie do pamieci
    int shm_id = shmget(SHM_KEY_ID, sizeof(struct ParkSharedMemory), 0666);
    if (shm_id == -1){
        printf("[PRZEWODNIK] Błąd shmget");
        exit(1);
    }
    struct ParkSharedMemory *park = (struct ParkSharedMemory*)shmat(shm_id, NULL, 0);

    // pobranie id semaforow
    int sem_id = semget(SEM_KEY_ID, 5, 0666);
    if (sem_id == -1) {
        printf("[PRZEWODNIK] Błąd semget");
        exit(1);
    }

    // petla zycia przewodnika

    printf("[PRZEWODNIK %d] Melduję się w pracy! Czekam na grupy...\n", id);

    // przewodnik w przeciwienstwie do turysty jest w petli nieskonczonej (obsluga wielu wycieczek)
    while(1) {
        // czekanie na grupe
        sem_lock(sem_id, 1);
        
        // przejecie grupy
        printf("[PRZEWODNIK %d] O! Jest grupa %d osób. Zabieram was!\n", id, M_GROUP_SIZE);

        // odblokowanie turystow, wisza na semaforze nr 2, musimy ich uwolnic, robimy to M razy bo tylu jest w grupie
        for(int k=0; k < M_GROUP_SIZE; k++) {
            sem_unlock(sem_id, 2);
        }

        // wycieczka
        printf("[PRZEWODNIK %d] Oprowadzam wycieczkę...\n", id);
        sleep(3); // symulacja zwiedzania
        printf("[PRZEWODNIK %d] Koniec wycieczki. Wracam do bazy.\n", id);
    }



    shmdt(park);
    return 0;
}