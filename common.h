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

// kolory do wypisywania logow w terminalu
#define CLR_RESET "\033[0m"
#define CLR_RED "\033[0;31m"
#define CLR_GREEN "\033[0;32m"
#define CLR_YELLOW "\033[0;33m"
#define CLR_BLUE "\033[0;34m"
#define CLR_MAGENTA "\033[0;35m"
#define CLR_CYAN "\033[0;36m"
#define CLR_WHITE "\033[0;37m"
#define CLR_ORANGE "\033[38;5;208m"
#define CLR_GRAY "\033[38;5;245m"
#define CLR_BOLD "\033[1m"

#define CLR_BG_RED "\033[41m"
#define CLR_BG_GREEN "\033[42m"
#define CLR_BG_BLUE "\033[44m"

// limity i stale konfiguracyjne symulacji
#define N_PARK_CAPACITY 500
#define M_GROUP_SIZE 10
#define X1_BRIDGE_CAP 9
#define X2_TOWER_CAP 18 
#define X3_FERRY_CAP 12 
#define MAX_GROUPS 15

// czasy trwania atrakcji
// #define BRIDGE_CROSS_TIME_MIN 500000 
// #define BRIDGE_CROSS_TIME_MAX 2000000 
// #define TOWER_VISIT_TIME_MIN 1000000 
// #define TOWER_VISIT_TIME_MAX 3000000
// #define FERRY_TRAVEL_TIME_MIN 1000000
// #define FERRY_TRAVEL_TIME_MAX 2000000
// #define WALK_TIME_MIN 200000  
// #define WALK_TIME_MAX 1000000
#define TOURIST_ARRIVAL_MIN 1000 
#define TOURIST_ARRIVAL_MAX 50000

#ifndef BRIDGE_CROSS_TIME_MIN
#define BRIDGE_CROSS_TIME_MIN 0
#define BRIDGE_CROSS_TIME_MAX 0
#endif
#ifndef TOWER_VISIT_TIME_MIN
#define TOWER_VISIT_TIME_MIN 0
#define TOWER_VISIT_TIME_MAX 0
#endif
#ifndef FERRY_TRAVEL_TIME_MIN
#define FERRY_TRAVEL_TIME_MIN 0
#define FERRY_TRAVEL_TIME_MAX 0
#endif
#ifndef WALK_TIME_MIN
#define WALK_TIME_MIN 0
#define WALK_TIME_MAX 0
#endif
#ifndef TOURIST_ARRIVAL_MIN
#define TOURIST_ARRIVAL_MIN 0
#define TOURIST_ARRIVAL_MAX 0
#endif

// stale pomocnicze do logiki atrakcji
#define BRIDGE_CROSS_TIME 0 
#define TOWER_VISIT_TIME 0
#define FERRY_TRAVEL_TIME 0

// fazy zwiedzania
#define PHASE_WAITING 0
#define PHASE_READY 1
#define PHASE_ATTRACTION 2
#define PHASE_FINISHED 3

// identyfikatory atrakcji
#define ATTR_NONE 0
#define ATTR_BRIDGE 1
#define ATTR_TOWER 2 
#define ATTR_FERRY 3

// kierunki ruchu
#define DIR_NONE -1
#define DIR_KA 0
#define DIR_AK 1

// definicje stron rzeki dla promu
#define FERRY_SHORE_START 0
#define FERRY_SHORE_END 1

// indeksy semaforow w tablicy semaforow
#define SEM_PARK_LIMIT 0
#define SEM_PRZEWODNIK 1
#define SEM_QUEUE_MUTEX 2
#define SEM_STATS_MUTEX 3

// semafory atrakcji
#define SEM_MOST_LIMIT 4
#define SEM_MOST_MUTEX 5
#define SEM_WIEZA_LIMIT 6
#define SEM_WIEZA_MUTEX 7
#define SEM_PROM_MUTEX 8

// bazy indeksow semaforow dla grup i turystow
#define SEM_GROUP_DONE_BASE 11
#define SEM_GROUP_MUTEX (SEM_GROUP_DONE_BASE + MAX_GROUPS)

#define SEM_TOURIST_ASSIGNED_BASE (SEM_GROUP_MUTEX + 1)
#define SEM_TOURIST_READ_DONE_BASE (SEM_TOURIST_ASSIGNED_BASE + N_PARK_CAPACITY)

// semafory kolejkowania do mostu
#define SEM_BRIDGE_WAIT_KA (SEM_TOURIST_READ_DONE_BASE + N_PARK_CAPACITY)
#define SEM_BRIDGE_WAIT_AK (SEM_BRIDGE_WAIT_KA + 1)

// semafory kolejkowania do promu
#define SEM_FERRY_WAIT_KA (SEM_BRIDGE_WAIT_AK + 1)
#define SEM_FERRY_WAIT_AK (SEM_FERRY_WAIT_KA + 1)
#define SEM_FERRY_VIP_WAIT_KA (SEM_FERRY_WAIT_AK + 1)
#define SEM_FERRY_VIP_WAIT_AK (SEM_FERRY_VIP_WAIT_KA + 1)
#define SEM_FERRY_CAP (SEM_FERRY_VIP_WAIT_AK + 1)
#define SEM_FERRY_GUIDE_READY_BASE (SEM_FERRY_CAP + 1)

#define SEM_MEMBER_GO_BASE (SEM_FERRY_GUIDE_READY_BASE + MAX_GROUPS)

// semafory pomocnicze
#define SEM_QUEUE_SLOTS (SEM_MEMBER_GO_BASE + MAX_GROUPS * M_GROUP_SIZE)
#define SEM_GROUP_SLOTS (SEM_QUEUE_SLOTS + 1)
#define SEM_TOWER_WAIT (SEM_GROUP_SLOTS + 1)
#define SEM_CASH_QUEUE_MUTEX (SEM_TOWER_WAIT + 1)
#define SEM_CASH_QUEUE_SLOTS (SEM_CASH_QUEUE_MUTEX + 1)
#define SEM_TOWER_STAIRS_UP (SEM_CASH_QUEUE_SLOTS + 1)
#define SEM_TOWER_STAIRS_DOWN (SEM_TOWER_STAIRS_UP + 1)
#define SEM_TOWER_VIP_WAIT (SEM_TOWER_STAIRS_DOWN + 1)
#define SEM_TOWER_NORMAL_WAIT (SEM_TOWER_VIP_WAIT + 1)
#define SEM_BRIDGE_GUIDE_READY_BASE (SEM_TOWER_NORMAL_WAIT + 1)

#define TOTAL_SEMAPHORES (SEM_BRIDGE_GUIDE_READY_BASE + MAX_GROUPS)

// makra ulatwiajace dostep do konkretnych semaforow w tablicy
#define SEM_GROUP_DONE(gid)  (SEM_GROUP_DONE_BASE + (gid))
#define SEM_TOURIST_ASSIGNED(pos) (SEM_TOURIST_ASSIGNED_BASE + (pos))
#define SEM_TOURIST_READ_DONE(pos) (SEM_TOURIST_READ_DONE_BASE + (pos))
#define SEM_BRIDGE_WAIT(dir) ((dir) == DIR_KA ? SEM_BRIDGE_WAIT_KA : SEM_BRIDGE_WAIT_AK)
#define SEM_MEMBER_GO(group, member) (SEM_MEMBER_GO_BASE + (group) * M_GROUP_SIZE + (member))
#define SEM_BRIDGE_GUIDE_READY(group) (SEM_BRIDGE_GUIDE_READY_BASE + (group))
#define SEM_FERRY_GUIDE_READY(group) (SEM_FERRY_GUIDE_READY_BASE + (group))
#define SEM_FERRY_WAIT(dir) ((dir) == 0 ? SEM_FERRY_WAIT_KA : SEM_FERRY_WAIT_AK)
#define SEM_FERRY_VIP_WAIT(dir) ((dir) == 0 ? SEM_FERRY_VIP_WAIT_KA : SEM_FERRY_VIP_WAIT_AK)

// klucze ipc i sciezki
#define FTOK_PATH "./main"
#define FTOK_SHM_ID 'S'
#define FTOK_SEM_ID 'E'
#define FTOK_MSG_ID 'M'
#define FTOK_MSG_REPORT_ID 'R'
#define FIFO_PATH "/tmp/park_reports.fifo"
#define TICKET_PRICE 50

// typy komunikatow
#define MSG_TYPE_ENTRY 1
#define MSG_TYPE_EXIT 2
#define MSG_TYPE_REPORT 3
#define MSG_TYPE_EXIT_NOTICE 4

// struktura komunikatu do kolejki
struct msg_buffer {
    long msg_type; 
    int tourist_id;
    int age;
    int is_vip; 
    char info[256]; 
};

// struktura przechowujaca stan pojedynczej grupy
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
    int member_caretaker_of[M_GROUP_SIZE];  
    int member_has_caretaker[M_GROUP_SIZE];
    int member_caretaker_is_guide[M_GROUP_SIZE];

    int signal_tower_evacuate;
    int signal_emergency_exit;
};

// glowna struktura pamieci dzielonej
struct ParkSharedMemory {

    time_t park_open_time;
    time_t park_closing_time;
    int park_closed;
    int rejected_after_close;

    int total_entered;
    int total_exited;
    int total_expected;
    int total_revenue;
    int paid_entries;
    int free_entries_vip;
    int free_entries_children;
    int people_in_park;
    int vip_in_park;

    // zarzadzanie kolejka wejsciowa
    int people_in_queue;
    int queue_head;
    int queue_tail;
    int cash_queue_count;
    int queue_ages[N_PARK_CAPACITY];
    int queue_ids[N_PARK_CAPACITY];
    int queue_vips[N_PARK_CAPACITY];
    pid_t queue_pids[N_PARK_CAPACITY];
    int assigned_group_id[N_PARK_CAPACITY];
    int assigned_member_index[N_PARK_CAPACITY];

    struct GroupState groups[MAX_GROUPS];
    int next_group_slot;

    // stan mostu
    int bridge_on_bridge;
    int bridge_direction;
    int bridge_waiting[2];

    // stan wiezy
    int tower_current_count;
    pid_t tower_visitors[X2_TOWER_CAP];
    int tower_waiting_vip;
    int tower_waiting_normal;

    // stan promu
    int ferry_position;
    int ferry_passengers;
    int ferry_expected;
    int ferry_disembarked;
    int ferry_current_group;
    int ferry_on_ferry;
    int ferry_direction;
    int ferry_waiting_vip[2];
    int ferry_waiting_normal[2];
};

// unia dla semaforow
union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
};

// funkcje pomocnicze inline

// wypisanie bledu bez konczenia programu
static inline void report_error(const char *context) {
    perror(context);
}

// wypisanie bledu i zakonczenie programu
static inline void fatal_error(const char *context) {
    perror(context);
    exit(1);
}

// operacja opuszczenia semafora
static inline void sem_lock(int sem_id, int sem_num) {
    struct sembuf op;
    op.sem_num = sem_num;
    op.sem_op = -1;
    op.sem_flg = 0;

    while (semop(sem_id, &op, 1) == -1) {
        if (errno == EINTR) { // wznowienie jesli przerwane sygnalem
            continue;
        }
        fatal_error("Błąd sem_lock");
    }
}

// operacja opuszczenia semafora z obsluga flagi przerwania
static inline int sem_lock_interruptible(int sem_id, int sem_num, volatile sig_atomic_t *interrupt_flag) {
    struct sembuf op;
    op.sem_num = sem_num;
    op.sem_op = -1;
    op.sem_flg = 0;

    while (semop(sem_id, &op, 1) == -1) {
        if (errno == EINTR) { 
            if (interrupt_flag != NULL && *interrupt_flag) {
                return -1; // przerwanie przez sygnał
            }
            continue;
        }
        fatal_error("Błąd sem_lock_interruptible");
    }
    return 0;
}

// operacja podniesienia semafora
static inline void sem_unlock(int sem_id, int sem_num) {
    struct sembuf op;
    op.sem_num = sem_num;
    op.sem_op = 1;
    op.sem_flg = 0;

    while (semop(sem_id, &op, 1) == -1) {
        if (errno == EINTR) {
            continue;
        }
        fatal_error("Błąd sem_unlock");
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
            return -1; 
        }
        if (errno == EINTR) {
            return -1; 
        }
        fatal_error("Błąd sem_trylock");
    }
    return 0;
}

// pobranie wartosci semafora
static inline int sem_getval(int sem_id, int sem_num) {
    int val = semctl(sem_id, sem_num, GETVAL);
    if (val == -1) {
        fatal_error("Błąd sem_getval");
    }
    return val;
}

// czekanie na semaforze z limitem czasu
static inline int sem_timed_wait(int sem_id, int sem_num, int seconds,volatile sig_atomic_t *flag1, volatile sig_atomic_t *flag2) {
    struct sembuf op;
    op.sem_num = sem_num;
    op.sem_op = -1;
    op.sem_flg = 0;

    struct timespec deadline;
    clock_gettime(CLOCK_MONOTONIC, &deadline);
    deadline.tv_sec += seconds;

    while (1) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);

        struct timespec remaining;
        remaining.tv_sec = deadline.tv_sec - now.tv_sec;
        remaining.tv_nsec = deadline.tv_nsec - now.tv_nsec;
        if (remaining.tv_nsec < 0) {
            remaining.tv_sec--;
            remaining.tv_nsec += 1000000000L;
        }
        if (remaining.tv_sec < 0) {
            return 1; // timeout
        }

        int ret = semtimedop(sem_id, &op, 1, &remaining);
        if (ret == 0) {
            return 0; // sukces
        }
        if (errno == EAGAIN) {
            return 1; // timeout z errno
        }
        if (errno == EINTR) {
            if ((flag1 != NULL && *flag1) || (flag2 != NULL && *flag2)) {
                return -1; // przerwanie
            }
            continue;
        }
        fatal_error("Błąd sem_timed_wait");
    }
}

// logika wejscia na prom
static inline int ferry_enter(struct ParkSharedMemory *park, int sem_id, int direction, int is_vip, volatile sig_atomic_t *interrupt_flag) {
    while (1) {
        sem_lock(sem_id, SEM_PROM_MUTEX);
        if (park->ferry_direction == DIR_NONE || park->ferry_direction == direction) {
            // jesli vip czeka a my nie jestesmy vipem - czekamy
            if (!is_vip && park->ferry_waiting_vip[direction] > 0) {
                park->ferry_waiting_normal[direction]++;
                sem_unlock(sem_id, SEM_PROM_MUTEX);

                // czekanie na semaforze kolejki normalnej
                if (sem_lock_interruptible(sem_id, SEM_FERRY_WAIT(direction), interrupt_flag) == -1) {
                    sem_lock(sem_id, SEM_PROM_MUTEX);
                    if (park->ferry_waiting_normal[direction] > 0) {
                        park->ferry_waiting_normal[direction]--;
                    }
                    sem_unlock(sem_id, SEM_PROM_MUTEX);
                    return -1;
                }

                // sprawdzenie pojemnosci promu
                if (sem_lock_interruptible(sem_id, SEM_FERRY_CAP, interrupt_flag) == -1) {
                    return -1;
                }

                sem_lock(sem_id, SEM_PROM_MUTEX);
                park->ferry_on_ferry++;
                sem_unlock(sem_id, SEM_PROM_MUTEX);
                return 0;
            } else {
                park->ferry_direction = direction;
                park->ferry_on_ferry++;
                sem_unlock(sem_id, SEM_PROM_MUTEX);

                if (sem_lock_interruptible(sem_id, SEM_FERRY_CAP, interrupt_flag) == -1) {
                    sem_lock(sem_id, SEM_PROM_MUTEX);
                    park->ferry_on_ferry--;
                    if (park->ferry_on_ferry == 0) {
                        park->ferry_direction = DIR_NONE;
                    }
                    sem_unlock(sem_id, SEM_PROM_MUTEX);
                    return -1;
                }
                return 0;
            }
        }

        // jesli zly kierunek, ustawiamy sie w kolejce
        if (is_vip) {
            park->ferry_waiting_vip[direction]++;
        } else {
            park->ferry_waiting_normal[direction]++;
        }
        sem_unlock(sem_id, SEM_PROM_MUTEX);

        if (sem_lock_interruptible(sem_id, is_vip ? SEM_FERRY_VIP_WAIT(direction) : SEM_FERRY_WAIT(direction), interrupt_flag) == -1) {
            // obsluga przerwania czekania
            sem_lock(sem_id, SEM_PROM_MUTEX);
            if (is_vip && park->ferry_waiting_vip[direction] > 0) {
                park->ferry_waiting_vip[direction]--;
            } else if (!is_vip && park->ferry_waiting_normal[direction] > 0) {
                park->ferry_waiting_normal[direction]--;
            }
            sem_unlock(sem_id, SEM_PROM_MUTEX);
            return -1;
        }

        if (sem_lock_interruptible(sem_id, SEM_FERRY_CAP, interrupt_flag) == -1) {
            return -1;
        }

        sem_lock(sem_id, SEM_PROM_MUTEX);
        park->ferry_on_ferry++;
        sem_unlock(sem_id, SEM_PROM_MUTEX);
        return 0;
    }
}

// logika zejscia z promu i budzenia czekajacych
static inline void ferry_leave(struct ParkSharedMemory *park, int sem_id, int direction) {
    sem_unlock(sem_id, SEM_FERRY_CAP);

    sem_lock(sem_id, SEM_PROM_MUTEX);
    park->ferry_on_ferry--;

    if (park->ferry_on_ferry == 0) {
        int other_dir = 1 - direction;
        int wake_dir = -1;

        // priorytet dla czekajacych z drugiej strony
        if (park->ferry_waiting_vip[other_dir] > 0 || park->ferry_waiting_normal[other_dir] > 0) {
            wake_dir = other_dir;
        } else if (park->ferry_waiting_vip[direction] > 0 || park->ferry_waiting_normal[direction] > 0) {
            wake_dir = direction;
        }

        if (wake_dir != -1) {
            int vip_to_wake = park->ferry_waiting_vip[wake_dir];
            int normal_to_wake = park->ferry_waiting_normal[wake_dir];
            park->ferry_direction = wake_dir;
            // reset licznikow oczekujacych
            if (vip_to_wake > 0) {
                park->ferry_waiting_vip[wake_dir] = 0;
            } else {
                park->ferry_waiting_normal[wake_dir] = 0;
            }
            sem_unlock(sem_id, SEM_PROM_MUTEX);

            // budzenie procesow (najpierw vipy)
            if (vip_to_wake > 0) {
                for (int i = 0; i < vip_to_wake; i++) {
                    sem_unlock(sem_id, SEM_FERRY_VIP_WAIT(wake_dir));
                }
            } else {
                for (int i = 0; i < normal_to_wake; i++) {
                    sem_unlock(sem_id, SEM_FERRY_WAIT(wake_dir));
                }
            }
        } else {
            park->ferry_direction = DIR_NONE;
            sem_unlock(sem_id, SEM_PROM_MUTEX);
        }
    } else {
        sem_unlock(sem_id, SEM_PROM_MUTEX);
    }
}

// symulacja uplywu czasu
static inline void sim_sleep(int min_us, int max_us, int has_young_children) {
    if (max_us <= 0) return;
    int duration = min_us + (rand() % (max_us - min_us + 1));
    if (has_young_children) {
        duration = (int)(duration * 1.5);
    }
    if (duration > 0) {
        usleep(duration);
    }
}

// pobranie aktualnego czasu jako string
static inline void get_timestamp(char *buffer, size_t size) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    strftime(buffer, size, "%H:%M:%S", t);
}

// ustalenie atrakcji dla danego kroku trasy
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

// helpery do kierunkow mostu i promu
static inline int get_bridge_direction(int route) {
    return (route == 1) ? DIR_KA : DIR_AK;
}

static inline int get_ferry_direction(int route) {
    return (route == 1) ? 0 : 1;
}

// dodanie turysty do listy odwiedzajacych wieze
static inline int tower_add_visitor(struct ParkSharedMemory *park, pid_t pid) {
    for (int i = 0; i < X2_TOWER_CAP; i++) {
        if (park->tower_visitors[i] == 0) {
            park->tower_visitors[i] = pid;
            return i;
        }
    }
    return -1; 

}

// usuniecie turysty z listy odwiedzajacych wieze
static inline void tower_remove_visitor(struct ParkSharedMemory *park, pid_t pid) {
    for (int i = 0; i < X2_TOWER_CAP; i++) {
        if (park->tower_visitors[i] == pid) {
            park->tower_visitors[i] = 0;
            return;
        }
    }
}

// sprawdzenie czy turysta jest na wiezy
static inline int tower_has_visitor(struct ParkSharedMemory *park, pid_t pid) {
    for (int i = 0; i < X2_TOWER_CAP; i++) {
        if (park->tower_visitors[i] == pid) {
            return 1;
        }
    }
    return 0;
}

#endif