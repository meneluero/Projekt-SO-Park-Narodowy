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
#include <signal.h>

#define N_PARK_CAPACITY 500
#define M_GROUP_SIZE 5
#define X1_BRIDGE_CAP 3
#define X2_TOWER_CAP 6 
#define X3_FERRY_CAP 7 
#define MAX_GROUPS 10

#define BRIDGE_CROSS_TIME 2
#define TOWER_VISIT_TIME 3 
#define FERRY_TRAVEL_TIME 2 

#define PHASE_WAITING 0
#define PHASE_READY 1
#define PHASE_ATTRACTION 2
#define PHASE_FINISHED 3

#define ATTR_NONE 0
#define ATTR_BRIDGE 1
#define ATTR_TOWER 2 
#define ATTR_FERRY 3

#define DIR_NONE -1
#define DIR_KA 0
#define DIR_AK 1

#define FERRY_SHORE_START 0
#define FERRY_SHORE_END 1

#define SEM_PARK_LIMIT 0
#define SEM_PRZEWODNIK 1
#define SEM_QUEUE_MUTEX 2
#define SEM_STATS_MUTEX 3

#define SEM_MOST_LIMIT 4
#define SEM_MOST_MUTEX 5
#define SEM_WIEZA_LIMIT 6
#define SEM_WIEZA_MUTEX 7
#define SEM_PROM_MUTEX 8

#define SEM_GROUP_DONE_BASE 11
#define SEM_GROUP_MUTEX 21

#define SEM_TOURIST_ASSIGNED_BASE 22
#define SEM_TOURIST_READ_DONE_BASE 27

#define SEM_BRIDGE_WAIT_KA 32
#define SEM_BRIDGE_WAIT_AK 33

#define SEM_FERRY_CONTROL 34
#define SEM_FERRY_BOARD 35
#define SEM_FERRY_ALL_ABOARD 36
#define SEM_FERRY_ARRIVE 37
#define SEM_FERRY_DISEMBARK 38

#define SEM_MEMBER_GO_BASE 39

#define SEM_QUEUE_SLOTS 49
#define SEM_GROUP_SLOTS 50

#define TOTAL_SEMAPHORES 51

#define SEM_GROUP_DONE(gid)  (SEM_GROUP_DONE_BASE + (gid))
#define SEM_TOURIST_ASSIGNED(pos) (SEM_TOURIST_ASSIGNED_BASE + (pos))
#define SEM_TOURIST_READ_DONE(pos) (SEM_TOURIST_READ_DONE_BASE + (pos))
#define SEM_BRIDGE_WAIT(dir) ((dir) == DIR_KA ? SEM_BRIDGE_WAIT_KA : SEM_BRIDGE_WAIT_AK)
#define SEM_MEMBER_GO(group) (SEM_MEMBER_GO_BASE + (group))

#define SHM_KEY_ID 1234
#define SEM_KEY_ID 5678
#define MSG_KEY_ID 9012
#define FIFO_PATH "/tmp/park_reports.fifo"

#define MSG_TYPE_ENTRY 1
#define MSG_TYPE_EXIT 2
#define MSG_TYPE_REPORT 3

struct msg_buffer {
    long msg_type; 
    int tourist_id;
    int age;
    int is_vip; 
    char info[256]; 
};

struct GroupState {
    int active;
    int guide_id;
    pid_t guide_pid;

    int route;
    int size;
    int current_attraction;
    int attraction_step;

    int tourists_ready;
    int tourists_on_attraction;

    pid_t member_pids[M_GROUP_SIZE];
    int member_ids[M_GROUP_SIZE];
    int member_ages[M_GROUP_SIZE];
    int member_vips[M_GROUP_SIZE];
    int member_is_caretaker[M_GROUP_SIZE];

    int signal_tower_evacuate;
    int signal_emergency_exit;
};

struct ParkSharedMemory {

    int total_entered;
    int total_exited;
    int total_expected;
    int people_in_park;
    int vip_in_park;

    int people_in_queue;
    int queue_ages[N_PARK_CAPACITY];
    int queue_ids[N_PARK_CAPACITY];
    int queue_vips[N_PARK_CAPACITY];
    pid_t queue_pids[N_PARK_CAPACITY];
    int assigned_group_id[N_PARK_CAPACITY];
    int assigned_member_index[N_PARK_CAPACITY];

    struct GroupState groups[MAX_GROUPS];
    int next_group_slot;

    int bridge_on_bridge;
    int bridge_direction;
    int bridge_waiting[2];

    int tower_current_count;
    pid_t tower_visitors[X2_TOWER_CAP];

    int ferry_position;
    int ferry_passengers;
    int ferry_expected;
    int ferry_disembarked;
    int ferry_current_group;
};

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

static inline int sem_lock_interruptible(int sem_id, int sem_num, volatile sig_atomic_t *interrupt_flag) {
    struct sembuf op;
    op.sem_num = sem_num;
    op.sem_op = -1;
    op.sem_flg = 0;

    while (semop(sem_id, &op, 1) == -1) {
        if (errno == EINTR) {
            if (interrupt_flag != NULL && *interrupt_flag) {
                return -1;
            }
            continue;
        }
        perror("Błąd sem_lock_interruptible");
        exit(1);
    }
    return 0;
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

static inline int sem_trylock(int sem_id, int sem_num) {
    struct sembuf op;
    op.sem_num = sem_num;
    op.sem_op = -1;
    op.sem_flg = IPC_NOWAIT;

    if (semop(sem_id, &op, 1) == -1) {
        if (errno == EAGAIN) {
            return -1; 
        }
        if (errno == EINTR) {
            return -1; 
        }
        perror("Błąd sem_trylock");
        exit(1);
    }
    return 0;
}

static inline int sem_getval(int sem_id, int sem_num) {
    int val = semctl(sem_id, sem_num, GETVAL);
    if (val == -1) {
        perror("Błąd sem_getval");
        exit(1);
    }
    return val;
}

static inline void get_timestamp(char *buffer, size_t size) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    strftime(buffer, size, "%H:%M:%S", t);
}

static inline int get_attraction_for_step(int route, int step) {
    if (route == 1) {
        switch(step) {
            case 0: return ATTR_BRIDGE;
            case 1: return ATTR_TOWER;
            case 2: return ATTR_FERRY;
            default: return ATTR_NONE;
        }
    } else {
        switch(step) {
            case 0: return ATTR_FERRY;
            case 1: return ATTR_TOWER;
            case 2: return ATTR_BRIDGE;
            default: return ATTR_NONE;
        }
    }
}

static inline int get_bridge_direction(int route) {
    return (route == 1) ? DIR_KA : DIR_AK;
}

static inline int get_ferry_direction(int route) {
    return (route == 1) ? 0 : 1;
}

static inline int tower_add_visitor(struct ParkSharedMemory *park, pid_t pid) {
    for (int i = 0; i < X2_TOWER_CAP; i++) {
        if (park->tower_visitors[i] == 0) {
            park->tower_visitors[i] = pid;
            return i;
        }
    }
    return -1; 

}

static inline void tower_remove_visitor(struct ParkSharedMemory *park, pid_t pid) {
    for (int i = 0; i < X2_TOWER_CAP; i++) {
        if (park->tower_visitors[i] == pid) {
            park->tower_visitors[i] = 0;
            return;
        }
    }
}

static inline int tower_has_visitor(struct ParkSharedMemory *park, pid_t pid) {
    for (int i = 0; i < X2_TOWER_CAP; i++) {
        if (park->tower_visitors[i] == pid) {
            return 1;
        }
    }
    return 0;
}

#endif