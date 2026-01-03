#include "common.h"
#include <string.h>

int main(int argc, char* argv[]) {
    // walidacja argumentow
    if (argc < 2) {
        printf("[KASJER] Błąd: Brak ID kasjera!\n");
        exit(1);
    }
    
    int id = atoi(argv[1]);
    
    // podlaczenie do pamieci dzielonej
    int shm_id = shmget(SHM_KEY_ID, sizeof(struct ParkSharedMemory), 0666);
    if (shm_id == -1) {
        perror("[KASJER] Błąd shmget");
        exit(1);
    }
    
    struct ParkSharedMemory *park = (struct ParkSharedMemory*)shmat(shm_id, NULL, 0);
    if (park == (void*)-1) {
        perror("[KASJER] Błąd shmat");
        exit(1);
    }
    
    // pobranie id semaforow
    int sem_id = semget(SEM_KEY_ID, 5, 0666);
    if (sem_id == -1) {
        perror("[KASJER] Błąd semget");
        exit(1);
    }
    
    // tworzenie kolejki komunikatow
    int msg_id = msgget(MSG_KEY_ID, IPC_CREAT | 0666);
    if (msg_id == -1) {
        perror("[KASJER] Błąd msgget");
        exit(1);
    }
    
    // otwarcie pliku logów do zapisu
    FILE *log_file = fopen("park_log.txt", "a");
    if (log_file == NULL) {
        perror("[KASJER] Błąd otwierania pliku logów");
        exit(1);
    }
    
    printf("[KASJER %d] Otwieram kasę! Czekam na turystów...\n", id);
    fprintf(log_file, "[KASJER %d] Rozpoczęcie pracy\n", id);
    fflush(log_file);
    
    // petla zycia kasjera - obsluguje komunikaty w nieskonczonosc
    while(1) {
        struct msg_buffer message;
        
        // odbieranie komunikatow z kolejki (msgrcv blokuje az przyjdzie wiadomosc)
        // typ 0 = odbierz pierwszy dostepny komunikat dowolnego typu
        if (msgrcv(msg_id, &message, sizeof(message) - sizeof(long), 0, 0) == -1) {
            perror("[KASJER] Błąd msgrcv");
            continue; // kontynuuj mimo bledu
        }
        
        // obsluga roznych typow komunikatow
        if (message.msg_type == MSG_TYPE_ENTRY) {
            // turysta chce wejsc do parku
            
            // informacja o wejsciu
            if (message.is_vip) {
                printf("[KASJER %d] VIP Turysta %d (wiek: %d) - wejście bezpłatne (legitymacja PTTK)\n", 
                       id, message.tourist_id, message.age);
                fprintf(log_file, "[KASJER %d] VIP Turysta %d (wiek: %d) - wejście bezpłatne\n",
                        id, message.tourist_id, message.age);
            } else if (message.age < 7) {
                printf("[KASJER %d] Turysta %d (wiek: %d) - wejście bezpłatne (dziecko <7 lat)\n",
                       id, message.tourist_id, message.age);
                fprintf(log_file, "[KASJER %d] Turysta %d (wiek: %d) - dziecko, bilet bezpłatny\n",
                        id, message.tourist_id, message.age);
            } else {
                printf("[KASJER %d] Turysta %d (wiek: %d) - bilet opłacony\n",
                       id, message.tourist_id, message.age);
                fprintf(log_file, "[KASJER %d] Turysta %d (wiek: %d) - bilet normalny\n",
                        id, message.tourist_id, message.age);
            }
            
            // rejestracja wejscia w statystykach
            park->total_entered++;
            
            fflush(log_file);
            
        } else if (message.msg_type == MSG_TYPE_EXIT) {

            // turysta wychodzi z parku (info od przewodnika)
            printf("[KASJER %d] Turysta %d - wyjście z parku\n", 
                   id, message.tourist_id);
            fprintf(log_file, "[KASJER %d] Turysta %d - wyjście (czas w parku: %s)\n",
                    id, message.tourist_id, message.info);
            
            // rejestracja wyjscia
            park->total_exited++;
            
            fflush(log_file);
            
        } else if (message.msg_type == MSG_TYPE_REPORT) {

            // raport od przewodnika o zakonczeniu wycieczki
            printf("[KASJER %d] Raport od przewodnika: %s\n", id, message.info);
            fprintf(log_file, "[KASJER %d] Raport: %s\n", id, message.info);
            
            fflush(log_file);
            
        } else {
            // nieznany typ wiadomosci
            printf("[KASJER %d] Otrzymano nieznany typ komunikatu: %ld\n", id, message.msg_type);
        }
        
        // male opoznienie zeby nie zabierac 100% cpu
        usleep(10000); // 10ms
    }
    
    // sprzatanie (nigdy nie powinno sie wykonac, ale dla formalnosci)
    fclose(log_file);
    shmdt(park);
    
    return 0;
}