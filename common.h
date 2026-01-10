#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/msg.h>
#include <errno.h>
#include <time.h>
#include <string.h>

//stałe konfiguracyjne do symulacji
#define N_PARK_CAPACITY 10 //max osob w parku
#define M_GROUP_SIZE 5 // liczebność grupy
#define X1_BRIDGE_CAP 3 // pojemność mostu
#define X1_TOWER_CAP 3 // pojemność wieży
#define X1_FERRY_CAP 3 // pojemność promu
#define P_guides 3 // liczba przewodników

// definicje ideksow semaforow
#define SEM_KASA 0 // limit wejsc do parku
#define SEM_PRZEWODNIK 1 // przewodnik czeka na skompletowanie grupy
#define SEM_START_WYCIECZKI 2 // turysci czekaja na start wycieczki
#define SEM_KONIEC_WYCIECZKI 3 // turysci czekaja na koniec wycieczki

// klucze ipc
#define SHM_KEY_ID 1234
#define SEM_KEY_ID 5678
#define MSG_KEY_ID 9012

// typy komunikatow w kolejce
#define MSG_TYPE_ENTRY 1 // turysta wchodzi do parku
#define MSG_TYPE_EXIT 2 // turysta wychodzi z parku
#define MSG_TYPE_REPORT 3 // raport od przewodnika

// struktura komunikatu dla kolejki komunikatow
struct msg_buffer {
    long msg_type; // typ wiadomosci (1=wejscie, 2=wyjscie, 3=raport)
    int tourist_id; // id turysty/przewodnika
    int age; // wiek turysty
    int is_vip; // czy ma legitymacje PTTK (0/1)
    char info[256]; // dodatkowe informacje (np. czas, trasa)
};

struct ParkSharedMemory {
    // statystyki ogolne
    int total_entered; // calkowita liczba wejsc
    int total_exited; // calkowita liczba wyjsc
    int people_in_park; // aktualna liczba osob w parku
    
    // system grupowania
    int people_in_queue; // ile osob czeka na przewodnika
    
    // atrakcje - stany
    int bridge_current_count; // ile osob na moscie
    int bridge_direction; // kierunek ruchu (0=K->A, 1=A->K)
    int bridge_waiting_ka; // ile czeka K->A
    int bridge_waiting_ak; // ile czeka A->K
    
    int tower_current_count; // ile osob na wiezy
    int tower_visitors[20]; // tablica PID turystow w wiezy (dla sygnalu)
    
    int ferry_current_count; // ile osob na promie
    int ferry_position; // pozycja promu (0=brzeg A, 1=brzeg B)
    
    // flagi awaryjne (dla sygnalow)
    int emergency_tower; // flaga ewakuacji z wiezy (SIGUSR1)
    int emergency_exit; // flaga ewakuacji grupy (SIGUSR2)
    
    // statystyki vip
    int vip_in_park; // ile osob vip w parku
};

union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
};

// funkcje pomocnicze do semaforow

// opuszczenie semafora (czekaj / P / wait)
void sem_lock(int sem_id, int sem_num) {
    struct sembuf operacja;
    operacja.sem_num = sem_num; // ktory semafor (0 = kasa)
    operacja.sem_op = -1;       // zmniejsz o 1 (zajmij miejsce)
    operacja.sem_flg = 0;
    
    if (semop(sem_id, &operacja, 1) == -1) {
        perror("Błąd sem_lock");
        exit(1);
    }
}

// podniesienie semafora (sygnal / V / signal)
void sem_unlock(int sem_id, int sem_num) {
    struct sembuf operacja;
    operacja.sem_num = sem_num;
    operacja.sem_op = 1;        // zwieksz o 1 (zwolnij miejsce)
    operacja.sem_flg = 0;
    
    if (semop(sem_id, &operacja, 1) == -1) {
        perror("Błąd sem_unlock");
        exit(1);
    }
}

// funkcja pomocnicza do pobierania aktualnego czasu (timestamp)
void get_timestamp(char *buffer, size_t size) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    strftime(buffer, size, "%H:%M:%S", t);
}

#endif