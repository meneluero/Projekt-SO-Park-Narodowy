#include "common.h"
#include <signal.h>

volatile sig_atomic_t tower_evacuation_flag = 0;
volatile sig_atomic_t emergency_exit_flag = 0;
volatile sig_atomic_t sigterm_flag = 0;

int g_sem_id = -1;
int g_id = -1;
struct ParkSharedMemory *g_park = NULL;

int g_is_caretaker = 0;
int g_caretaker_child_age = -1;
int g_my_caretaker_id = -1;
int g_has_guide_caretaker = 0;

int g_member_index = -1;
int g_has_queue_slot = 0;

static int int_to_str(int val, char *buf, int buf_size) {
    if (buf_size < 2) return 0;
    int neg = 0, len = 0;
    char tmp[12];
    if (val < 0) { neg = 1; val = -val; }
    do { tmp[len++] = '0' + (val % 10); val /= 10; } while (val > 0 && len < 11);
    int pos = 0;
    if (neg && pos < buf_size - 1) buf[pos++] = '-';
    for (int i = len - 1; i >= 0 && pos < buf_size - 1; i--) buf[pos++] = tmp[i];
    buf[pos] = '\0';
    return pos;
}

static void enter_park_and_report(int id, int age, int is_vip, int sem_id, int msg_id, struct ParkSharedMemory *park) {
    sem_lock(sem_id, SEM_PARK_LIMIT);

    sem_lock(sem_id, SEM_STATS_MUTEX);
    park->people_in_park++;
    if (is_vip) {
        park->vip_in_park++;
        park->free_entries_vip++;
    } else if (age < 7) {
        park->free_entries_children++;
    } else {
        park->paid_entries++;
        park->total_revenue += TICKET_PRICE;
    }
    sem_unlock(sem_id, SEM_STATS_MUTEX);

    struct msg_buffer entry_msg;
    entry_msg.msg_type = MSG_TYPE_ENTRY;
    entry_msg.tourist_id = id;
    entry_msg.age = age;
    entry_msg.is_vip = is_vip;
    strcpy(entry_msg.info, "wejście do parku");

    if (msgsnd(msg_id, &entry_msg, sizeof(entry_msg) - sizeof(long), 0) == -1) {
        fatal_error("[TURYSTA] Błąd msgsnd (wejście)");
    }
}

void sigterm_handler(int sig) {
    (void)sig;
    sigterm_flag = 1;
    emergency_exit_flag = 1;
    if (signal(SIGTERM, SIG_DFL) == SIG_ERR) {
        report_error("[TURYSTA] Błąd signal(SIGTERM)");
    }
    char msg[128];
    int pos = 0;
    const char p1[] = "\n\033[1;31m[TURYSTA ";
    const char p2[] = "] SIGTERM: Kończę pracę.\033[0m\n";
    for (int i = 0; p1[i]; i++) msg[pos++] = p1[i];
    pos += int_to_str(g_id, msg + pos, sizeof(msg) - pos);
    for (int i = 0; p2[i]; i++) msg[pos++] = p2[i];
    if (write(STDOUT_FILENO, msg, pos) == -1) {
        report_error("[TURYSTA] Błąd write w handlerze SIGTERM");
    }
}

void sigusr1_handler(int sig) {
    (void)sig;
    tower_evacuation_flag = 1;
    char msg[128];
    int pos = 0;
    const char p1[] = "\n\033[1;31m[TURYSTA ";
    const char p2[] = "] SIGUSR1: Ewakuacja z wieży!\033[0m\n";
    for (int i = 0; p1[i]; i++) msg[pos++] = p1[i];
    pos += int_to_str(g_id, msg + pos, sizeof(msg) - pos);
    for (int i = 0; p2[i]; i++) msg[pos++] = p2[i];
    if (write(STDOUT_FILENO, msg, pos) == -1) {
        report_error("[TURYSTA] Błąd write w handlerze SIGUSR1");
    }
}

void sigusr2_handler(int sig) {
    (void)sig;
    emergency_exit_flag = 1;
    char msg[128];
    int pos = 0;
    const char p1[] = "\n\033[1;31m[TURYSTA ";
    const char p2[] = "] SIGUSR2: Alarm! Natychmiastowy powrót do kasy!\033[0m\n";
    for (int i = 0; p1[i]; i++) msg[pos++] = p1[i];
    pos += int_to_str(g_id, msg + pos, sizeof(msg) - pos);
    for (int i = 0; p2[i]; i++) msg[pos++] = p2[i];
    if (write(STDOUT_FILENO, msg, pos) == -1) {
        report_error("[TURYSTA] Błąd write w handlerze SIGUSR2");
    }
}

static int tower_acquire_slot(int sem_id, struct ParkSharedMemory *park, int is_vip) {
    while (1) {
        if (emergency_exit_flag) {
            return -1;
        }

        sem_lock(sem_id, SEM_WIEZA_MUTEX);
        int can_enter = (park->tower_current_count < X2_TOWER_CAP);
        if (!is_vip) {
            can_enter = can_enter && (park->tower_waiting_vip == 0);
        }
        if (can_enter) {
            park->tower_current_count++;
            sem_unlock(sem_id, SEM_WIEZA_MUTEX);
            return 0;
        }

        if (is_vip) {
            park->tower_waiting_vip++;
            sem_unlock(sem_id, SEM_WIEZA_MUTEX);
            if (sem_lock_interruptible(sem_id, SEM_TOWER_VIP_WAIT, &emergency_exit_flag) == -1) {
                sem_lock(sem_id, SEM_WIEZA_MUTEX);
                if (park->tower_waiting_vip > 0) {
                    park->tower_waiting_vip--;
                }
                sem_unlock(sem_id, SEM_WIEZA_MUTEX);
                return -1;
            }
            return 0; 
        } else {
            park->tower_waiting_normal++;
            sem_unlock(sem_id, SEM_WIEZA_MUTEX);
            if (sem_lock_interruptible(sem_id, SEM_TOWER_NORMAL_WAIT, &emergency_exit_flag) == -1) {
                sem_lock(sem_id, SEM_WIEZA_MUTEX);
                if (park->tower_waiting_normal > 0) {
                    park->tower_waiting_normal--;
                }
                sem_unlock(sem_id, SEM_WIEZA_MUTEX);
                return -1;
            }
            return 0;
        }
    }
}

static void tower_release_slot(int sem_id, struct ParkSharedMemory *park) {
    sem_lock(sem_id, SEM_WIEZA_MUTEX);
    if (park->tower_current_count > 0) {
        park->tower_current_count--;
    }
    if (park->tower_waiting_vip > 0) {
        park->tower_waiting_vip--;
        park->tower_current_count++;
        sem_unlock(sem_id, SEM_WIEZA_MUTEX);
        sem_unlock(sem_id, SEM_TOWER_VIP_WAIT);
        return;
    }
    if (park->tower_waiting_normal > 0) {
        park->tower_waiting_normal--;
        park->tower_current_count++;
        sem_unlock(sem_id, SEM_WIEZA_MUTEX);
        sem_unlock(sem_id, SEM_TOWER_NORMAL_WAIT);
        return;
    }
    sem_unlock(sem_id, SEM_WIEZA_MUTEX);
}

void do_bridge(int id, int age, int is_vip, int direction, int group_id, struct ParkSharedMemory *park, int sem_id) {
    int other_dir = 1 - direction;
    int entered_bridge = 0;  

    printf(CLR_CYAN "[TURYSTA %d] Podchodzę do mostu (kierunek: %s)" CLR_RESET "\n", id, direction == DIR_KA ? "K->A" : "A->K");

    if (emergency_exit_flag) {
        printf(CLR_RED "[TURYSTA %d] Ewakuacja przed mostem! Pomijam." CLR_RESET "\n", id);
        return;
    }

    if (group_id >= 0) {
        if (sem_lock_interruptible(sem_id, SEM_BRIDGE_GUIDE_READY(group_id), &emergency_exit_flag) == -1) {
            printf(CLR_RED "[TURYSTA %d] Ewakuacja przed mostem - nie czekam na przewodnika." CLR_RESET "\n", id);
            return;
        }
    }

    if (age < 15) {
        if (g_my_caretaker_id >= 0) {
            printf(CLR_YELLOW "[TURYSTA %d] Mam %d lat - idę przez most pod opieką turysty %d" CLR_RESET "\n", id, age, g_my_caretaker_id);
        } else if (g_has_guide_caretaker) {
            printf(CLR_YELLOW "[TURYSTA %d] Mam %d lat - idę przez most pod opieką przewodnika" CLR_RESET "\n", id, age);
        } else {
            printf(CLR_YELLOW "[TURYSTA %d] Mam %d lat - idę przez most pod opieką dorosłego" CLR_RESET "\n", id, age);
        }
    }

    if (age < 15 && g_my_caretaker_id < 0 && !g_has_guide_caretaker) {
        printf(CLR_RED "[TURYSTA %d] Brak opiekuna - nie wchodzę na most." CLR_RESET "\n", id);
        return;
    }

    sem_lock(sem_id, SEM_MOST_MUTEX);

    if (park->bridge_direction == DIR_NONE || park->bridge_direction == direction) {

        park->bridge_direction = direction;
        park->bridge_on_bridge++;
        sem_unlock(sem_id, SEM_MOST_MUTEX);

        if (sem_lock_interruptible(sem_id, SEM_MOST_LIMIT, &emergency_exit_flag) == -1) {

            sem_lock(sem_id, SEM_MOST_MUTEX);
            park->bridge_on_bridge--;
            printf(CLR_RED "[TURYSTA %d] Ewakuacja podczas czekania na miejsce na moście (pozostało: %d)" CLR_RESET "\n",
                   id, park->bridge_on_bridge);

            if (park->bridge_on_bridge == 0) {
                park->bridge_direction = DIR_NONE;
            }
            sem_unlock(sem_id, SEM_MOST_MUTEX);
            return;  

        }

        entered_bridge = 1;  

        printf(CLR_CYAN "[TURYSTA %d] Wchodzę na most (%d osób na moście)" CLR_RESET "\n", id, park->bridge_on_bridge);

    } else {

        park->bridge_waiting[direction]++;
        printf(CLR_CYAN "[TURYSTA %d] Most zajęty w przeciwnym kierunku. Czekam..." CLR_RESET "\n", id);
        sem_unlock(sem_id, SEM_MOST_MUTEX);

        if (sem_lock_interruptible(sem_id, SEM_BRIDGE_WAIT(direction), &emergency_exit_flag) == -1) {
            sem_lock(sem_id, SEM_MOST_MUTEX);
            if (park->bridge_waiting[direction] > 0) {
                park->bridge_waiting[direction]--;
            }
            sem_unlock(sem_id, SEM_MOST_MUTEX);
            printf(CLR_RED "[TURYSTA %d] Ewakuacja podczas czekania na zmianę kierunku mostu." CLR_RESET "\n", id);
            return;
        }

        sem_lock(sem_id, SEM_MOST_MUTEX);
        park->bridge_on_bridge++;
        int count = park->bridge_on_bridge;
        sem_unlock(sem_id, SEM_MOST_MUTEX);

        if (sem_lock_interruptible(sem_id, SEM_MOST_LIMIT, &emergency_exit_flag) == -1) {
            sem_lock(sem_id, SEM_MOST_MUTEX);
            park->bridge_on_bridge--;
            printf(CLR_RED "[TURYSTA %d] Ewakuacja podczas czekania na miejsce na moście (po obudzeniu, pozostało: %d)" CLR_RESET "\n", id, park->bridge_on_bridge);

            if (park->bridge_on_bridge == 0) {
                if (park->bridge_waiting[other_dir] > 0) {
                    park->bridge_direction = other_dir;
                    int to_wake = park->bridge_waiting[other_dir];
                    park->bridge_waiting[other_dir] = 0;
                    sem_unlock(sem_id, SEM_MOST_MUTEX);
                    for (int i = 0; i < to_wake; i++) {
                        sem_unlock(sem_id, SEM_BRIDGE_WAIT(other_dir));
                    }
                } else {
                    park->bridge_direction = DIR_NONE;
                    sem_unlock(sem_id, SEM_MOST_MUTEX);
                }
            } else {
                sem_unlock(sem_id, SEM_MOST_MUTEX);
            }
            return;
        }

        entered_bridge = 1;

        printf(CLR_CYAN "[TURYSTA %d] Obudzony! Wchodzę na most (%d osób)" CLR_RESET "\n", id, count);
    }

    if (entered_bridge) {
        if (!emergency_exit_flag) {
            printf(CLR_CYAN "[TURYSTA %d] Przechodzę przez most..." CLR_RESET "\n", id);
            sim_sleep(BRIDGE_CROSS_TIME_MIN, BRIDGE_CROSS_TIME_MAX, 0);
        } else {
            printf(CLR_RED "[TURYSTA %d] Ewakuacja! Szybko schodzę z mostu." CLR_RESET "\n", id);
        }

        sem_unlock(sem_id, SEM_MOST_LIMIT);

        sem_lock(sem_id, SEM_MOST_MUTEX);
        park->bridge_on_bridge--;

        printf(CLR_CYAN "[TURYSTA %d] Zszedłem z mostu (pozostało: %d)" CLR_RESET "\n", id, park->bridge_on_bridge);

        if (park->bridge_on_bridge == 0) {
            if (park->bridge_waiting[other_dir] > 0) {
        
                park->bridge_direction = other_dir;
                int to_wake = park->bridge_waiting[other_dir];
                park->bridge_waiting[other_dir] = 0;
        
                printf(CLR_CYAN "[TURYSTA %d] Zmieniam kierunek mostu, budzę %d czekających" CLR_RESET "\n", id, to_wake);
        
                for (int i = 0; i < to_wake; i++) {
                    sem_unlock(sem_id, SEM_BRIDGE_WAIT(other_dir));
                }
                
                sem_unlock(sem_id, SEM_MOST_MUTEX); 
                
            } else {
                park->bridge_direction = DIR_NONE;
                sem_unlock(sem_id, SEM_MOST_MUTEX);
            }
        } else {
            sem_unlock(sem_id, SEM_MOST_MUTEX);
        }
    }
}

void do_tower(int id, int age, int is_vip, struct ParkSharedMemory *park, int sem_id) {
    if (age <= 5) {
        printf(CLR_YELLOW "[TURYSTA %d] Mam %d lat - nie mogę wejść na wieżę. Czekam na dole." CLR_RESET "\n", id, age);
        return;
    }

    if (g_is_caretaker && g_caretaker_child_age <= 5) {
        printf(CLR_YELLOW "[TURYSTA %d] Jestem opiekunem dziecka (wiek %d) - czekam na dole wieży." CLR_RESET "\n", id, g_caretaker_child_age);
        return;
    }

    printf(CLR_MAGENTA "[TURYSTA %d] Podchodzę do wieży widokowej" CLR_RESET "\n", id);

    if (age < 15) {
        if (g_my_caretaker_id >= 0) {
            printf(CLR_YELLOW "[TURYSTA %d] Mam %d lat - wchodzę na wieżę pod opieką turysty %d" CLR_RESET "\n", id, age, g_my_caretaker_id);
        } else if (g_has_guide_caretaker) {
            printf(CLR_YELLOW "[TURYSTA %d] Mam %d lat - opiekunem jest przewodnik, który nie wchodzi na wieżę." CLR_RESET "\n", id, age);
        } else {
            printf(CLR_YELLOW "[TURYSTA %d] Mam %d lat - potrzebuję dorosłego opiekuna na wieżę." CLR_RESET "\n", id, age);
        }
    }

    if (age < 15 && g_my_caretaker_id < 0) {
        printf(CLR_RED "[TURYSTA %d] Brak dorosłego opiekuna na wieżę - nie wchodzę." CLR_RESET "\n", id);
        return;
    }

    if (emergency_exit_flag) {
        printf(CLR_RED "[TURYSTA %d] Ewakuacja! Nie wchodzę na wieżę." CLR_RESET "\n", id);
        return;
    }

    if (is_vip) {
        printf(CLR_MAGENTA "[TURYSTA %d] Jestem VIPem - omijam kolejkę do wieży" CLR_RESET "\n", id);
    } else {
        printf(CLR_MAGENTA "[TURYSTA %d] Czekam na wejście na wieżę..." CLR_RESET "\n", id);
    }

    if (tower_acquire_slot(sem_id, park, is_vip) == -1) {
        printf(CLR_RED "[TURYSTA %d] Ewakuacja przed wejściem na wieżę!" CLR_RESET "\n", id);
        return;
    }

    if (emergency_exit_flag) {
        printf(CLR_RED "[TURYSTA %d] Ewakuacja przed schodami - rezygnuję z wejścia na wieżę." CLR_RESET "\n", id);
        tower_release_slot(sem_id, park);
        return;
    }

    if (sem_lock_interruptible(sem_id, SEM_TOWER_STAIRS_UP, &emergency_exit_flag) == -1) {
        printf(CLR_RED "[TURYSTA %d] Ewakuacja podczas wejścia po schodach." CLR_RESET "\n", id);
        tower_release_slot(sem_id, park);
        return;
    }

    printf(CLR_MAGENTA "[TURYSTA %d] Wchodzę po schodach w górę." CLR_RESET "\n", id);
    sem_unlock(sem_id, SEM_TOWER_STAIRS_UP);

    sem_lock(sem_id, SEM_WIEZA_MUTEX);
    tower_add_visitor(park, getpid());
    int count = park->tower_current_count;
    sem_unlock(sem_id, SEM_WIEZA_MUTEX);

    printf(CLR_MAGENTA "[TURYSTA %d] Wchodzę na wieżę (%d/%d osób)" CLR_RESET "\n", id, count, X2_TOWER_CAP);

    printf(CLR_MAGENTA "[TURYSTA %d] Podziwiam widoki z wieży..." CLR_RESET "\n", id);

    tower_evacuation_flag = 0;

    int tower_stay_us = TOWER_VISIT_TIME_MIN;
    if (TOWER_VISIT_TIME_MAX > TOWER_VISIT_TIME_MIN) {
        tower_stay_us = TOWER_VISIT_TIME_MIN + (rand() % (TOWER_VISIT_TIME_MAX - TOWER_VISIT_TIME_MIN + 1));
    }
    int tower_stay_sec = (tower_stay_us > 0) ? (tower_stay_us + 999999) / 1000000 : 0;

    int tower_result;
    if (tower_stay_sec > 0) {
        tower_result = sem_timed_wait(sem_id, SEM_TOWER_WAIT, tower_stay_sec, &tower_evacuation_flag, &emergency_exit_flag);
    } else {
        tower_result = (tower_evacuation_flag || emergency_exit_flag) ? -1 : 1;
    }

    if (tower_result == -1) {
        if (tower_evacuation_flag) {
            printf(CLR_RED "[TURYSTA %d] SIGUSR1! Natychmiast schodzę z wieży!" CLR_RESET "\n", id);
        } else {
            printf(CLR_RED "[TURYSTA %d] Ewakuacja ogólna! Schodzę z wieży!" CLR_RESET "\n", id);
        }
    }

    sem_lock(sem_id, SEM_TOWER_STAIRS_DOWN);
    printf(CLR_MAGENTA "[TURYSTA %d] Schodzę po schodach w dół." CLR_RESET "\n", id);
    sem_unlock(sem_id, SEM_TOWER_STAIRS_DOWN);

    sem_lock(sem_id, SEM_WIEZA_MUTEX);
    tower_remove_visitor(park, getpid());
    sem_unlock(sem_id, SEM_WIEZA_MUTEX);

    tower_release_slot(sem_id, park);

    printf(CLR_MAGENTA "[TURYSTA %d] Zszedłem z wieży" CLR_RESET "\n", id);
}

void do_ferry(int id, int my_group_id, int age, int is_vip, struct ParkSharedMemory *park, int sem_id) {

    printf(CLR_CYAN "[TURYSTA %d] Podchodzę do promu" CLR_RESET "\n", id);

    if (age < 15) {
        if (g_my_caretaker_id >= 0) {
            printf(CLR_YELLOW "[TURYSTA %d] Mam %d lat - wsiadam na prom pod opieką turysty %d" CLR_RESET "\n", id, age, g_my_caretaker_id);
        } else if (g_has_guide_caretaker) {
            printf(CLR_YELLOW "[TURYSTA %d] Mam %d lat - wsiadam na prom pod opieką przewodnika" CLR_RESET "\n", id, age);
        } else {
            printf(CLR_YELLOW "[TURYSTA %d] Mam %d lat - wsiadam na prom pod opieką dorosłego" CLR_RESET "\n", id, age);
        }
    }

    if (age < 15 && g_my_caretaker_id < 0 && !g_has_guide_caretaker) {
        printf(CLR_RED "[TURYSTA %d] Brak opiekuna - nie wchodzę na prom." CLR_RESET "\n", id);
        return;
    }

    if (is_vip) {
        printf(CLR_MAGENTA "[TURYSTA %d] Jestem VIPem przy promie" CLR_RESET "\n", id);
    }

    if (emergency_exit_flag) {
        printf(CLR_RED "[TURYSTA %d] Ewakuacja przed promem! Pomijam." CLR_RESET "\n", id);
        return;
    }

    if (my_group_id >= 0) {
        if (sem_lock_interruptible(sem_id, SEM_FERRY_GUIDE_READY(my_group_id), &emergency_exit_flag) == -1) {
            printf(CLR_RED "[TURYSTA %d] Ewakuacja przed promem - nie czekam na przewodnika." CLR_RESET "\n", id);
            return;
        }
    }

    int direction = 0;
    if (my_group_id >= 0) {
        direction = get_ferry_direction(park->groups[my_group_id].route);
    }

    if (is_vip) {
        printf(CLR_MAGENTA "[TURYSTA %d] VIP: omijam kolejkę na prom." CLR_RESET "\n", id);
    } else {
        printf(CLR_CYAN "[TURYSTA %d] Czekam na możliwość wejścia na prom..." CLR_RESET "\n", id);
    }

    if (ferry_enter(park, sem_id, direction, is_vip, &emergency_exit_flag) == -1) {
        printf(CLR_RED "[TURYSTA %d] Ewakuacja podczas oczekiwania na prom." CLR_RESET "\n", id);
        return;
    }

    printf(CLR_CYAN "[TURYSTA %d] Wsiadłem na prom (kierunek %d)." CLR_RESET "\n", id, direction);
    sim_sleep(FERRY_TRAVEL_TIME_MIN, FERRY_TRAVEL_TIME_MAX, 0);
    ferry_leave(park, sem_id, direction);
    printf(CLR_CYAN "[TURYSTA %d] Wysiadłem z promu." CLR_RESET "\n", id);
}

void do_ferry_vip(int id, int age, int route, struct ParkSharedMemory *park, int sem_id) {
    (void)age;

    printf(CLR_MAGENTA "[TURYSTA %d] VIP: podchodzę do promu i omijam kolejkę" CLR_RESET "\n", id);

    int direction = get_ferry_direction(route);
    if (ferry_enter(park, sem_id, direction, 1, &emergency_exit_flag) == -1) {
        printf(CLR_RED "[TURYSTA %d] Ewakuacja przed wejściem na prom VIP." CLR_RESET "\n", id);
        return;
    }

    printf(CLR_MAGENTA "[TURYSTA %d] VIP: wsiadam na prom (kierunek %d)." CLR_RESET "\n", id, direction);
    sim_sleep(FERRY_TRAVEL_TIME_MIN, FERRY_TRAVEL_TIME_MAX, 0);
    ferry_leave(park, sem_id, direction);

    printf(CLR_MAGENTA "[TURYSTA %d] VIP: dotarłem promem na drugi brzeg." CLR_RESET "\n", id);
}

int main(int argc, char* argv[]) {

    if (argc < 2) {
        printf(CLR_RED "[TURYSTA] Błąd: Brak ID turysty! Uruchamiaj przez main." CLR_RESET "\n");
        exit(1);
    }

    int id = atoi(argv[1]);
    g_id = id; 

    struct sigaction sa1;
    sa1.sa_handler = sigusr1_handler;
    if (sigemptyset(&sa1.sa_mask) == -1) {
        fatal_error("[TURYSTA] Błąd sigemptyset(SIGUSR1)");
    }
    sa1.sa_flags = 0; 

    if (sigaction(SIGUSR1, &sa1, NULL) == -1) {
        fatal_error("[TURYSTA] Błąd sigaction SIGUSR1");
    }

    struct sigaction sa2;
    sa2.sa_handler = sigusr2_handler;
    if (sigemptyset(&sa2.sa_mask) == -1) {
        fatal_error("[TURYSTA] Błąd sigemptyset(SIGUSR2)");
    }
    sa2.sa_flags = 0;
    if (sigaction(SIGUSR2, &sa2, NULL) == -1) {
        fatal_error("[TURYSTA] Błąd sigaction SIGUSR2");
    }

    struct sigaction sa_term;
    sa_term.sa_handler = sigterm_handler;
    if (sigemptyset(&sa_term.sa_mask) == -1) {
        fatal_error("[TURYSTA] Błąd sigemptyset(SIGTERM)");
    }
    sa_term.sa_flags = 0;
    if (sigaction(SIGTERM, &sa_term, NULL) == -1) {
        fatal_error("[TURYSTA] Błąd sigaction SIGTERM");
    }

    srand(time(NULL) + id);

    int age = (rand() % 68) + 3; 

    int is_vip = (rand() % 100) < 5; 
    int vip_can_go_solo = (is_vip && age >= 15);
    int should_send_exit_notice = 1;
    int entry_msg_sent = 0;

    int shm_id = shmget(ftok(FTOK_PATH, FTOK_SHM_ID), sizeof(struct ParkSharedMemory), 0600);
    int sem_id = semget(ftok(FTOK_PATH, FTOK_SEM_ID), TOTAL_SEMAPHORES, 0600);
    int msg_id = msgget(ftok(FTOK_PATH, FTOK_MSG_ID), 0600);
    int report_msg_id = msgget(ftok(FTOK_PATH, FTOK_MSG_REPORT_ID), 0600);

    if (shm_id == -1 || sem_id == -1 || msg_id == -1 || report_msg_id == -1) {
        fatal_error("[TURYSTA] Nie mogę znaleźć zasobów IPC");
    }

    struct ParkSharedMemory *park = (struct ParkSharedMemory*)shmat(shm_id, NULL, 0);
    if (park == (void*)-1) {
        fatal_error("[TURYSTA] Błąd shmat");
    }

    g_sem_id = sem_id;
    g_park = park;

    if (is_vip) {
        printf(CLR_MAGENTA "[TURYSTA %d] Jestem VIPem (wiek: %d). Mam legitymację PTTK!" CLR_RESET "\n", id, age);
    } else if (age < 7) {
        printf(CLR_YELLOW "[TURYSTA %d] Jestem dzieckiem (wiek: %d). Wchodzę za darmo!" CLR_RESET "\n", id, age);
    } else {
        printf(CLR_CYAN "[TURYSTA %d] Przychodzę do parku (wiek: %d)." CLR_RESET "\n", id, age);
    }

    if (vip_can_go_solo) {
        printf(CLR_MAGENTA "[TURYSTA %d] VIP: wejście bezpłatne, pomijam kasę." CLR_RESET "\n", id);
        enter_park_and_report(id, age, is_vip, sem_id, msg_id, park);
        printf(CLR_GREEN "[TURYSTA %d] Wszedłem do parku! Idę do punktu zbiórki." CLR_RESET "\n", id);
        printf(CLR_MAGENTA "[TURYSTA %d] VIP: omijam kolejkę do kasy i zwiedzam samodzielnie." CLR_RESET "\n", id);

        int route = (rand() % 2) + 1;
        printf(CLR_MAGENTA "[TURYSTA %d] VIP: startuję trasę %d solo." CLR_RESET "\n", id, route);

        for (int step = 0; step < 3; step++) {
            int attraction = get_attraction_for_step(route, step);
            if (!emergency_exit_flag) {
                printf(CLR_MAGENTA "[TURYSTA %d] VIP: idę do atrakcji %d" CLR_RESET "\n", id, attraction);
                switch (attraction) {
                    case ATTR_BRIDGE:
                        do_bridge(id, age, is_vip, get_bridge_direction(route), -1, park, sem_id);
                        break;
                    case ATTR_TOWER:
                        do_tower(id, age, is_vip, park, sem_id);
                        break;
                    case ATTR_FERRY:
                        do_ferry_vip(id, age, route, park, sem_id);
                        break;
                }
            } else {
                printf(CLR_RED "[TURYSTA %d] VIP: ewakuacja! Pomijam atrakcję %d." CLR_RESET "\n", id, attraction);
            }
        }

        printf(CLR_MAGENTA "[TURYSTA %d] VIP: koniec wycieczki. Wracam do kasy." CLR_RESET "\n", id);
        goto cleanup;
    }

    if (is_vip && age < 15) {
        printf(CLR_MAGENTA "[TURYSTA %d] VIP-dziecko: omijam kasę, ale muszę iść z grupą (potrzebuję opiekuna)." CLR_RESET "\n", id);
        printf(CLR_MAGENTA "[TURYSTA %d] VIP-dziecko: wejście bezpłatne." CLR_RESET "\n", id);
    }

    if (!is_vip) {
        if (sem_lock_interruptible(sem_id, SEM_CASH_QUEUE_SLOTS, &emergency_exit_flag) == -1) {
            printf(CLR_RED "[TURYSTA %d] Ewakuacja przed kasą!" CLR_RESET "\n", id);
            goto cleanup;
        }

        sem_lock(sem_id, SEM_CASH_QUEUE_MUTEX);
        park->cash_queue_count++;
        sem_unlock(sem_id, SEM_CASH_QUEUE_MUTEX);

        printf(CLR_CYAN "[TURYSTA %d] Ustawiam się w kolejce do kasy." CLR_RESET "\n", id);

        sem_lock(sem_id, SEM_CASH_QUEUE_MUTEX);
        if (park->cash_queue_count > 0) {
            park->cash_queue_count--;
        }
        sem_unlock(sem_id, SEM_CASH_QUEUE_MUTEX);

        sem_unlock(sem_id, SEM_CASH_QUEUE_SLOTS);

        if (age < 7) {
            printf(CLR_YELLOW "[TURYSTA %d] Dziecko <7 - bilet bezpłatny." CLR_RESET "\n", id);
        } else {
            printf(CLR_CYAN "[TURYSTA %d] Płacę za bilet: %d PLN." CLR_RESET "\n", id, TICKET_PRICE);
        }
    }

    if (sem_lock_interruptible(sem_id, SEM_QUEUE_SLOTS, &emergency_exit_flag) == -1) {
        printf(CLR_RED "[TURYSTA %d] Ewakuacja przed wejściem do kolejki!" CLR_RESET "\n", id);
        goto cleanup;
    }

    g_has_queue_slot = 1;

    sem_lock(sem_id, SEM_QUEUE_MUTEX);

    int my_position = park->queue_tail;
    park->queue_ages[my_position] = age;
    park->queue_vips[my_position] = is_vip;
    park->queue_pids[my_position] = getpid();
    park->queue_ids[my_position] = id;
    park->queue_tail = (park->queue_tail + 1) % N_PARK_CAPACITY;
    park->people_in_queue++;

    int current_count = park->people_in_queue;

    sem_unlock(sem_id, SEM_QUEUE_MUTEX);

    if (is_vip && age < 15) {
        printf(CLR_MAGENTA "[TURYSTA %d] VIP-dziecko: czekam na przydzielenie do grupy." CLR_RESET "\n", id);
    } else {
        printf(CLR_CYAN "[TURYSTA %d] Jestem w kolejce grupowej." CLR_RESET "\n", id);
    }

    if (!entry_msg_sent) {
        enter_park_and_report(id, age, is_vip, sem_id, msg_id, park);
        entry_msg_sent = 1;
        printf(CLR_GREEN "[TURYSTA %d] Wszedłem do parku! Idę do punktu zbiórki." CLR_RESET "\n", id);
    }

    printf(CLR_CYAN "[TURYSTA %d] Czekam na przewodnika. (Kolejka: %d/%d)" CLR_RESET "\n", id, current_count, M_GROUP_SIZE);

    if (current_count >= M_GROUP_SIZE) {
        printf(CLR_CYAN "[TURYSTA %d] Kolejka ma %d osób - budzę przewodnika!" CLR_RESET "\n", id, current_count);
        if (sem_getval(sem_id, SEM_PRZEWODNIK) == 0) {
            sem_unlock(sem_id, SEM_PRZEWODNIK);
        }
    }

    printf(CLR_CYAN "[TURYSTA %d] Czekam na przydzielenie do grupy (pozycja %d)..." CLR_RESET "\n", id, my_position);

    sem_lock(sem_id, SEM_TOURIST_ASSIGNED(my_position));

    sem_lock(sem_id, SEM_QUEUE_MUTEX);
    int my_group_id = park->assigned_group_id[my_position];
    g_member_index = park->assigned_member_index[my_position];
    sem_unlock(sem_id, SEM_QUEUE_MUTEX);

    g_has_queue_slot = 0;

    sem_unlock(sem_id, SEM_TOURIST_READ_DONE(my_position));

    printf(CLR_CYAN "[TURYSTA %d] Potwierdzam odczyt, przydzielony do grupy %d." CLR_RESET "\n", id, my_group_id);

    if (my_group_id < 0 || my_group_id >= MAX_GROUPS) {
        printf(CLR_RED "[TURYSTA %d] Błąd: nieprawidłowy group_id=%d!" CLR_RESET "\n", id, my_group_id);
        goto cleanup;
    }
    should_send_exit_notice = 0;

    if (emergency_exit_flag) {
        printf(CLR_RED "[TURYSTA %d] Ewakuacja - ale muszę dokończyć protokół grupy!" CLR_RESET "\n", id);
    }

    printf(CLR_CYAN "[TURYSTA %d] Czekam na start wycieczki..." CLR_RESET "\n", id);

    struct GroupState *my_group = &park->groups[my_group_id];
    for (int i = 0; i < M_GROUP_SIZE; i++) {
        if (my_group->member_pids[i] == getpid()) {
            g_is_caretaker = my_group->member_is_caretaker[i];

            if (g_is_caretaker) {
                int child_idx = my_group->member_caretaker_of[i];
                if (child_idx >= 0) {
                    g_caretaker_child_age = my_group->member_ages[child_idx];
                }
                if (g_caretaker_child_age <= 5) {
                    printf(CLR_YELLOW "[TURYSTA %d] Jestem opiekunem dziecka (wiek %d) - nie wejdę na wieżę" CLR_RESET "\n", id, g_caretaker_child_age);
                } else {
                    printf(CLR_YELLOW "[TURYSTA %d] Jestem opiekunem dziecka (wiek %d)" CLR_RESET "\n", id, g_caretaker_child_age);
                }
            }

            int caretaker_idx = my_group->member_has_caretaker[i];
            if (caretaker_idx >= 0) {
                g_my_caretaker_id = my_group->member_ids[caretaker_idx];
            } else if (my_group->member_caretaker_is_guide[i]) {
                g_has_guide_caretaker = 1;
                g_my_caretaker_id = -1;
            }
            break;
        }
    }

    sem_lock(sem_id, SEM_MEMBER_GO(my_group_id, g_member_index));

    int route = my_group->route;

    printf(CLR_CYAN "[TURYSTA %d] Wycieczka start! Trasa %d" CLR_RESET "\n", id, route);

    for (int step = 0; step < 3; step++) {

        int attraction = get_attraction_for_step(route, step);

        if (!emergency_exit_flag) {
            printf(CLR_CYAN "[TURYSTA %d] Faza %d: idę do atrakcji %d" CLR_RESET "\n", id, step + 1, attraction);
            switch (attraction) {
                case ATTR_BRIDGE:
                    do_bridge(id, age, is_vip, get_bridge_direction(route), my_group_id, park, sem_id);
                    break; 
                case ATTR_TOWER:
                    do_tower(id, age, is_vip, park, sem_id);
                    break; 
                case ATTR_FERRY:
                    do_ferry(id, my_group_id, age, is_vip, park, sem_id);
                    break;
            }
        } else {
            printf(CLR_RED "[TURYSTA %d] Ewakuacja! Pomijam atrakcję %d, ale zgłaszam obecność." CLR_RESET "\n", id, attraction);
        }

        sem_unlock(sem_id, SEM_GROUP_DONE(my_group_id));

        if (!emergency_exit_flag) {
            printf(CLR_CYAN "[TURYSTA %d] Zakończyłem atrakcje %d. Czekam na grupę." CLR_RESET "\n", id, attraction);
        }

        if (step < 2) {
            sem_lock(sem_id, SEM_MEMBER_GO(my_group_id, g_member_index));
        }
    }

    printf(CLR_CYAN "[TURYSTA %d] Koniec wycieczki. Wracam do kasy." CLR_RESET "\n", id);

cleanup:
    if (g_has_queue_slot) {
        sem_unlock(sem_id, SEM_QUEUE_SLOTS);
        g_has_queue_slot = 0;
        printf(CLR_CYAN "[TURYSTA %d] Zwolniłem slot w kolejce." CLR_RESET "\n", id);
    }

    printf(CLR_GREEN "[TURYSTA %d] Wychodzę z parku." CLR_RESET "\n", id);

    if (should_send_exit_notice) {
        struct msg_buffer notice_msg;
        notice_msg.msg_type = MSG_TYPE_EXIT_NOTICE;
        notice_msg.tourist_id = id;
        notice_msg.age = age;
        notice_msg.is_vip = is_vip;

        char timestamp[20];
        get_timestamp(timestamp, sizeof(timestamp));
        strcpy(notice_msg.info, timestamp);

        if (msgsnd(report_msg_id, &notice_msg, sizeof(notice_msg) - sizeof(long), 0) == -1) {
            report_error("[TURYSTA] Błąd msgsnd (notice wyjścia)");
        }
    }

    sem_lock(sem_id, SEM_STATS_MUTEX);
    if (park->people_in_park > 0) {
        park->people_in_park--;
    }
    if (is_vip && park->vip_in_park > 0) {
        park->vip_in_park--;
    }
    sem_unlock(sem_id, SEM_STATS_MUTEX);

    sem_unlock(sem_id, SEM_PARK_LIMIT);

    printf(CLR_CYAN "[TURYSTA %d] Do widzenia!" CLR_RESET "\n", id);

    if (shmdt(park) == -1) {
        report_error("[TURYSTA] Błąd shmdt");
    }

    return 0;
}
