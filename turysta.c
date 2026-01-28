#include "common.h"
#include <signal.h>

volatile sig_atomic_t tower_evacuation_flag = 0;
volatile sig_atomic_t emergency_exit_flag = 0;

int g_sem_id = -1;
int g_id = -1;
struct ParkSharedMemory *g_park = NULL;

int g_is_caretaker = 0;
int g_caretaker_child_age = -1;
int g_my_caretaker_id = -1;

int g_member_index = -1;
int g_has_queue_slot = 0;

void sigusr1_handler(int sig) {
    tower_evacuation_flag = 1;
    char msg[100];
    int len = sprintf(msg, "\n[TURYSTA %d] SIGUSR1: Ewakuacja z wieży!\n", g_id);
    write(STDOUT_FILENO, msg, len);
}

void sigusr2_handler(int sig) {
    emergency_exit_flag = 1;
    char msg[100];
    int len = sprintf(msg, "\n[TURYSTA %d] SIGUSR2: Alarm! Natychmiastowy powrót do kasy!\n", g_id);
    write(STDOUT_FILENO, msg, len);
}

void do_bridge(int id, int age, int is_vip, int direction, struct ParkSharedMemory *park, int sem_id) {
    int other_dir = 1 - direction;
    int entered_bridge = 0;  

    printf("[TURYSTA %d] Podchodzę do mostu (kierunek: %s)\n", id, direction == DIR_KA ? "K->A" : "A->K");

    if (age < 15) {
        if (g_my_caretaker_id >= 0) {
            printf("[TURYSTA %d] Mam %d lat - idę przez most pod opieką turysty %d\n", id, age, g_my_caretaker_id);
        } else {
            printf("[TURYSTA %d] Mam %d lat - idę przez most pod opieką dorosłego\n", id, age);
        }
    }

    if (emergency_exit_flag) {
        printf("[TURYSTA %d] Ewakuacja przed mostem! Pomijam.\n", id);
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
            printf("[TURYSTA %d] Ewakuacja podczas czekania na miejsce na moście (pozostało: %d)\n",
                   id, park->bridge_on_bridge);

            if (park->bridge_on_bridge == 0) {
                park->bridge_direction = DIR_NONE;
            }
            sem_unlock(sem_id, SEM_MOST_MUTEX);
            return;  

        }

        entered_bridge = 1;  

        printf("[TURYSTA %d] Wchodzę na most (%d osób na moście)\n", id, park->bridge_on_bridge);

    } else {

        park->bridge_waiting[direction]++;
        printf("[TURYSTA %d] Most zajęty w przeciwnym kierunku. Czekam...\n", id);
        sem_unlock(sem_id, SEM_MOST_MUTEX);

        if (sem_lock_interruptible(sem_id, SEM_BRIDGE_WAIT(direction), &emergency_exit_flag) == -1) {
            sem_lock(sem_id, SEM_MOST_MUTEX);
            if (park->bridge_waiting[direction] > 0) {
                park->bridge_waiting[direction]--;
            }
            sem_unlock(sem_id, SEM_MOST_MUTEX);
            printf("[TURYSTA %d] Ewakuacja podczas czekania na zmianę kierunku mostu.\n", id);
            return;
        }

        sem_lock(sem_id, SEM_MOST_MUTEX);
        park->bridge_on_bridge++;
        int count = park->bridge_on_bridge;
        sem_unlock(sem_id, SEM_MOST_MUTEX);

        if (sem_lock_interruptible(sem_id, SEM_MOST_LIMIT, &emergency_exit_flag) == -1) {
            sem_lock(sem_id, SEM_MOST_MUTEX);
            park->bridge_on_bridge--;
            printf("[TURYSTA %d] Ewakuacja podczas czekania na miejsce na moście (po obudzeniu, pozostało: %d)\n", id, park->bridge_on_bridge);

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

        printf("[TURYSTA %d] Obudzony! Wchodzę na most (%d osób)\n", id, count);
    }

    if (entered_bridge) {
        if (!emergency_exit_flag) {
            printf("[TURYSTA %d] Przechodzę przez most...\n", id);

        } else {
            printf("[TURYSTA %d] Ewakuacja! Szybko schodzę z mostu.\n", id);
        }

        sem_unlock(sem_id, SEM_MOST_LIMIT);

        sem_lock(sem_id, SEM_MOST_MUTEX);
        park->bridge_on_bridge--;

        printf("[TURYSTA %d] Zszedłem z mostu (pozostało: %d)\n", id, park->bridge_on_bridge);

        if (park->bridge_on_bridge == 0) {
            if (park->bridge_waiting[other_dir] > 0) {

                park->bridge_direction = other_dir;
                int to_wake = park->bridge_waiting[other_dir];
                park->bridge_waiting[other_dir] = 0;

                printf("[TURYSTA %d] Zmieniam kierunek mostu, budzę %d czekających\n", id, to_wake);

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
    }
}

void do_tower(int id, int age, int is_vip, struct ParkSharedMemory *park, int sem_id) {
    if (age <= 5) {
        printf("[TURYSTA %d] Mam %d lat - nie mogę wejść na wieżę. Czekam na dole.\n", id, age);
        return;
    }

    if (g_is_caretaker && g_caretaker_child_age <= 5) {
        printf("[TURYSTA %d] Jestem opiekunem dziecka (wiek %d) - czekam na dole wieży.\n", id, g_caretaker_child_age);
        return;
    }

    printf("[TURYSTA %d] Podchodzę do wieży widokowej\n", id);

    if (age < 15) {
        if (g_my_caretaker_id >= 0) {
            printf("[TURYSTA %d] Mam %d lat - wchodzę na wieżę pod opieką turysty %d\n", id, age, g_my_caretaker_id);
        } else {
            printf("[TURYSTA %d] Mam %d lat - wchodzę na wieżę pod opieką dorosłego\n", id, age);
        }
    }

    if (emergency_exit_flag) {
        printf("[TURYSTA %d] Ewakuacja! Nie wchodzę na wieżę.\n", id);
        return;
    }

    int entered = 0;

    if (is_vip) {
        printf("[TURYSTA %d] Jestem VIPem - próbuję ominąć kolejkę do wieży\n", id);

        if (sem_trylock(sem_id, SEM_WIEZA_LIMIT) == 0) {
            printf("[TURYSTA %d] VIP wchodzi na wieżę bez czekania!\n", id);
            entered = 1;
        } else {
            printf("[TURYSTA %d] Brak miejsca - VIP czeka na wieżę\n", id);

            if (sem_lock_interruptible(sem_id, SEM_WIEZA_LIMIT, &emergency_exit_flag) == 0) {
                entered = 1;
            }
        }
    } else {
        printf("[TURYSTA %d] Czekam na wejście na wieżę...\n", id);

        if (sem_lock_interruptible(sem_id, SEM_WIEZA_LIMIT, &emergency_exit_flag) == 0) {
            entered = 1;
        }
    }

    if (!entered) {
        printf("[TURYSTA %d] Ewakuacja przed wejściem na wieżę!\n", id);
        return;
    }

    sem_lock(sem_id, SEM_WIEZA_MUTEX);
    park->tower_current_count++;
    tower_add_visitor(park, getpid());
    int count = park->tower_current_count;
    sem_unlock(sem_id, SEM_WIEZA_MUTEX);

    printf("[TURYSTA %d] Wchodzę na wieżę (%d/%d osób)\n", id, count, X2_TOWER_CAP);

    printf("[TURYSTA %d] Podziwiam widoki z wieży...\n", id);

    tower_evacuation_flag = 0;

    int tower_result = sem_timed_wait(sem_id, SEM_TOWER_WAIT, TOWER_VISIT_TIME, &tower_evacuation_flag, &emergency_exit_flag);

    if (tower_result == -1) {
        if (tower_evacuation_flag) {
            printf("[TURYSTA %d] SIGUSR1! Natychmiast schodzę z wieży!\n", id);
        } else {
            printf("[TURYSTA %d] Ewakuacja ogólna! Schodzę z wieży!\n", id);
        }
    }

    sem_lock(sem_id, SEM_WIEZA_MUTEX);
    park->tower_current_count--;
    tower_remove_visitor(park, getpid());
    sem_unlock(sem_id, SEM_WIEZA_MUTEX);

    sem_unlock(sem_id, SEM_WIEZA_LIMIT);

    printf("[TURYSTA %d] Zszedłem z wieży\n", id);
}

void do_ferry(int id, int my_group_id, int age, int is_vip, struct ParkSharedMemory *park, int sem_id) {

    printf("[TURYSTA %d] Podchodzę do promu\n", id);

    if (age < 15) {
        if (g_my_caretaker_id >= 0) {
            printf("[TURYSTA %d] Mam %d lat - wsiadam na prom pod opieką turysty %d\n", id, age, g_my_caretaker_id);
        } else {
            printf("[TURYSTA %d] Mam %d lat - wsiadam na prom pod opieką dorosłego\n", id, age);
        }
    }

    if (is_vip) {
        printf("[TURYSTA %d] Jestem VIPem przy promie\n", id);
    }

    if (emergency_exit_flag) {
        printf("[TURYSTA %d] Ewakuacja przed promem! Pomijam.\n", id);
        return;
    }

    printf("[TURYSTA %d] Czekam na pozwolenie wsiadania na prom...\n", id);

    if (sem_lock_interruptible(sem_id, SEM_FERRY_BOARD, &emergency_exit_flag) == -1) {
        printf("[TURYSTA %d] Ewakuacja podczas czekania na prom - wychodzę.\n", id);
        return;
    }

    int actually_boarded = 0;
    if (!emergency_exit_flag) {
        sem_lock(sem_id, SEM_PROM_MUTEX);
        park->ferry_passengers++;
        int passengers = park->ferry_passengers;
        sem_unlock(sem_id, SEM_PROM_MUTEX);

        printf("[TURYSTA %d] Wsiadłem na prom (%d osób na pokładzie)\n", id, passengers);
        actually_boarded = 1;
    } else {
        printf("[TURYSTA %d] Ewakuacja po wejściu do protokołu - kontynuuję sygnalizację.\n", id);
    }

    sem_unlock(sem_id, SEM_FERRY_ALL_ABOARD);
    
    printf("[TURYSTA %d] Czekam na dopłynięcie promu...\n", id);
    sem_lock(sem_id, SEM_FERRY_ARRIVE);

    if (actually_boarded) {
        sem_lock(sem_id, SEM_PROM_MUTEX);
        park->ferry_passengers--;
        park->ferry_disembarked++;
        sem_unlock(sem_id, SEM_PROM_MUTEX);

        printf("[TURYSTA %d] Wysiadłem z promu\n", id);
    } else {
        printf("[TURYSTA %d] Zakończyłem protokół promu (tryb ewakuacji)\n", id);
    }

    sem_unlock(sem_id, SEM_FERRY_DISEMBARK);
}

int main(int argc, char* argv[]) {

    if (argc < 2) {
        printf("[TURYSTA] Błąd: Brak ID turysty! Uruchamiaj przez main.\n");
        exit(1);
    }

    int id = atoi(argv[1]);
    g_id = id; 

    struct sigaction sa1;
    sa1.sa_handler = sigusr1_handler;
    sigemptyset(&sa1.sa_mask);
    sa1.sa_flags = 0; 

    sigaction(SIGUSR1, &sa1, NULL);

    struct sigaction sa2;
    sa2.sa_handler = sigusr2_handler;
    sigemptyset(&sa2.sa_mask);
    sa2.sa_flags = 0;
    sigaction(SIGUSR2, &sa2, NULL);

    srand(time(NULL) + id);

    int age = (rand() % 68) + 3; 

    int is_vip = (rand() % 100) < 10; 

    int shm_id = shmget(SHM_KEY_ID, sizeof(struct ParkSharedMemory), 0600);
    int sem_id = semget(SEM_KEY_ID, TOTAL_SEMAPHORES, 0600);
    int msg_id = msgget(MSG_KEY_ID, 0600);

    if (shm_id == -1 || sem_id == -1 || msg_id == -1) {
        perror("[TURYSTA] Nie mogę znaleźć zasobów IPC");
        exit(1);
    }

    struct ParkSharedMemory *park = (struct ParkSharedMemory*)shmat(shm_id, NULL, 0);
    if (park == (void*)-1) {
        perror("[TURYSTA] Błąd shmat");
        exit(1);
    }

    g_sem_id = sem_id;
    g_park = park;

    if (is_vip) {
        printf("[TURYSTA %d] Jestem VIPem (wiek: %d). Mam legitymację PTTK!\n", id, age);
    } else if (age < 7) {
        printf("[TURYSTA %d] Jestem dzieckiem (wiek: %d). Wchodzę za darmo!\n", id, age);
    } else {
        printf("[TURYSTA %d] Przychodzę do parku (wiek: %d).\n", id, age);
    }

    sem_lock(sem_id, SEM_PARK_LIMIT);

    sem_lock(sem_id, SEM_STATS_MUTEX);
    park->people_in_park++;
    if (is_vip) {
        park->vip_in_park++;
    }
    sem_unlock(sem_id, SEM_STATS_MUTEX);

    printf("[TURYSTA %d] Wszedłem do parku! Idę do punktu zbiórki.\n", id);

    if (sem_lock_interruptible(sem_id, SEM_QUEUE_SLOTS, &emergency_exit_flag) == -1) {
        printf("[TURYSTA %d] Ewakuacja przed wejściem do kolejki!\n", id);
        goto cleanup;
    }

    g_has_queue_slot = 1;

    sem_lock(sem_id, SEM_QUEUE_MUTEX);

    int my_position = park->people_in_queue;
    park->queue_ages[my_position] = age;
    park->queue_vips[my_position] = is_vip;
    park->queue_pids[my_position] = getpid();
    park->queue_ids[my_position] = id;
    park->people_in_queue++;

    int current_count = park->people_in_queue;

    sem_unlock(sem_id, SEM_QUEUE_MUTEX);

    printf("[TURYSTA %d] Jestem w kolejce. Kupuję bilet...\n", id);

    struct msg_buffer entry_msg;
    entry_msg.msg_type = MSG_TYPE_ENTRY;
    entry_msg.tourist_id = id;
    entry_msg.age = age;
    entry_msg.is_vip = is_vip;
    strcpy(entry_msg.info, "wejście do parku");

    if (msgsnd(msg_id, &entry_msg, sizeof(entry_msg) - sizeof(long), 0) == -1) {
        perror("[TURYSTA] Błąd msgsnd (wejście)");
        exit(1);
    }

    printf("[TURYSTA %d] Czekam na przewodnika. (Kolejka: %d/%d)\n", id, current_count, M_GROUP_SIZE);

    if (current_count == M_GROUP_SIZE) {
        printf("[TURYSTA %d] Komplet! Budzę przewodnika!\n", id);
        sem_unlock(sem_id, SEM_PRZEWODNIK);
    }

    printf("[TURYSTA %d] Czekam na przydzielenie do grupy (pozycja %d)...\n", id, my_position);

    sem_lock(sem_id, SEM_TOURIST_ASSIGNED(my_position));

    sem_lock(sem_id, SEM_QUEUE_MUTEX);
    int my_group_id = park->assigned_group_id[my_position];
    g_member_index = park->assigned_member_index[my_position];
    sem_unlock(sem_id, SEM_QUEUE_MUTEX);

    g_has_queue_slot = 0;

    sem_unlock(sem_id, SEM_TOURIST_READ_DONE(my_position));

    printf("[TURYSTA %d] Potwierdzam odczyt, przydzielony do grupy %d.\n", id, my_group_id);

    if (my_group_id < 0 || my_group_id >= MAX_GROUPS) {
        printf("[TURYSTA %d] Błąd: nieprawidłowy group_id=%d!\n", id, my_group_id);
        goto cleanup;
    }

    if (emergency_exit_flag) {
        printf("[TURYSTA %d] Ewakuacja - ale muszę dokończyć protokół grupy!\n", id);
    }

    printf("[TURYSTA %d] Czekam na start wycieczki...\n", id);

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
                    printf("[TURYSTA %d] Jestem opiekunem dziecka (wiek %d) - nie wejdę na wieżę\n", id, g_caretaker_child_age);
                } else {
                    printf("[TURYSTA %d] Jestem opiekunem dziecka (wiek %d)\n", id, g_caretaker_child_age);
                }
            }

            int caretaker_idx = my_group->member_has_caretaker[i];
            if (caretaker_idx >= 0) {
                g_my_caretaker_id = my_group->member_ids[caretaker_idx];
            }
            break;
        }
    }

    sem_lock(sem_id, SEM_MEMBER_GO(my_group_id, g_member_index));

    int route = my_group->route;

    printf("[TURYSTA %d] Wycieczka start! Trasa %d\n", id, route);

    for (int step = 0; step < 3; step++) {

        int attraction = get_attraction_for_step(route, step);

        if (!emergency_exit_flag) {
            printf("[TURYSTA %d] Faza %d: idę do atrakcji %d\n", id, step + 1, attraction);
            switch (attraction) {
                case ATTR_BRIDGE:
                    do_bridge(id, age, is_vip, get_bridge_direction(route), park, sem_id);
                    break; 
                case ATTR_TOWER:
                    do_tower(id, age, is_vip, park, sem_id);
                    break; 
                case ATTR_FERRY:
                    do_ferry(id, my_group_id, age, is_vip, park, sem_id);
                    break;
            }
        } else {
            printf("[TURYSTA %d] Ewakuacja! Pomijam atrakcję %d, ale zgłaszam obecność.\n", id, attraction);
        }

        sem_unlock(sem_id, SEM_GROUP_DONE(my_group_id));

        if (!emergency_exit_flag) {
            printf("[TURYSTA %d] Zakończyłem atrakcje %d. Czekam na grupę.\n", id, attraction);
        }

        if (step < 2) {
            sem_lock(sem_id, SEM_MEMBER_GO(my_group_id, g_member_index));
        }
    }

    printf("[TURYSTA %d] Koniec wycieczki. Wracam do kasy.\n", id);

cleanup:
    if (g_has_queue_slot) {
        sem_unlock(sem_id, SEM_QUEUE_SLOTS);
        g_has_queue_slot = 0;
        printf("[TURYSTA %d] Zwolniłem slot w kolejce.\n", id);
    }

    printf("[TURYSTA %d] Wychodzę z parku.\n", id);

    struct msg_buffer exit_msg;
    exit_msg.msg_type = MSG_TYPE_EXIT;
    exit_msg.tourist_id = id;
    exit_msg.age = age;
    exit_msg.is_vip = is_vip;

    char timestamp[20];
    get_timestamp(timestamp, sizeof(timestamp));
    strcpy(exit_msg.info, timestamp);

    if (msgsnd(msg_id, &exit_msg, sizeof(exit_msg) - sizeof(long), 0) == -1) {
        perror("[TURYSTA] Błąd msgsnd (wyjście)");
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

    printf("[TURYSTA %d] Do widzenia!\n", id);

    shmdt(park);

    return 0;
}