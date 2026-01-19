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

// stale konfiguracyjne symulacji
#define N_PARK_CAPACITY 10
#define M_GROUP_SIZE 5
#define X1_BRIDGE_CAP 3
#define X2_TOWER_CAP 6 
#define X3_FERRY_CAP 7 
#define MAX_GROUPS 10 

// czasy atrakcji
#define BRIDGE_CROSS_TIME 2
#define TOWER_VISIT_TIME 3 
#define FERRY_TRAVEL_TIME 2 

// fazy wycieczki
#define PHASE_WAITING 0
#define PHASE_READY 1
#define PHASE_ATTRACTION 2
#define PHASE_FINISHED 3

// indeksy atrakcji w trasie
#define ATTR_NONE 0
#define ATTR_BRIDGE 1
#define ATTR_TOWER 2 
#define ATTR_FERRY 3

// kierunki na moscie i promie
#define DIR_NONE -1
#define DIR_KA 0
#define DIR_AK 1

// pozycje promu
#define FERRY_SHORE_START 0
#define FERRY_SHORE_END 1

// semafory podstawowe
#define SEM_PARK_LIMIT 0  
#define SEM_PRZEWODNIK 1
#define SEM_QUEUE_MUTEX 2
#define SEM_STATS_MUTEX 3

// semafory atrakcji
#define SEM_MOST_LIMIT 4 
#define SEM_MOST_MUTEX 5
#define SEM_WIEZA_LIMIT 6
#define SEM_WIEZA_MUTEX 7
#define SEM_PROM_LIMIT 8
#define SEM_PROM_MUTEX 9
#define SEM_PROM_ARRIVED 10

// semafory dla grup
#define SEM_GROUP_START_BASE 11
#define SEM_GROUP_DONE_BASE 21
#define SEM_GROUP_MUTEX 31

#define TOTAL_SEMAPHORES 32

// makra pomocnicze dla semaforow grupowych
#define SEM_GROUP_START(gid) (SEM_GROUP_START_BASE + (gid))
#define SEM_GROUP_DONE(gid)  (SEM_GROUP_DONE_BASE + (gid))

// klucze ipc
#define SHM_KEY_ID 1234
#define SEM_KEY_ID 5678
#define MSG_KEY_ID 9012
#define FIFO_PATH "/tmp/park_reports.fifo"

// typy komunikatow w kolejce
#define MSG_TYPE_ENTRY 1
#define MSG_TYPE_EXIT 2
#define MSG_TYPE_REPORT 3


// struktura komunikatu
struct msg_buffer {
    long msg_type; 
    int tourist_id;
    int age;
    int is_vip; 
    char info[256]; 
};

// struktura stanu grupy
struct GroupState {
    int active;
    int guide_id;
    pid_t guide_pid;
    
    int route;
    int current_attraction; 
    int attraction_step;
    
    int tourists_ready;
    int tourists_on_attraction;
    
    // dane czlonkow grupy
    pid_t member_pids[M_GROUP_SIZE];
    int member_ids[M_GROUP_SIZE];
    int member_ages[M_GROUP_SIZE];
    int member_vips[M_GROUP_SIZE];
    
    // flagi sygnalow dla tej grupy
    int signal_tower_evacuate;
    int signal_emergency_exit;
};

// struktura pamieci dzielonej
struct ParkSharedMemory {
    // statystyki ogolne
    int total_entered;
    int total_exited;
    int people_in_park;
    int vip_in_park;
    
    // kolejka oczekujacych na grupe
    int people_in_queue;
    int queue_ages[M_GROUP_SIZE];
    int queue_vips[M_GROUP_SIZE];
    pid_t queue_pids[M_GROUP_SIZE];
    int queue_ids[M_GROUP_SIZE];
    
    // system grup
    struct GroupState groups[MAX_GROUPS];
    int next_group_slot;
    
    // stan mostu
    int bridge_current_count;
    int bridge_direction;
    int bridge_waiting[2];
    int bridge_crossing[2];
    
    // stan wiezy
    int tower_current_count;
    pid_t tower_visitors[X2_TOWER_CAP];
    
    // stan promu
    int ferry_current_count;
    int ferry_position;
    int ferry_waiting[2];
    int ferry_moving;
    int ferry_group_id;
};

// union do operacji na semaforach
union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
};

static inline void sem_lock(int sem_id, int sem_num) {
    struct sembuf op;
    op.sem_num = sem_num;
    op.sem_op = -1;
    op.sem_flg = 0;
    
    while (semop(sem_id, &op, 1) == -1) {
        if (errno == EINTR) {
            continue;
        }
        perror("Błąd sem_lock");
        exit(1);
    }
}

static inline void sem_unlock(int sem_id, int sem_num) {
    struct sembuf op;
    op.sem_num = sem_num;
    op.sem_op = 1;
    op.sem_flg = 0;
    
    while (semop(sem_id, &op, 1) == -1) {
        if (errno == EINTR) {
            continue;
        }
        perror("Błąd sem_unlock");
        exit(1);
    }
}

// proba opuszczenia semafora bez blokowania
static inline int sem_trylock(int sem_id, int sem_num) {
    struct sembuf op;
    op.sem_num = sem_num;
    op.sem_op = -1;
    op.sem_flg = IPC_NOWAIT;
    
    if (semop(sem_id, &op, 1) == -1) {
        if (errno == EAGAIN) {
            return -1; // semafor niedostepny
        }
        if (errno == EINTR) {
            return -1; // przerwane sygnalem
        }
        perror("Błąd sem_trylock");
        exit(1);
    }
    return 0;
}
// pobranie aktualnej wartosci semafora
static inline int sem_getval(int sem_id, int sem_num) {
    int val = semctl(sem_id, sem_num, GETVAL);
    if (val == -1) {
        perror("Błąd sem_getval");
        exit(1);
    }
    return val;
}

// pobieranie aktualnego czasu jako string
static inline void get_timestamp(char *buffer, size_t size) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    strftime(buffer, size, "%H:%M:%S", t);
}

// kolejnosc atrakcji dla danej trasy
static inline int get_attraction_for_step(int route, int step) {
    if (route == 1) {
        // trasa 1
        switch(step) {
            case 0: return ATTR_BRIDGE;
            case 1: return ATTR_TOWER;
            case 2: return ATTR_FERRY;
            default: return ATTR_NONE;
        }
    } else {
        // trasa 2
        switch(step) {
            case 0: return ATTR_FERRY;
            case 1: return ATTR_TOWER;
            case 2: return ATTR_BRIDGE;
            default: return ATTR_NONE;
        }
    }
}

// zwraca kierunek mostu dla danej trasy
static inline int get_bridge_direction(int route) {
    return (route == 1) ? DIR_KA : DIR_AK;
}

// zwraca kierunek promu dla danej strony
static inline int get_ferry_direction(int route) {
    return (route == 1) ? 0 : 1;
}

// dodaje pid do listy osob na wiezy
static inline int tower_add_visitor(struct ParkSharedMemory *park, pid_t pid) {
    for (int i = 0; i < X2_TOWER_CAP; i++) {
        if (park->tower_visitors[i] == 0) {
            park->tower_visitors[i] = pid;
            return i;
        }
    }
    return -1; // brak miejsca nie powinno sie wydarzyc
}

// usuwa pid z listy osob na wiezy
static inline void tower_remove_visitor(struct ParkSharedMemory *park, pid_t pid) {
    for (int i = 0; i < X2_TOWER_CAP; i++) {
        if (park->tower_visitors[i] == pid) {
            park->tower_visitors[i] = 0;
            return;
        }
    }
}

// sprawdzy czy pid jest na wiezy
static inline int tower_has_visitor(struct ParkSharedMemory *park, pid_t pid) {
    for (int i = 0; i < X2_TOWER_CAP; i++) {
        if (park->tower_visitors[i] == pid) {
            return 1;
        }
    }
    return 0;
}

#endif