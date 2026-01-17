#include "common.h"
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/wait.h>

int log_fd = -1;

void write_log(char *buffer) {
    if (log_fd != -1) {
        if (write(log_fd, buffer, strlen(buffer)) == -1) {
            perror("[KASJER] Błąd write log");
        }
    }
}

int main(int argc, char* argv[]) {
    // walidacja argumentow
    if (argc < 2) {
        printf("[KASJER] Błąd: Brak ID kasjera!\n");
        exit(1);
    }
    
    int id = atoi(argv[1]);
    
    // podlaczenie do pamieci dzielonej
    int shm_id = shmget(SHM_KEY_ID, sizeof(struct ParkSharedMemory), 0600);
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
    int sem_id = semget(SEM_KEY_ID, 11, 0600);
    if (sem_id == -1) {
        perror("[KASJER] Błąd semget");
        exit(1);
    }
    
    // tworzenie kolejki komunikatow
    int msg_id = msgget(MSG_KEY_ID, 0600);
    if (msg_id == -1) {
        perror("[KASJER] Błąd msgget");
        exit(1);
    }
    
    // otwarcie pliku logow do zapisu
    log_fd = open("park_log.txt", O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (log_fd == -1) {
        perror("[KASJER] Błąd open");
        exit(1);
    }
    
    printf("[KASJER %d] Otwieram kasę!\n", id);
    char start_msg[256];
    snprintf(start_msg, sizeof(start_msg), "[KASJER %d] Rozpoczęcie pracy\n", id);
    write_log(start_msg);
    
    // petla zycia kasjera - obsluguje komunikaty w nieskonczonosc
    pid_t pid = fork();

    if (pid == -1) {
        perror("[KASJER] Błąd fork");
        exit(1);
    }

    if (pid == 0) {
        // obsluga fifo 
        int fifo_fd = open(FIFO_PATH, O_RDONLY);
        if (fifo_fd == -1) {
            perror("[KASJER-FIFO] Błąd open FIFO");
            exit(1);
        }
        
        char fifo_buffer[512];
        ssize_t bytes;

        while ((bytes = read(fifo_fd, fifo_buffer, sizeof(fifo_buffer) - 1)) > 0) {
            fifo_buffer[bytes] = '\0';
            
            char *line = strtok(fifo_buffer, "\n");
            while (line != NULL) {
                printf("[KASJER %d] Raport z FIFO: %s\n", id, line);
                
                char log_fifo[256];
                snprintf(log_fifo, sizeof(log_fifo), "[KASJER %d] Raport FIFO: %s\n", id, line);
                write_log(log_fifo);
                
                line = strtok(NULL, "\n");
            }
        }
        
        close(fifo_fd);
        exit(0);

    } else {
        // obsluga kolejki
        struct msg_buffer message;
        
        while(1) {
            if (msgrcv(msg_id, &message, sizeof(message) - sizeof(long), 0, 0) == -1) {
                if (errno == EINTR) continue;
                perror("[KASJER] Błąd msgrcv");
                break;
            }

            char log_msg[512];

            if (message.msg_type == MSG_TYPE_ENTRY) {
                if (message.is_vip) {
                    snprintf(log_msg, sizeof(log_msg), "[KASJER %d] VIP Turysta %d (wiek: %d) - wejście bezpłatne\n", id, message.tourist_id, message.age);
                } else if (message.age < 7) {
                    snprintf(log_msg, sizeof(log_msg), "[KASJER %d] Turysta %d (wiek: %d) - dziecko, bilet bezpłatny\n", id, message.tourist_id, message.age);
                } else {
                    snprintf(log_msg, sizeof(log_msg), "[KASJER %d] Turysta %d (wiek: %d) - bilet normalny\n", id, message.tourist_id, message.age);
                }
                write_log(log_msg);

                sem_lock(sem_id, SEM_STATS_MUTEX);
                park->total_entered++;
                sem_unlock(sem_id, SEM_STATS_MUTEX);

            } else if (message.msg_type == MSG_TYPE_EXIT) {
                printf("[KASJER %d] Turysta %d - wyjście z parku\n", id, message.tourist_id);
                snprintf(log_msg, sizeof(log_msg), "[KASJER %d] Turysta %d - wyjście (czas w parku: %s)\n", id, message.tourist_id, message.info);
                write_log(log_msg);

                sem_lock(sem_id, SEM_STATS_MUTEX);
                park->total_exited++;
                sem_unlock(sem_id, SEM_STATS_MUTEX);
            }
        }
        
        wait(NULL);
    }

    // sprzatanie - nigdy nie powinno sie wykonac
    close(log_fd);
    shmdt(park);

    return 0;
}