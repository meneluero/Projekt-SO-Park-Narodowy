#include "common.h"
#include <signal.h>

// flagi do obslugi sygnalow
volatile sig_atomic_t tower_evacuation_flag = 0;
volatile sig_atomic_t emergency_exit_flag = 0;
volatile sig_atomic_t sigterm_flag = 0;

// zmienne globalne potrzebne w handlerach sygnalow
int g_sem_id = -1;
int g_id = -1;
pid_t g_pid = -1;
struct ParkSharedMemory *g_park = NULL;

// zmienne dotyczace opieki nad dziecmi
int g_is_caretaker = 0;
int g_caretaker_child_age = -1;
int g_caretaker_child_id = -1;
pid_t g_caretaker_child_pid = -1;
int g_my_caretaker_id = -1;
pid_t g_my_caretaker_pid = -1;
int g_has_guide_caretaker = 0;

int g_member_index = -1;
int g_has_queue_slot = 0;

// funkcja pomocnicza do bezpiecznej konwersji int na string (dla write w handlerze)
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

static const char *tourist_error_ctx(const char *msg) {
    static char buf[256];
    int written;
    if (g_id >= 0 && g_pid > 0) {
        written = snprintf(buf, sizeof(buf), "[T %d | PID %d] %s", g_id, (int)g_pid, msg);
    } else if (g_pid > 0) {
        written = snprintf(buf, sizeof(buf), "[T ? | PID %d] %s", (int)g_pid, msg);
    } else {
        written = snprintf(buf, sizeof(buf), "[T ? | PID ?] %s", msg);
    }
    if (written < 0) {
        // W przypadku błędu snprintf, zwróć prostą wiadomość
        return "[Turysta] Błąd formatowania komunikatu";
    }
    return buf;
}

// budzenie przewodników w zależności od wielkości kolejki
static void wake_guides_for_count(int sem_id, int current_count, int allow_partial) {
    if (current_count <= 0) {
        return;
    }

    int desired = allow_partial ? (current_count + M_GROUP_SIZE - 1) / M_GROUP_SIZE
                                : (current_count / M_GROUP_SIZE);
    if (desired <= 0) {
        return;
    }

    int current_val = sem_getval(sem_id, SEM_PRZEWODNIK);
    int to_post = desired - current_val;
    for (int i = 0; i < to_post; i++) {
        sem_unlock(sem_id, SEM_PRZEWODNIK);
    }
}

static void wake_guides_for_queue(int sem_id, struct ParkSharedMemory *park, int allow_partial) {
    sem_lock(sem_id, SEM_QUEUE_MUTEX);
    int in_queue = park->people_in_queue;
    sem_unlock(sem_id, SEM_QUEUE_MUTEX);

    wake_guides_for_count(sem_id, in_queue, allow_partial);
}

// ponowne sprawdzenie Tk tuz przed wejsciem (minimalna korekta)
static int reject_if_closed_late(int id, int sem_id, struct ParkSharedMemory *park, int release_queue_slot) {
    time_t check_time = time(NULL);
    if (check_time == (time_t)-1) {
        report_error(tourist_error_ctx("Błąd time (sprawdzenie zamknięcia parku)"));
        check_time = park->park_closing_time; // zaloz ze park zamkniety w przypadku bledu
    }

    if (park->park_closed || check_time >= park->park_closing_time) {
        printf(CLR_YELLOW "[T %d | PID %d] Park zamknięty (Tk). Odchodzę." CLR_RESET "\n", id, getpid());
        if (fflush(stdout) == EOF) {
            report_error(tourist_error_ctx("Błąd fflush (park zamknięty)"));
        }

        if (release_queue_slot) {
            sem_unlock(sem_id, SEM_QUEUE_SLOTS);
        }

        sem_lock(sem_id, SEM_STATS_MUTEX);
        if (park->daily_entered_count > 0) {
            park->daily_entered_count--;
        }
        park->rejected_after_close++;
        park->total_expected--;
        if (park->total_exited >= park->total_expected) {
            sem_unlock(sem_id, SEM_ALL_DONE);
        }
        sem_unlock(sem_id, SEM_STATS_MUTEX);

        if (shmdt(park) == -1) {
            report_error(tourist_error_ctx("Błąd shmdt"));
        }
        return 1;
    }
    return 0;
}

// procedura wejscia do parku, aktualizacji statystyk i wyslania komunikatu
static void enter_park_and_report(int id, int age, int is_vip, int sem_id, int msg_id, struct ParkSharedMemory *park) {
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

    // wyslanie wiadomosci do kasy
    struct msg_buffer entry_msg;
    entry_msg.msg_type = MSG_TYPE_ENTRY;
    entry_msg.tourist_id = id;
    pid_t pid = getpid();
    entry_msg.tourist_pid = pid;
    entry_msg.age = age;
    entry_msg.is_vip = is_vip;
    const char *info_msg = "wejście do parku";
    size_t len = strlen(info_msg);
    if (len >= sizeof(entry_msg.info)) {
        len = sizeof(entry_msg.info) - 1;
    }
    memcpy(entry_msg.info, info_msg, len);
    entry_msg.info[len] = '\0';

    if (msgsnd(msg_id, &entry_msg, sizeof(entry_msg) - sizeof(long), 0) == -1) {
        fatal_error(tourist_error_ctx("Błąd msgsnd (wejście)"));
    }
}

// obsluga sygnalu sigterm (zakonczenie programu)
void sigterm_handler(int sig) {
    (void)sig;
    sigterm_flag = 1;
    emergency_exit_flag = 1;
    if (signal(SIGTERM, SIG_DFL) == SIG_ERR) {
        report_error(tourist_error_ctx("Błąd signal(SIGTERM)"));
    }
    // bezpieczne wypisywanie komunikatu bez uzycia printf
    char msg[128];
    int pos = 0;
    const char p1[] = "\n\033[1;31m[T ";
    const char p_mid[] = " | PID ";
    const char p2[] = "] SIGTERM: Kończę pracę.\033[0m\n";
    for (int i = 0; p1[i]; i++) msg[pos++] = p1[i];
    pos += int_to_str(g_id, msg + pos, sizeof(msg) - pos);
    for (int i = 0; p_mid[i]; i++) msg[pos++] = p_mid[i];
    pos += int_to_str((int)g_pid, msg + pos, sizeof(msg) - pos);
    for (int i = 0; p2[i]; i++) msg[pos++] = p2[i];
    if (write(STDOUT_FILENO, msg, pos) == -1) {
        report_error(tourist_error_ctx("Błąd write w handlerze SIGTERM"));
    }
}

// obsluga sygnalu ewakuacji z wiezy
void sigusr1_handler(int sig) {
    (void)sig;
    tower_evacuation_flag = 1;
    char msg[128];
    int pos = 0;
    const char p1[] = "\n\033[1;31m[T ";
    const char p_mid[] = " | PID ";
    const char p2[] = "] SIGUSR1: Ewakuacja z wieży!\033[0m\n";
    for (int i = 0; p1[i]; i++) msg[pos++] = p1[i];
    pos += int_to_str(g_id, msg + pos, sizeof(msg) - pos);
    for (int i = 0; p_mid[i]; i++) msg[pos++] = p_mid[i];
    pos += int_to_str((int)g_pid, msg + pos, sizeof(msg) - pos);
    for (int i = 0; p2[i]; i++) msg[pos++] = p2[i];
    if (write(STDOUT_FILENO, msg, pos) == -1) {
        report_error(tourist_error_ctx("Błąd write w handlerze SIGUSR1"));
    }
}

// obsluga sygnalu ewakuacji ogolnej (powrot do kasy)
void sigusr2_handler(int sig) {
    (void)sig;
    emergency_exit_flag = 1;
    char msg[128];
    int pos = 0;
    const char p1[] = "\n\033[1;31m[T ";
    const char p_mid[] = " | PID ";
    const char p2[] = "] SIGUSR2: Alarm! Natychmiastowy powrót do kasy!\033[0m\n";
    for (int i = 0; p1[i]; i++) msg[pos++] = p1[i];
    pos += int_to_str(g_id, msg + pos, sizeof(msg) - pos);
    for (int i = 0; p_mid[i]; i++) msg[pos++] = p_mid[i];
    pos += int_to_str((int)g_pid, msg + pos, sizeof(msg) - pos);
    for (int i = 0; p2[i]; i++) msg[pos++] = p2[i];
    if (write(STDOUT_FILENO, msg, pos) == -1) {
        report_error(tourist_error_ctx("Błąd write w handlerze SIGUSR2"));
    }
}

// logika zajmowania miejsca na wiezy z uwzglednieniem priorytetu vip
static int tower_acquire_slot(int sem_id, struct ParkSharedMemory *park, int is_vip) {
    while (1) {
        if (emergency_exit_flag) {
            return -1;
        }

        sem_lock(sem_id, SEM_WIEZA_MUTEX);
        // sprawdzenie czy jest miejsce i czy nie ma oczekujacych vipow (dla zwyklych)
        int can_enter = (park->tower_current_count < X2_TOWER_CAP);
        if (!is_vip) {
            can_enter = can_enter && (park->tower_waiting_vip == 0);
        }
        if (can_enter) {
            park->tower_current_count++;
            sem_unlock(sem_id, SEM_WIEZA_MUTEX);
            return 0;
        }

        // jesli brak miejsc, czekamy na odpowiednim semaforze
        if (is_vip) {
            park->tower_waiting_vip++;
            sem_unlock(sem_id, SEM_WIEZA_MUTEX);
            if (sem_lock_interruptible(sem_id, SEM_TOWER_VIP_WAIT, &emergency_exit_flag) == -1) {
                // obsluga przerwania czekania
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
                // obsluga przerwania czekania
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

// zwalnianie miejsca na wiezy i budzenie oczekujacych
static void tower_release_slot(int sem_id, struct ParkSharedMemory *park) {
    sem_lock(sem_id, SEM_WIEZA_MUTEX);
    if (park->tower_current_count > 0) {
        park->tower_current_count--;
    }
    // priorytet dla vipow przy wpuszczaniu
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

// logika atrakcji: most
void do_bridge(int id, int age, int is_vip, int direction, int group_id, struct ParkSharedMemory *park, int sem_id) {
    int other_dir = 1 - direction;
    int entered_bridge = 0;  

    printf(CLR_CYAN "[T %d | PID %d] Podchodzę do mostu (kierunek: %s)" CLR_RESET "\n", id, getpid(), direction == DIR_KA ? "K->A" : "A->K");

    if (emergency_exit_flag) {
        printf(CLR_RED "[T %d | PID %d] Ewakuacja przed mostem! Pomijam." CLR_RESET "\n", id, getpid());
        return;
    }

    // czekanie na przewodnika jesli w grupie
    if (group_id >= 0) {
        if (sem_lock_interruptible(sem_id, SEM_BRIDGE_GUIDE_READY(group_id), &emergency_exit_flag) == -1) {
            printf(CLR_RED "[T %d | PID %d] Ewakuacja przed mostem - nie czekam na przewodnika." CLR_RESET "\n", id, getpid());
            return;
        }
    }

    // logowanie opieki nad dziecmi
    if (age < 15) {
        if (g_my_caretaker_id >= 0) {
            printf(CLR_YELLOW "[T %d | PID %d] Mam %d lat - idę przez most pod opieką [T %d | PID %d]" CLR_RESET "\n", id, getpid(), age, g_my_caretaker_id, g_my_caretaker_pid);
        } else if (g_has_guide_caretaker) {
            printf(CLR_YELLOW "[T %d | PID %d] Mam %d lat - idę przez most pod opieką przewodnika (PID %d)" CLR_RESET "\n", id, getpid(), age, g_my_caretaker_pid);
        } else {
            printf(CLR_YELLOW "[T %d | PID %d] Mam %d lat - idę przez most pod opieką dorosłego" CLR_RESET "\n", id, getpid(), age);
        }
    }

    // blokada dla dzieci bez opiekuna
    if (age < 15 && g_my_caretaker_id < 0 && !g_has_guide_caretaker) {
        printf(CLR_RED "[T %d | PID %d] Brak opiekuna - nie wchodzę na most." CLR_RESET "\n", id, getpid());
        return;
    }

    sem_lock(sem_id, SEM_MOST_MUTEX);

    // logika wejscia na most: jesli pusty lub ten sam kierunek
    if (park->bridge_direction == DIR_NONE || park->bridge_direction == direction) {

        park->bridge_direction = direction;
        park->bridge_on_bridge++;
        sem_unlock(sem_id, SEM_MOST_MUTEX);

        // czekanie na pojemnosc mostu
        if (sem_lock_interruptible(sem_id, SEM_MOST_LIMIT, &emergency_exit_flag) == -1) {

            sem_lock(sem_id, SEM_MOST_MUTEX);
            park->bridge_on_bridge--;
            printf(CLR_RED "[T %d | PID %d] Ewakuacja podczas czekania na miejsce na moście (pozostało: %d)" CLR_RESET "\n", id, getpid(), park->bridge_on_bridge);

            if (park->bridge_on_bridge == 0) {
                park->bridge_direction = DIR_NONE;
            }
            sem_unlock(sem_id, SEM_MOST_MUTEX);
            return;  

        }

        entered_bridge = 1;  

        printf(CLR_CYAN "[T %d | PID %d] Wchodzę na most (%d osób na moście)" CLR_RESET "\n", id, getpid(), park->bridge_on_bridge);

    } else {

        // jesli zajety w przeciwnym kierunku - czekamy
        park->bridge_waiting[direction]++;
        printf(CLR_CYAN "[T %d | PID %d] Most zajęty w przeciwnym kierunku. Czekam..." CLR_RESET "\n", id, getpid());
        sem_unlock(sem_id, SEM_MOST_MUTEX);

        if (sem_lock_interruptible(sem_id, SEM_BRIDGE_WAIT(direction), &emergency_exit_flag) == -1) {
            sem_lock(sem_id, SEM_MOST_MUTEX);
            if (park->bridge_waiting[direction] > 0) {
                park->bridge_waiting[direction]--;
            }
            sem_unlock(sem_id, SEM_MOST_MUTEX);
            printf(CLR_RED "[T %d | PID %d] Ewakuacja podczas czekania na zmianę kierunku mostu." CLR_RESET "\n", id, getpid());
            return;
        }

        sem_lock(sem_id, SEM_MOST_MUTEX);
        park->bridge_on_bridge++;
        int count = park->bridge_on_bridge;
        sem_unlock(sem_id, SEM_MOST_MUTEX);

        if (sem_lock_interruptible(sem_id, SEM_MOST_LIMIT, &emergency_exit_flag) == -1) {
            sem_lock(sem_id, SEM_MOST_MUTEX);
            park->bridge_on_bridge--;
            printf(CLR_RED "[T %d | PID %d] Ewakuacja podczas czekania na miejsce na moście (po obudzeniu, pozostało: %d)" CLR_RESET "\n", id, getpid(), park->bridge_on_bridge);

            // obsluga zwalniania mostu przy przerwaniu
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

        printf(CLR_CYAN "[T %d | PID %d] Obudzony! Wchodzę na most (%d osób)" CLR_RESET "\n", id, getpid(), count);
    }

    // przejscie przez most
    if (entered_bridge) {
        if (!emergency_exit_flag) {
            printf(CLR_CYAN "[T %d | PID %d] Przechodzę przez most..." CLR_RESET "\n", id, getpid());
            sim_sleep(BRIDGE_CROSS_TIME_MIN, BRIDGE_CROSS_TIME_MAX, 0);
        } else {
            printf(CLR_RED "[T %d | PID %d] Ewakuacja! Szybko schodzę z mostu." CLR_RESET "\n", id, getpid());
        }

        sem_unlock(sem_id, SEM_MOST_LIMIT);

        sem_lock(sem_id, SEM_MOST_MUTEX);
        park->bridge_on_bridge--;

        printf(CLR_CYAN "[T %d | PID %d] Zszedłem z mostu (pozostało: %d)" CLR_RESET "\n", id, getpid(), park->bridge_on_bridge);

        // zmiana kierunku mostu jesli jestesmy ostatni
        if (park->bridge_on_bridge == 0) {
            if (park->bridge_waiting[other_dir] > 0) {
        
                park->bridge_direction = other_dir;
                int to_wake = park->bridge_waiting[other_dir];
                park->bridge_waiting[other_dir] = 0;
        
                printf(CLR_CYAN "[T %d | PID %d] Zmieniam kierunek mostu, budzę %d czekających" CLR_RESET "\n", id, getpid(), to_wake);
        
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

// logika atrakcji: wieza
void do_tower(int id, int age, int is_vip, struct ParkSharedMemory *park, int sem_id) {
    if (age <= 5) {
        printf(CLR_YELLOW "[T %d | PID %d] Mam %d lat - nie mogę wejść na wieżę. Czekam na dole." CLR_RESET "\n", id, getpid(), age);
        return;
    }

    if (g_is_caretaker && g_caretaker_child_age <= 5) {
        printf(CLR_YELLOW "[T %d | PID %d] Jestem opiekunem dziecka (wiek %d) - czekam na dole wieży." CLR_RESET "\n", id, getpid(), g_caretaker_child_age);
        return;
    }

    printf(CLR_MAGENTA "[T %d | PID %d] Podchodzę do wieży widokowej" CLR_RESET "\n", id, getpid());

    if (age < 15) {
        if (g_my_caretaker_id >= 0) {
            printf(CLR_YELLOW "[T %d | PID %d] Mam %d lat - wchodzę na wieżę pod opieką [T %d | PID %d]" CLR_RESET "\n", id, getpid(), age, g_my_caretaker_id, g_my_caretaker_pid);
        } else if (g_has_guide_caretaker) {
            printf(CLR_YELLOW "[T %d | PID %d] Mam %d lat - opiekunem jest przewodnik (PID %d), który nie wchodzi na wieżę." CLR_RESET "\n", id, getpid(), age, g_my_caretaker_pid);
        } else {
            printf(CLR_YELLOW "[T %d | PID %d] Mam %d lat - potrzebuję dorosłego opiekuna na wieżę." CLR_RESET "\n", id, getpid(), age);
        }
    }

    if (age < 15 && g_my_caretaker_id < 0) {
        printf(CLR_RED "[T %d | PID %d] Brak dorosłego opiekuna na wieżę - nie wchodzę." CLR_RESET "\n", id, getpid());
        return;
    }

    if (emergency_exit_flag) {
        printf(CLR_RED "[T %d | PID %d] Ewakuacja! Nie wchodzę na wieżę." CLR_RESET "\n", id, getpid());
        return;
    }

    if (is_vip) {
        printf(CLR_MAGENTA "[T %d | PID %d] Jestem VIPem - omijam kolejkę do wieży" CLR_RESET "\n", id, getpid());
    } else {
        printf(CLR_MAGENTA "[T %d | PID %d] Czekam na wejście na wieżę..." CLR_RESET "\n", id, getpid());
    }

    // zajecie slotu w wiezy
    if (tower_acquire_slot(sem_id, park, is_vip) == -1) {
        printf(CLR_RED "[T %d | PID %d] Ewakuacja przed wejściem na wieżę!" CLR_RESET "\n", id, getpid());
        return;
    }

    if (emergency_exit_flag) {
        printf(CLR_RED "[T %d | PID %d] Ewakuacja przed schodami - rezygnuję z wejścia na wieżę." CLR_RESET "\n", id, getpid());
        tower_release_slot(sem_id, park);
        return;
    }

    // wejscie po schodach
    if (sem_lock_interruptible(sem_id, SEM_TOWER_STAIRS_UP, &emergency_exit_flag) == -1) {
        printf(CLR_RED "[T %d | PID %d] Ewakuacja podczas wejścia po schodach." CLR_RESET "\n", id, getpid());
        tower_release_slot(sem_id, park);
        return;
    }

    printf(CLR_MAGENTA "[T %d | PID %d] Wchodzę po schodach w górę." CLR_RESET "\n", id, getpid());
    sem_unlock(sem_id, SEM_TOWER_STAIRS_UP);

    sem_lock(sem_id, SEM_WIEZA_MUTEX);
    tower_add_visitor(park, getpid());
    int count = park->tower_current_count;
    sem_unlock(sem_id, SEM_WIEZA_MUTEX);

    printf(CLR_MAGENTA "[T %d | PID %d] Wchodzę na wieżę (%d/%d osób)" CLR_RESET "\n", id, getpid(), count, X2_TOWER_CAP);

    printf(CLR_MAGENTA "[T %d | PID %d] Podziwiam widoki z wieży..." CLR_RESET "\n", id, getpid());

    tower_evacuation_flag = 0;

    // czas pobytu na wiezy
    int tower_stay_us = TOWER_VISIT_TIME_MIN;
    if (TOWER_VISIT_TIME_MAX > TOWER_VISIT_TIME_MIN) {
        tower_stay_us = TOWER_VISIT_TIME_MIN + (rand() % (TOWER_VISIT_TIME_MAX - TOWER_VISIT_TIME_MIN + 1));
    }
    int tower_stay_sec = (tower_stay_us > 0) ? (tower_stay_us + 999999) / 1000000 : 0;

    // czekanie z mozliwoscia przerwania przez sygnal ewakuacji
    int tower_result;
    if (tower_stay_sec > 0) {
        tower_result = sem_timed_wait(sem_id, SEM_TOWER_WAIT, tower_stay_sec, &tower_evacuation_flag, &emergency_exit_flag);
    } else {
        tower_result = (tower_evacuation_flag || emergency_exit_flag) ? -1 : 1;
    }

    if (tower_result == -1) {
        if (tower_evacuation_flag) {
            printf(CLR_RED "[T %d | PID %d] SIGUSR1! Natychmiast schodzę z wieży!" CLR_RESET "\n", id, getpid());
        } else {
            printf(CLR_RED "[T %d | PID %d] Ewakuacja ogólna! Schodzę z wieży!" CLR_RESET "\n", id, getpid());
        }
    }

    // zejscie po schodach
    sem_lock(sem_id, SEM_TOWER_STAIRS_DOWN);
    printf(CLR_MAGENTA "[T %d | PID %d] Schodzę po schodach w dół." CLR_RESET "\n", id, getpid());
    sem_unlock(sem_id, SEM_TOWER_STAIRS_DOWN);

    sem_lock(sem_id, SEM_WIEZA_MUTEX);
    tower_remove_visitor(park, getpid());
    sem_unlock(sem_id, SEM_WIEZA_MUTEX);

    tower_release_slot(sem_id, park);

    printf(CLR_MAGENTA "[T %d | PID %d] Zszedłem z wieży" CLR_RESET "\n", id, getpid());
}

// logika atrakcji: prom
void do_ferry(int id, int my_group_id, int age, int is_vip, struct ParkSharedMemory *park, int sem_id) {

    printf(CLR_CYAN "[T %d | PID %d] Podchodzę do promu" CLR_RESET "\n", id, getpid());

    // sprawdzenie opieki
    if (age < 15) {
        if (g_my_caretaker_id >= 0) {
            printf(CLR_YELLOW "[T %d | PID %d] Mam %d lat - wsiadam na prom pod opieką [T %d | PID %d]" CLR_RESET "\n", id, getpid(), age, g_my_caretaker_id, g_my_caretaker_pid);
        } else if (g_has_guide_caretaker) {
            printf(CLR_YELLOW "[T %d | PID %d] Mam %d lat - wsiadam na prom pod opieką przewodnika (PID %d)" CLR_RESET "\n", id, getpid(), age, g_my_caretaker_pid);
        } else {
            printf(CLR_YELLOW "[T %d | PID %d] Mam %d lat - wsiadam na prom pod opieką dorosłego" CLR_RESET "\n", id, getpid(), age);
        }
    }

    if (age < 15 && g_my_caretaker_id < 0 && !g_has_guide_caretaker) {
        printf(CLR_RED "[T %d | PID %d] Brak opiekuna - nie wchodzę na prom." CLR_RESET "\n", id, getpid());
        return;
    }

    if (is_vip) {
        printf(CLR_MAGENTA "[T %d | PID %d] Jestem VIPem przy promie" CLR_RESET "\n", id, getpid());
    }

    if (emergency_exit_flag) {
        printf(CLR_RED "[T %d | PID %d] Ewakuacja przed promem! Pomijam." CLR_RESET "\n", id, getpid());
        return;
    }

    // czekanie na przewodnika
    if (my_group_id >= 0) {
        if (sem_lock_interruptible(sem_id, SEM_FERRY_GUIDE_READY(my_group_id), &emergency_exit_flag) == -1) {
            printf(CLR_RED "[T %d | PID %d] Ewakuacja przed promem - nie czekam na przewodnika." CLR_RESET "\n", id, getpid());
            return;
        }
    }

    int direction = 0;
    if (my_group_id >= 0) {
        direction = get_ferry_direction(park->groups[my_group_id].route);
    }

    if (is_vip) {
        printf(CLR_MAGENTA "[T %d | PID %d] VIP: omijam kolejkę na prom." CLR_RESET "\n", id, getpid());
    } else {
        printf(CLR_CYAN "[T %d | PID %d] Czekam na możliwość wejścia na prom..." CLR_RESET "\n", id, getpid());
    }

    // proba wejscia na prom (kolejkowanie)
    if (ferry_enter(park, sem_id, direction, is_vip, &emergency_exit_flag) == -1) {
        printf(CLR_RED "[T %d | PID %d] Ewakuacja podczas oczekiwania na prom." CLR_RESET "\n", id, getpid());
        return;
    }

    printf(CLR_CYAN "[T %d | PID %d] Wsiadłem na prom (kierunek %d)." CLR_RESET "\n", id, getpid(), direction);
    sim_sleep(FERRY_TRAVEL_TIME_MIN, FERRY_TRAVEL_TIME_MAX, 0);
    ferry_leave(park, sem_id, direction);
    printf(CLR_CYAN "[T %d | PID %d] Wysiadłem z promu." CLR_RESET "\n", id, getpid());
}

// wersja promu dla vipow (samotnych)
void do_ferry_vip(int id, int age, int route, struct ParkSharedMemory *park, int sem_id) {
    (void)age;

    printf(CLR_MAGENTA "[T %d | PID %d] VIP: podchodzę do promu i omijam kolejkę" CLR_RESET "\n", id, getpid());

    int direction = get_ferry_direction(route);
    if (ferry_enter(park, sem_id, direction, 1, &emergency_exit_flag) == -1) {
        printf(CLR_RED "[T %d | PID %d] Ewakuacja przed wejściem na prom VIP." CLR_RESET "\n", id, getpid());
        return;
    }

    printf(CLR_MAGENTA "[T %d | PID %d] VIP: wsiadam na prom (kierunek %d)." CLR_RESET "\n", id, getpid(), direction);
    sim_sleep(FERRY_TRAVEL_TIME_MIN, FERRY_TRAVEL_TIME_MAX, 0);
    ferry_leave(park, sem_id, direction);

    printf(CLR_MAGENTA "[T %d | PID %d] VIP: dotarłem promem na drugi brzeg." CLR_RESET "\n", id, getpid());
}

int main(int argc, char* argv[]) {

    if (argc < 2) {
        printf(CLR_RED "[T ? | PID %d] Błąd: Brak ID turysty! Uruchamiaj przez main." CLR_RESET "\n", getpid());
        exit(1);
    }

    int id = atoi(argv[1]);
    g_id = id; 
    g_pid = getpid();

    // inicjalizacja handlerow sygnalow
    struct sigaction sa1;
    sa1.sa_handler = sigusr1_handler;
    if (sigemptyset(&sa1.sa_mask) == -1) {
        fatal_error(tourist_error_ctx("Błąd sigemptyset(SIGUSR1)"));
    }
    sa1.sa_flags = 0; 

    if (sigaction(SIGUSR1, &sa1, NULL) == -1) {
        fatal_error(tourist_error_ctx("Błąd sigaction SIGUSR1"));
    }

    struct sigaction sa2;
    sa2.sa_handler = sigusr2_handler;
    if (sigemptyset(&sa2.sa_mask) == -1) {
        fatal_error(tourist_error_ctx("Błąd sigemptyset(SIGUSR2)"));
    }
    sa2.sa_flags = 0;
    if (sigaction(SIGUSR2, &sa2, NULL) == -1) {
        fatal_error(tourist_error_ctx("Błąd sigaction SIGUSR2"));
    }

    struct sigaction sa_term;
    sa_term.sa_handler = sigterm_handler;
    if (sigemptyset(&sa_term.sa_mask) == -1) {
        fatal_error(tourist_error_ctx("Błąd sigemptyset(SIGTERM)"));
    }
    sa_term.sa_flags = 0;
    if (sigaction(SIGTERM, &sa_term, NULL) == -1) {
        fatal_error(tourist_error_ctx("Błąd sigaction SIGTERM"));
    }

    time_t seed_time = time(NULL);
    if (seed_time == (time_t)-1) {
        report_error(tourist_error_ctx("Błąd time (srand seed)"));
        srand(id); // uzyj id jako seed
    } else {
        srand(seed_time + id);
    }

    // losowanie atrybutow turysty
    int age = (rand() % 68) + 3; 
    int is_vip = (rand() % 100) < 5; 
    int vip_can_go_solo = (is_vip && age >= 15);
    int should_send_exit_notice = 1;
    int entry_msg_sent = 0;

    // dolaczenie do pamieci dzielonej i semaforow
    int shm_id = shmget(ftok(FTOK_PATH, FTOK_SHM_ID), sizeof(struct ParkSharedMemory), 0600);
    int sem_id = semget(ftok(FTOK_PATH, FTOK_SEM_ID), TOTAL_SEMAPHORES, 0600);
    int msg_id = msgget(ftok(FTOK_PATH, FTOK_MSG_ID), 0600);
    int report_msg_id = msgget(ftok(FTOK_PATH, FTOK_MSG_REPORT_ID), 0600);

    if (shm_id == -1 || sem_id == -1 || msg_id == -1 || report_msg_id == -1) {
        fatal_error(tourist_error_ctx("Nie mogę znaleźć zasobów IPC"));
    }

    struct ParkSharedMemory *park = (struct ParkSharedMemory*)shmat(shm_id, NULL, 0);
    if (park == (void*)-1) {
        fatal_error(tourist_error_ctx("Błąd shmat"));
    }

    g_sem_id = sem_id;
    g_park = park;

    if (is_vip) {
        printf(CLR_MAGENTA "[T %d | PID %d] Jestem VIPem (wiek: %d). Mam legitymację PTTK!" CLR_RESET "\n", id, getpid(), age);
    } else if (age < 7) {
        printf(CLR_YELLOW "[T %d | PID %d] Jestem dzieckiem (wiek: %d). Wchodzę za darmo!" CLR_RESET "\n", id, getpid(), age);
    } else {
        printf(CLR_CYAN "[T %d | PID %d] Przychodzę do parku (wiek: %d)." CLR_RESET "\n", id, getpid(), age);
    }

    // sprawdzenie czy park jest otwarty
    time_t check_time = time(NULL);
    if (check_time == (time_t)-1) {
        report_error(tourist_error_ctx("Błąd time (sprawdzenie zamknięcia parku)"));
        check_time = park->park_closing_time; // zaloz ze park zamkniety w przypadku bledu
    }
    if (park->park_closed || check_time >= park->park_closing_time) {
        printf(CLR_YELLOW "[T %d | PID %d] Park zamknięty (Tk). Odchodzę." CLR_RESET "\n", id, getpid());
        if (fflush(stdout) == EOF) {
            report_error(tourist_error_ctx("Błąd fflush (park zamknięty)"));
        }

        sem_lock(sem_id, SEM_STATS_MUTEX);
        park->rejected_after_close++;
        park->total_expected--;
        if (park->total_exited >= park->total_expected) {
            sem_unlock(sem_id, SEM_ALL_DONE);
        }
        sem_unlock(sem_id, SEM_STATS_MUTEX);

        if (shmdt(park) == -1) {
            report_error(tourist_error_ctx("Błąd shmdt"));
        }
        return 0;
    }

    // sprawdzenie limitu dziennego N (atomowe, pod mutexem)
    sem_lock(sem_id, SEM_STATS_MUTEX);
    if (park->daily_entered_count >= park->daily_visitor_limit) {
        park->rejected_daily_limit++;
        park->total_expected--;
        if (park->total_exited >= park->total_expected) {
            sem_unlock(sem_id, SEM_ALL_DONE);
        }
        sem_unlock(sem_id, SEM_STATS_MUTEX);

        printf(CLR_YELLOW "[T %d | PID %d] Osiągnięty limit dzienny (%d osób). Odchodzę." CLR_RESET "\n", id, getpid(), park->daily_visitor_limit);
        if (fflush(stdout) == EOF) {
            report_error(tourist_error_ctx("Błąd fflush (limit dzienny)"));
        }

        // jeśli limit osiągnięty, obudź przewodników dla ewentualnej niepełnej grupy
        wake_guides_for_queue(sem_id, park, 1);

        if (shmdt(park) == -1) {
            report_error(tourist_error_ctx("Błąd shmdt"));
        }
        return 0;
    }
    park->daily_entered_count++;
    int limit_reached_now = (park->daily_entered_count >= park->daily_visitor_limit);
    sem_unlock(sem_id, SEM_STATS_MUTEX);

    if (limit_reached_now) {
        wake_guides_for_queue(sem_id, park, 1);
    }

    // sciezka dla vipow zwiedzajacych samotnie
    if (vip_can_go_solo) {
        if (reject_if_closed_late(id, sem_id, park, 0)) {
            return 0;
        }
        printf(CLR_MAGENTA "[T %d | PID %d] VIP: wejście bezpłatne, pomijam kasę." CLR_RESET "\n", id, getpid());
        enter_park_and_report(id, age, is_vip, sem_id, msg_id, park);
        printf(CLR_GREEN "[T %d | PID %d] Wszedłem do parku! Idę do punktu zbiórki." CLR_RESET "\n", id, getpid());
        printf(CLR_MAGENTA "[T %d | PID %d] VIP: omijam kolejkę do kasy i zwiedzam samodzielnie." CLR_RESET "\n", id, getpid());

        int route = (rand() % 2) + 1;
        printf(CLR_MAGENTA "[T %d | PID %d] VIP: startuję trasę %d solo." CLR_RESET "\n", id, getpid(), route);

        //sim_sleep(WALK_TIME_MIN, WALK_TIME_MAX, 0);

        for (int step = 0; step < 3; step++) {
            int attraction = get_attraction_for_step(route, step);
            if (!emergency_exit_flag) {
                printf(CLR_MAGENTA "[T %d | PID %d] VIP: idę do atrakcji %d" CLR_RESET "\n", id, getpid(), attraction);
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
                printf(CLR_RED "[T %d | PID %d] VIP: ewakuacja! Pomijam atrakcję %d." CLR_RESET "\n", id, getpid(), attraction);
            }
            if (step < 2) {
                //sim_sleep(WALK_TIME_MIN, WALK_TIME_MAX, 0);
            }
        }

        //sim_sleep(WALK_TIME_MIN, WALK_TIME_MAX, 0);

        printf(CLR_MAGENTA "[T %d | PID %d] VIP: koniec wycieczki. Wracam do kasy." CLR_RESET "\n", id, getpid());
        goto cleanup;
    }

    if (is_vip && age < 15) {
        printf(CLR_MAGENTA "[T %d | PID %d] VIP-dziecko: omijam kasę, ale muszę iść z grupą (potrzebuję opiekuna)." CLR_RESET "\n", id, getpid());
        printf(CLR_MAGENTA "[T %d | PID %d] VIP-dziecko: wejście bezpłatne." CLR_RESET "\n", id, getpid());
    }

    // sciezka dla turystow grupowych
    if (!is_vip) {
        if (sem_lock_interruptible(sem_id, SEM_CASH_QUEUE_SLOTS, &emergency_exit_flag) == -1) {
            printf(CLR_RED "[T %d | PID %d] Ewakuacja przed kasą!" CLR_RESET "\n", id, getpid());
            goto cleanup;
        }

        sem_lock(sem_id, SEM_CASH_QUEUE_MUTEX);
        park->cash_queue_count++;
        sem_unlock(sem_id, SEM_CASH_QUEUE_MUTEX);

        printf(CLR_CYAN "[T %d | PID %d] Ustawiam się w kolejce do kasy." CLR_RESET "\n", id, getpid());

        sem_lock(sem_id, SEM_CASH_QUEUE_MUTEX);
        if (park->cash_queue_count > 0) {
            park->cash_queue_count--;
        }
        sem_unlock(sem_id, SEM_CASH_QUEUE_MUTEX);

        sem_unlock(sem_id, SEM_CASH_QUEUE_SLOTS);

        if (age < 7) {
            printf(CLR_YELLOW "[T %d | PID %d] Dziecko <7 - bilet bezpłatny." CLR_RESET "\n", id, getpid());
        } else {
            printf(CLR_CYAN "[T %d | PID %d] Płacę za bilet: %d PLN." CLR_RESET "\n", id, getpid(), TICKET_PRICE);
        }
    }

    // wejscie do kolejki grupowej
    if (sem_lock_interruptible(sem_id, SEM_QUEUE_SLOTS, &emergency_exit_flag) == -1) {
        printf(CLR_RED "[T %d | PID %d] Ewakuacja przed wejściem do kolejki!" CLR_RESET "\n", id, getpid());
        goto cleanup;
    }

    if (reject_if_closed_late(id, sem_id, park, 1)) {
        return 0;
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
        printf(CLR_MAGENTA "[T %d | PID %d] VIP-dziecko: czekam na przydzielenie do grupy." CLR_RESET "\n", id, getpid());
    } else {
        printf(CLR_CYAN "[T %d | PID %d] Jestem w kolejce grupowej." CLR_RESET "\n", id, getpid());
    }

    if (!entry_msg_sent) {
        enter_park_and_report(id, age, is_vip, sem_id, msg_id, park);
        entry_msg_sent = 1;
        printf(CLR_GREEN "[T %d | PID %d] Wszedłem do parku! Idę do punktu zbiórki." CLR_RESET "\n", id, getpid());
    }

    printf(CLR_CYAN "[T %d | PID %d] Czekam na przewodnika. (Kolejka: %d/%d)" CLR_RESET "\n", id, getpid(), current_count, M_GROUP_SIZE);

    // budzenie przewodnika jesli kolejka pelna lub zamykamy park
    if (current_count >= M_GROUP_SIZE) {
        printf(CLR_CYAN "[T %d | PID %d] Kolejka ma %d osób - budzę przewodnika!" CLR_RESET "\n", id, getpid(), current_count);
        wake_guides_for_count(sem_id, current_count, 0);
    } else if ((park->park_closed || park->daily_entered_count >= park->daily_visitor_limit) && current_count > 0) {
        printf(CLR_YELLOW "[T %d | PID %d] Limit dzienny/zamknięcie - budzę przewodnika dla niepełnej grupy (%d osób)." CLR_RESET "\n", id, getpid(), current_count);
        wake_guides_for_count(sem_id, current_count, 1);
    }

    printf(CLR_CYAN "[T %d | PID %d] Czekam na przydzielenie do grupy (pozycja %d)..." CLR_RESET "\n", id, getpid(), my_position);

    // oczekiwanie na przypisanie przez przewodnika
    sem_lock(sem_id, SEM_TOURIST_ASSIGNED(my_position));

    sem_lock(sem_id, SEM_QUEUE_MUTEX);
    int my_group_id = park->assigned_group_id[my_position];
    g_member_index = park->assigned_member_index[my_position];
    sem_unlock(sem_id, SEM_QUEUE_MUTEX);

    g_has_queue_slot = 0;

    sem_unlock(sem_id, SEM_TOURIST_READ_DONE(my_position));

    printf(CLR_CYAN "[T %d | PID %d] Potwierdzam odczyt, przydzielony do grupy %d." CLR_RESET "\n", id, getpid(), my_group_id);

    if (my_group_id < 0 || my_group_id >= MAX_GROUPS) {
        printf(CLR_RED "[T %d | PID %d] Błąd: nieprawidłowy group_id=%d!" CLR_RESET "\n", id, getpid(), my_group_id);
        goto cleanup;
    }
    should_send_exit_notice = 0;

    if (emergency_exit_flag) {
        printf(CLR_RED "[T %d | PID %d] Ewakuacja - ale muszę dokończyć protokół grupy!" CLR_RESET "\n", id, getpid());
    }

    printf(CLR_CYAN "[T %d | PID %d] Czekam na start wycieczki..." CLR_RESET "\n", id, getpid());

    // sprawdzanie przydzialu opieki w grupie
    struct GroupState *my_group = &park->groups[my_group_id];
    for (int i = 0; i < M_GROUP_SIZE; i++) {
        if (my_group->member_pids[i] == getpid()) {
            g_is_caretaker = my_group->member_is_caretaker[i];

            if (g_is_caretaker) {
                int child_idx = my_group->member_caretaker_of[i];
                if (child_idx >= 0) {
                    g_caretaker_child_age = my_group->member_ages[child_idx];
                    g_caretaker_child_id = my_group->member_ids[child_idx];
                    g_caretaker_child_pid = my_group->member_pids[child_idx];
                }
                if (g_caretaker_child_age <= 5) {
                    printf(CLR_YELLOW "[T %d | PID %d] Jestem opiekunem dziecka [T %d | PID %d] (wiek %d) - nie wejdę na wieżę" CLR_RESET "\n", id, getpid(), g_caretaker_child_id, g_caretaker_child_pid, g_caretaker_child_age);
                } else {
                    printf(CLR_YELLOW "[T %d | PID %d] Jestem opiekunem dziecka [T %d | PID %d] (wiek %d)" CLR_RESET "\n", id, getpid(), g_caretaker_child_id, g_caretaker_child_pid, g_caretaker_child_age);
                }
            }

            int caretaker_idx = my_group->member_has_caretaker[i];
            if (caretaker_idx >= 0) {
                g_my_caretaker_id = my_group->member_ids[caretaker_idx];
                g_my_caretaker_pid = my_group->member_pids[caretaker_idx];
            } else if (my_group->member_caretaker_is_guide[i]) {
                g_has_guide_caretaker = 1;
                g_my_caretaker_id = -1;
                g_my_caretaker_pid = my_group->guide_pid;
            }
            break;
        }
    }

    if (age < 15) {
        if (g_my_caretaker_id >= 0) {
            printf(CLR_YELLOW "[T %d | PID %d] Mój opiekun: [T %d | PID %d]" CLR_RESET "\n", id, getpid(), g_my_caretaker_id, g_my_caretaker_pid);
        } else if (g_has_guide_caretaker) {
            printf(CLR_YELLOW "[T %d | PID %d] Mój opiekun: Przewodnik PID %d" CLR_RESET "\n", id, getpid(), g_my_caretaker_pid);
        }
    }

    sem_lock(sem_id, SEM_MEMBER_GO(my_group_id, g_member_index));

    int route = my_group->route;

    printf(CLR_CYAN "[T %d | PID %d] Wycieczka start! Trasa %d" CLR_RESET "\n", id, getpid(), route);

    // petla zwiedzania atrakcji
    for (int step = 0; step < 3; step++) {

        int attraction = get_attraction_for_step(route, step);

        if (!emergency_exit_flag) {
            printf(CLR_CYAN "[T %d | PID %d] Faza %d: idę do atrakcji %d" CLR_RESET "\n", id, getpid(), step + 1, attraction);
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
            printf(CLR_RED "[T %d | PID %d] Ewakuacja! Pomijam atrakcję %d, ale zgłaszam obecność." CLR_RESET "\n", id, getpid(), attraction);
        }

        // zgloszenie wykonania etapu przewodnikowi
        sem_unlock(sem_id, SEM_GROUP_DONE(my_group_id));

        if (!emergency_exit_flag) {
            printf(CLR_CYAN "[T %d | PID %d] Zakończyłem atrakcje %d. Czekam na grupę." CLR_RESET "\n", id, getpid(), attraction);
        }

        if (step < 2) {
            sem_lock(sem_id, SEM_MEMBER_GO(my_group_id, g_member_index));
        }
    }

    printf(CLR_CYAN "[T %d | PID %d] Koniec wycieczki. Wracam do kasy." CLR_RESET "\n", id, getpid());

cleanup:
    if (g_has_queue_slot) {
        sem_unlock(sem_id, SEM_QUEUE_SLOTS);
        g_has_queue_slot = 0;
        printf(CLR_CYAN "[T %d | PID %d] Zwolniłem slot w kolejce." CLR_RESET "\n", id, getpid());
    }

    printf(CLR_GREEN "[T %d | PID %d] Wychodzę z parku." CLR_RESET "\n", id, getpid());

    // wyslanie powiadomienia o wyjsciu
    if (should_send_exit_notice) {
        struct msg_buffer notice_msg;
        notice_msg.msg_type = MSG_TYPE_EXIT_NOTICE;
        notice_msg.tourist_id = id;
        notice_msg.tourist_pid = getpid();
        notice_msg.age = age;
        notice_msg.is_vip = is_vip;

        char timestamp[20];
        get_timestamp(timestamp, sizeof(timestamp));
        size_t len = strlen(timestamp);
        if (len >= sizeof(notice_msg.info)) {
            len = sizeof(notice_msg.info) - 1;
        }
        memcpy(notice_msg.info, timestamp, len);
        notice_msg.info[len] = '\0';

        if (msgsnd(report_msg_id, &notice_msg, sizeof(notice_msg) - sizeof(long), 0) == -1) {
            report_error(tourist_error_ctx("Błąd msgsnd (notice wyjścia)"));
        }
    }

    // aktualizacja licznikow w parku
    sem_lock(sem_id, SEM_STATS_MUTEX);
    if (park->people_in_park > 0) {
        park->people_in_park--;
    }
    if (is_vip && park->vip_in_park > 0) {
        park->vip_in_park--;
    }
    sem_unlock(sem_id, SEM_STATS_MUTEX);

    printf(CLR_CYAN "[T %d | PID %d] Do widzenia!" CLR_RESET "\n", id, getpid());

    if (shmdt(park) == -1) {
        report_error(tourist_error_ctx("Błąd shmdt"));
    }

    return 0;
}
