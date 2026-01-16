#include "common.h"
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

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
    int sem_id = semget(SEM_KEY_ID, 10, 0666);
    if (sem_id == -1) {
        perror("[KASJER] Błąd semget");
        exit(1);
    }
    
    // tworzenie kolejki komunikatow
    int msg_id = msgget(MSG_KEY_ID, 0666);
    if (msg_id == -1) {
        perror("[KASJER] Błąd msgget");
        exit(1);
    }
    
    // otwarcie pliku logów do zapisu
    int log_fd = open("park_log.txt", O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (log_fd == -1) {
        perror("[KASJER] Błąd open");
        exit(1);
    }
    
    printf("[KASJER %d] Otwieram kasę! Czekam na turystów...\n", id);
    char start_msg[256];
    int len = snprintf(start_msg, sizeof(start_msg), "[KASJER %d] Rozpoczęcie pracy\n", id);
    if (write(log_fd, start_msg, len) == -1) {
        perror("[KASJER] Błąd write");
    }
    
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
            char log_msg[256];
            int log_len;

            if (message.is_vip) {
                log_len = snprintf(log_msg, sizeof(log_msg), "[KASJER %d] VIP Turysta %d (wiek: %d) - wejście bezpłatne\n", id, message.tourist_id, message.age);
            } else if (message.age < 7) {
                log_len = snprintf(log_msg, sizeof(log_msg), "[KASJER %d] Turysta %d (wiek: %d) - dziecko, bilet bezpłatny\n", id, message.tourist_id, message.age);
            } else {
                log_len = snprintf(log_msg, sizeof(log_msg), "[KASJER %d] Turysta %d (wiek: %d) - bilet normalny\n", id, message.tourist_id, message.age);
            }
            if (write(log_fd, log_msg, log_len) == -1) {
                perror("[KASJER] Błąd write");
            }
            
            // rejestracja wejscia w statystykach
            park->total_entered++;
            
            
        } else if (message.msg_type == MSG_TYPE_EXIT) {

            // turysta wychodzi z parku (info od przewodnika)
            printf("[KASJER %d] Turysta %d - wyjście z parku\n", 
                   id, message.tourist_id);
            char exit_msg[256];
            int exit_len = snprintf(exit_msg, sizeof(exit_msg), "[KASJER %d] Turysta %d - wyjście (czas w parku: %s)\n", id, message.tourist_id, message.info);
            if (write(log_fd, exit_msg, exit_len) == -1) {
                perror("[KASJER] Błąd write");
            }
            
            // rejestracja wyjscia
            park->total_exited++;
            
            
        } else if (message.msg_type == MSG_TYPE_REPORT) {

            // raport od przewodnika o zakonczeniu wycieczki
            printf("[KASJER %d] Raport od przewodnika: %s\n", id, message.info);
            char report_msg[256];
            int report_len = snprintf(report_msg, sizeof(report_msg), "[KASJER %d] Raport: %s\n", id, message.info);
            if (write(log_fd, report_msg, report_len) == -1) {
                perror("[KASJER] Błąd write");
            }
            
        } else {
            // nieznany typ wiadomosci
            printf("[KASJER %d] Otrzymano nieznany typ komunikatu: %ld\n", id, message.msg_type);
        }
    }
    
    // sprzatanie (nigdy nie powinno sie wykonac, ale dla formalnosci)
    close(log_fd);
    shmdt(park);
    
    return 0;
}