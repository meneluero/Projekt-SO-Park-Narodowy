#include "common.h"
#include <string.h>

int main(int argc, char* argv[]) {
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
    int sem_id = semget(SEM_KEY_ID, 5, 0666);
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
    park->people_in_queue++;
    printf("[TURYSTA %d] Czekam na przewodnika. (Grupa: %d/%d)\n", 
           id, park->people_in_queue, M_GROUP_SIZE);
    
    // sprawdzenie czy jestesmy ostatni w grupie (komplet)
    if (park->people_in_queue == M_GROUP_SIZE) {
        printf("[TURYSTA %d] Komplet! Budzę przewodnika!\n", id);
        
        park->people_in_queue = 0; // zerujemy licznik dla nastepnej grupy
        sem_unlock(sem_id, 1);     // budzimy przewodnika (semafor 1)
    }
    
    // czekamy na znak od przewodnika (semafor 2)
    sem_lock(sem_id, 2);
    
    // koniec czekania - wycieczka!
    printf("[TURYSTA %d] Zwiedzam z przewodnikiem!\n", id);
    
    // -----------------------------------------------------------
    // symulacja zwiedzania (przewodnik prowadzi po trasie)
    // -----------------------------------------------------------
    
    // symulujemy ze turysta cos robi
    sleep(2 + (rand() % 3)); // 2-4 sekundy "zwiedzania"
    
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