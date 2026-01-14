#include "common.h"
#include <string.h>
#include <signal.h>

volatile sig_atomic_t emergency_flag = 0;

// handler sygnalu ewakuacji
void tower_evacuation_handler(int sig) {
    char *msg = "\n[TURYSTA] Ewakuacja! Zbiegam z wieży! (SIGUSR1)\n";
    write(STDOUT_FILENO, msg, strlen(msg));
    // sygnal przerwie sleep() wiec turystna "wybiegnie" naturalnie
}

void emergency_exit_handler(int sig) {
    emergency_flag = 1; // ustawiamy flage
    char *msg = "\n[TURYSTA] Alarm! Natychmiastowy powrót! (SIGUSR2)\n";
    write(STDOUT_FILENO, msg, strlen(msg));
}

int main(int argc, char* argv[]) {
    // rejestracja handlera
    struct sigaction sa;
    sa.sa_handler = tower_evacuation_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0; // nie uzywamy SA_RESTART zeby sleep() zostal przerwany
    sigaction(SIGUSR1, &sa, NULL);

    struct sigaction sa_exit;
    sa_exit.sa_handler = emergency_exit_handler;
    sigemptyset(&sa_exit.sa_mask);
    sa_exit.sa_flags = 0;
    sigaction(SIGUSR2, &sa_exit, NULL);

    // argv[1] to bedzie numer naszego turysty przekazanego przez maina
    if (argc < 2) {
        printf("[TURYSTA] Błąd: Brak ID turysty! Uruchamiaj przez main.\n");
        exit(1);
    }
    
    int id = atoi(argv[1]); // zamieniamy tekst na liczbe
    
    // losowanie atrybutow turysty
    srand(time(NULL) + id); // seed losowosci unikalny dla kazdego turysty
    
    int age = (rand() % 68) + 3;        // wiek 3-70 lat
    int is_vip = (rand() % 100) < 10;   // 10% szans na VIP
    
    // przylaczenie pamieci dzielonej
    // turysta nie tworzy (IPC_CREAT), on tylko pobiera (shmget) istniejaca
    int shm_id = shmget(SHM_KEY_ID, sizeof(struct ParkSharedMemory), 0666);
    int sem_id = semget(SEM_KEY_ID, 9, 0666);
    int msg_id = msgget(MSG_KEY_ID, 0666);
    
    if (shm_id == -1 || sem_id == -1 || msg_id == -1) {
        perror("[TURYSTA] Nie mogę znaleźć zasobów");
        exit(1);
    }
    
    // rzutowanie wskaznika
    struct ParkSharedMemory *park = (struct ParkSharedMemory*)shmat(shm_id, NULL, 0);
    if (park == (void*)-1) {
        perror("[TURYSTA] Błąd shmat");
        exit(1);
    }
    
    // wyswietlenie info o turyscie
    if (is_vip) {
        printf("[TURYSTA %d] Jestem VIPem (wiek: %d). Mam legitymację PTTK!\n", id, age);
    } else if (age < 7) {
        printf("[TURYSTA %d] Jestem dzieckiem (wiek: %d). Wchodzę za darmo!\n", id, age);
    } else {
        printf("[TURYSTA %d] Przychodzę do parku (wiek: %d).\n", id, age);
    }
    
    // logika wejscia do parku
    printf("[TURYSTA %d] Jestem przed kasą. Czekam na bilet...\n", id);
    
    // przygotowanie komunikatu o wejsciu dla kasjera
    struct msg_buffer entry_msg;
    entry_msg.msg_type = MSG_TYPE_ENTRY;
    entry_msg.tourist_id = id;
    entry_msg.age = age;
    entry_msg.is_vip = is_vip;
    strcpy(entry_msg.info, "wejście do parku");
    
    // wyslanie komunikatu do kasjera przez kolejke
    if (msgsnd(msg_id, &entry_msg, sizeof(entry_msg) - sizeof(long), 0) == -1) {
        perror("[TURYSTA] Błąd msgsnd (wejście)");
        exit(1);
    }
    
    // czekanie na wejscie - bramka pojemnosci parku
    // jesli park jest pelny, proces tu zasnie i poczeka
    sem_lock(sem_id, 0);
    
    // sekcja krytyczna - jestesmy w parku!
    park->people_in_park++;
    
    if (is_vip) {
        park->vip_in_park++;
    }
    
    printf("[TURYSTA %d] Wszedłem do parku! Idę do punktu zbiórki.\n", id);
    
    // -----------------------------------------------------------
    // logika zbiorki w grupy
    // -----------------------------------------------------------
    
    // vip moze zwiedzac samodzielnie lub dolaczyc do grupy priorytetowo
    // na razie upraszczamy - vip tez w grupach (do zrobienia: solo zwiedzanie VIP)
    
    // zgloszenie sie do grupy
    // blokujemy dostep do listy obecnosci
    sem_lock(sem_id, SEM_QUEUE_MUTEX);

    // sekcja krytyczna - tylko jeden turysta tu przebywa na raz
    park->group_ages[park->people_in_queue] = age;
    park->group_pids[park->people_in_queue] = getpid(); // zapisujemy pid
    park->people_in_queue++;

    int current_count = park->people_in_queue; // zapamietujemy lokalnie

    // jesli to byl ostatni turysta resetujemy licznik dla nastepnej grupy
    if (current_count == M_GROUP_SIZE) {
        park->people_in_queue = 0;
    }

    // odblokowanie dostepu
    sem_unlock(sem_id, SEM_QUEUE_MUTEX);

    printf("[TURYSTA %d] Czekam na przewodnika. (Grupa: %d/%d)\n", id, current_count, M_GROUP_SIZE);
    
    // sprawdzenie czy jestesmy ostatni w grupie (komplet)
    if (current_count == M_GROUP_SIZE) {
        printf("[TURYSTA %d] Komplet! Budzę przewodnika!\n", id);
        sem_unlock(sem_id, 1); // budzimy przewodnika
    }
    
    // czekamy na znak od przewodnika (semafor 2)
    sem_lock(sem_id, 2);
    
    // koniec czekania - wycieczka!
    printf("[TURYSTA %d] Zwiedzam z przewodnikiem!\n", id);
    
    // petla zwiedzania z okresowym sprawdzaniem flagi awaryjnej
    while(!emergency_flag) {
        // sprawdzamy co 100ms czy przewodnik nie wyslal alarmu
        usleep(100000);
        
        // sprawdzamy czy wycieczka skonczyla sie normalnie
        struct sembuf check;
        check.sem_num = SEM_KONIEC_WYCIECZKI;
        check.sem_op = -1;
        check.sem_flg = IPC_NOWAIT; // nie blokuj
        
        if (semop(sem_id, &check, 1) == 0) {
            printf("[TURYSTA %d] Wycieczka zakończona normalnie.\n", id);
            break;
        }
        // wycieczka trwa dalej - czekamy
    }

    // czy byla ewakuacja
    if (emergency_flag) {
        printf("[TURYSTA %d] Ewakuacja! Pomijam resztę wycieczki!\n", id);
    }
    
    // -----------------------------------------------------------
    // logika wyjscia z parku
    // -----------------------------------------------------------
    
    printf("[TURYSTA %d] Koniec zwiedzania. Wychodzę z parku.\n", id);
    
    // przygotowanie komunikatu o wyjsciu dla kasjera
    struct msg_buffer exit_msg;
    exit_msg.msg_type = MSG_TYPE_EXIT;
    exit_msg.tourist_id = id;
    exit_msg.age = age;
    exit_msg.is_vip = is_vip;
    
    char timestamp[20];
    get_timestamp(timestamp, sizeof(timestamp));
    strcpy(exit_msg.info, timestamp);
    
    // wyslanie komunikatu o wyjsciu
    if (msgsnd(msg_id, &exit_msg, sizeof(exit_msg) - sizeof(long), 0) == -1) {
        perror("[TURYSTA] Błąd msgsnd (wyjście)");
    }
    
    // aktualizacja statystyk
    park->people_in_park--;
    
    if (is_vip) {
        park->vip_in_park--;
    }
    
    // zwalniamy miejsce dla kogos w kolejce
    sem_unlock(sem_id, 0);
    
    printf("[TURYSTA %d] Do widzenia!\n", id);
    
    // odlaczenie pamieci dzielonej
    shmdt(park);
    
    return 0;
}