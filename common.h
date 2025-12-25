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
#define N_PARK_CAPACITY 20 //max osob w parku
#define M_group_size 5 // liczebność grupy
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
    int people_in_query;

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

#endif