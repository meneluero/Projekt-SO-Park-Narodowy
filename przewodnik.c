#include "common.h"
#include <string.h>

int main(int argc, char* argv[]) {
    // walidacja
    if (argc < 2) {
        printf("[PRZEWODNIK] Błąd: Brak ID!\n");
        exit(1);
    }
    
    int id = atoi(argv[1]);
    
    // podlaczenie do pamieci dzielonej
    int shm_id = shmget(SHM_KEY_ID, sizeof(struct ParkSharedMemory), 0666);
    if (shm_id == -1) {
        perror("[PRZEWODNIK] Błąd shmget");
        exit(1);
    }
    
    struct ParkSharedMemory *park = (struct ParkSharedMemory*)shmat(shm_id, NULL, 0);
    if (park == (void*)-1) {
        perror("[PRZEWODNIK] Błąd shmat");
        exit(1);
    }
    
    // pobranie id semaforow
    int sem_id = semget(SEM_KEY_ID, 5, 0666);
    if (sem_id == -1) {
        perror("[PRZEWODNIK] Błąd semget");
        exit(1);
    }
    
    // pobranie id kolejki komunikatow (do wysylania raportow)
    int msg_id = msgget(MSG_KEY_ID, 0666);
    if (msg_id == -1) {
        perror("[PRZEWODNIK] Błąd msgget");
        exit(1);
    }
    
    // inicjalizacja generatora losowego
    srand(time(NULL) + id);
    
    // petla zycia przewodnika
    printf("[PRZEWODNIK %d] Melduję się w pracy! Czekam na grupy...\n", id);
    
    // przewodnik w przeciwienstwie do turysty jest w petli nieskonczonej
    // (obsluguje wiele wycieczek)
    while (1) {
        // czekanie na grupe (semafor 1)
        sem_lock(sem_id, 1);
        
        // przejecie grupy
        printf("[PRZEWODNIK %d] Mam grupę %d osób! Zabieram was na wycieczkę!\n", 
               id, M_GROUP_SIZE);
        
        // odblokowanie turystow - wisza na semaforze nr 2
        // musimy ich uwolnic M razy bo tylu jest w grupie
        for (int k = 0; k < M_GROUP_SIZE; k++) {
            sem_unlock(sem_id, 2);
        }
        
        // male opoznienie zeby turyści sie "zgromadzili"
        usleep(200000); // 200ms
        
        // losowanie trasy (1 lub 2)
        int route = (rand() % 2) + 1;
        
        printf("[PRZEWODNIK %d] Wybieram trasę %d\n", id, route);
        
        if (route == 1) {
            printf("[PRZEWODNIK %d] Trasa: K → A → B → C → K\n", id);
            // do zrobienia: implementacja trasy 1
            // cross_bridge(id);
            // visit_tower(id);
            // take_ferry(id);
        } else {
            printf("[PRZEWODNIK %d] Trasa: K → C → B → A → K\n", id);
            // do zrobienia: implementacja trasy 2
            // take_ferry(id);
            // visit_tower(id);
            // cross_bridge(id);
        }
        
        // symulacja wycieczki
        int tour_time = 3 + (rand() % 4); // 3-6 sekund
        printf("[PRZEWODNIK %d] Oprowadzam wycieczkę (czas: %ds)...\n", id, tour_time);
        sleep(tour_time);

        // zwalniamy turystów
        printf("[PRZEWODNIK %d] Koniec czasu! Zwalniam grupę do domu.\n", id);
        
        // budzimy kazdego turyste z grupy osobno
        for (int k = 0; k < M_GROUP_SIZE; k++) {
            sem_unlock(sem_id, SEM_KONIEC_WYCIECZKI); // semafor nr 3
        }
        
        // koniec wycieczki
        printf("[PRZEWODNIK %d] Koniec wycieczki. Grupa wraca do kasy.\n", id);
        
        // przygotowanie raportu dla kasjera
        struct msg_buffer report_msg;
        report_msg.msg_type = MSG_TYPE_REPORT;
        report_msg.tourist_id = id; // id przewodnika
        report_msg.age = 0;
        report_msg.is_vip = 0;
        
        char report_info[256];
        sprintf(report_info, "Przewodnik %d zakończył wycieczkę (trasa %d, %d osób)", 
                id, route, M_GROUP_SIZE);
        strcpy(report_msg.info, report_info);
        
        // wyslanie raportu do kasjera
        if (msgsnd(msg_id, &report_msg, sizeof(report_msg) - sizeof(long), 0) == -1) {
            perror("[PRZEWODNIK] Błąd msgsnd (raport)");
        }
        
        printf("[PRZEWODNIK %d] Wracam do bazy. Czekam na kolejną grupę...\n\n", id);
        
        // male opoznienie przed przyjeciem kolejnej grupy
        sleep(1);
    }
    
    // sprzatanie (nigdy nie powinno sie wykonac)
    shmdt(park);
    
    return 0;
}