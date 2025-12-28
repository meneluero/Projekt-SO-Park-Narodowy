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

//stałe konfiguracyjne do symulacji
#define N_PARK_CAPACITY 10 //max osob w parku
#define M_GROUP_SIZE 5 // liczebność grupy
#define X1_BRIDGE_CAP 3 // pojemność mostu
#define X1_TOWER_CAP 3 // pojemność wieży
#define X1_FERRY_CAP 3 // pojemność promu
#define P_guides 3 // liczba przewodników

// klucze ipc
#define SHM_KEY_ID 1234
#define SEM_KEY_ID 5678
#define MSG_KEY_ID 9012

struct ParkSharedMemory {
    int people_in_park;
    
    int people_in_queue;

    int bridge_current_count;

    int tower_current_count;

    int ferry_current_count;
    int ferry_is_at_bank;

    //do uzupelnienia
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
#endif