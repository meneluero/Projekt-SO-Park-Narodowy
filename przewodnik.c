#include "common.h"
#include <signal.h>
#include <fcntl.h>

volatile sig_atomic_t shutdown_flag = 0;

void sigterm_handler(int sig) {
    (void)sig;  

    shutdown_flag = 1;
    char msg[] = "\n\033[1;31m[PRZEWODNIK] Otrzymano SIGTERM. Kończę pracę.\033[0m\n";
    if (write(STDOUT_FILENO, msg, sizeof(msg) - 1) == -1) {
        report_error("[PRZEWODNIK] Błąd write w handlerze SIGTERM");
    }
}

void send_emergency_exit(struct GroupState *group, int guide_id) {
    printf(CLR_RED "\n[PRZEWODNIK %d] Sytuacja awaryjna! Wysyłam SIGUSR2 do grupy!" CLR_RESET "\n", guide_id);

    group->signal_emergency_exit = 1;

    for (int i = 0; i < M_GROUP_SIZE; i++) {
        if (group->member_pids[i] > 0) {
            printf(CLR_RED "[PRZEWODNIK %d] SIGUSR2 -> Turysta PID=%d" CLR_RESET "\n", guide_id, group->member_pids[i]);
            if (kill(group->member_pids[i], SIGUSR2) == -1) {
                report_error("[PRZEWODNIK] Błąd kill SIGUSR2");
            }
        }
    }
    
    printf(CLR_RED "[PRZEWODNIK %d] Odprowadzam grupę bezpośrednio do kasy." CLR_RESET "\n", guide_id);
}

void send_tower_evacuation(struct GroupState *group, struct ParkSharedMemory *park, int guide_id) {
    printf(CLR_RED "\n[PRZEWODNIK %d] Ewakuacja wieży! Wysyłam SIGUSR1!" CLR_RESET "\n", guide_id);

    group->signal_tower_evacuate = 1;

    for (int i = 0; i < M_GROUP_SIZE; i++) {
        pid_t pid = group->member_pids[i];
        if (pid > 0) {
            int on_tower = tower_has_visitor(park, pid);
            printf(CLR_RED "[PRZEWODNIK %d] SIGUSR1 -> Turysta PID=%d (%s)" CLR_RESET "\n", guide_id, pid, on_tower ? "na wieży" : "czeka");
            if (kill(pid, SIGUSR1) == -1) {
                report_error("[PRZEWODNIK] Błąd kill SIGUSR1");
            }
        }
    }
}

int find_free_group_slot(struct ParkSharedMemory *park) {
    for (int i = 0; i < MAX_GROUPS; i++) {
        if (!park->groups[i].active) {
            return i;
        }
    }
    return -1; 

}

void guide_enter_bridge(int guide_id, int group_slot, int group_size, int direction, struct ParkSharedMemory *park, int sem_id) {
    printf(CLR_GREEN "[PRZEWODNIK %d] Podchodzę do mostu (kierunek: %s)" CLR_RESET "\n", guide_id, direction == DIR_KA ? "K->A" : "A->K");

    sem_lock(sem_id, SEM_MOST_MUTEX);

    if (park->bridge_direction == DIR_NONE || park->bridge_direction == direction) {

        park->bridge_direction = direction;
        park->bridge_on_bridge++;
        sem_unlock(sem_id, SEM_MOST_MUTEX);

        sem_lock(sem_id, SEM_MOST_LIMIT);

        printf(CLR_GREEN "[PRZEWODNIK %d] Wchodzę na most pierwszy! (osób na moście: %d)" CLR_RESET "\n", guide_id, park->bridge_on_bridge);
    } else {

        park->bridge_waiting[direction]++;
        printf(CLR_GREEN "[PRZEWODNIK %d] Most zajęty w przeciwnym kierunku. Czekam..." CLR_RESET "\n", guide_id);
        sem_unlock(sem_id, SEM_MOST_MUTEX);

        sem_lock(sem_id, SEM_BRIDGE_WAIT(direction));

        sem_lock(sem_id, SEM_MOST_LIMIT);

        sem_lock(sem_id, SEM_MOST_MUTEX);
        park->bridge_on_bridge++;
        int count = park->bridge_on_bridge;
        sem_unlock(sem_id, SEM_MOST_MUTEX);

        printf(CLR_GREEN "[PRZEWODNIK %d] Obudzony! Wchodzę na most. (osób: %d)" CLR_RESET "\n", guide_id, count);
    }

    for (int i = 0; i < group_size; i++) {
        sem_unlock(sem_id, SEM_BRIDGE_GUIDE_READY(group_slot));
    }

    printf(CLR_GREEN "[PRZEWODNIK %d] Przechodzę przez most..." CLR_RESET "\n", guide_id);

    sem_unlock(sem_id, SEM_MOST_LIMIT);

    printf(CLR_GREEN "[PRZEWODNIK %d] Zszedłem z mostu." CLR_RESET "\n", guide_id);

    int other_dir = 1 - direction;
    sem_lock(sem_id, SEM_MOST_MUTEX);
    park->bridge_on_bridge--;

    if (park->bridge_on_bridge == 0) {
        if (park->bridge_waiting[other_dir] > 0) {
            park->bridge_direction = other_dir;
            int to_wake = park->bridge_waiting[other_dir];
            park->bridge_waiting[other_dir] = 0;

            for (int i = 0; i < to_wake; i++) {
                sem_unlock(sem_id, SEM_BRIDGE_WAIT(other_dir));
            }
            
            sem_unlock(sem_id, SEM_MOST_MUTEX);
            
        } else if (park->bridge_waiting[direction] == 0) {
            park->bridge_direction = DIR_NONE;
            sem_unlock(sem_id, SEM_MOST_MUTEX);
        } else {
            sem_unlock(sem_id, SEM_MOST_MUTEX);
        }
    } else {
        sem_unlock(sem_id, SEM_MOST_MUTEX);
    }

    printf(CLR_GREEN "[PRZEWODNIK %d] Czekam na drugiej stronie mostu na grupę." CLR_RESET "\n", guide_id);
}


void guide_take_ferry(int guide_id, int group_slot, int destination, struct ParkSharedMemory *park, int sem_id, int group_size) {

    int my_shore = 1 - destination;
    (void)group_slot;

    printf(CLR_GREEN "[PRZEWODNIK %d] Podchodzę do promu (chcę na brzeg %d)" CLR_RESET "\n", guide_id, destination);

    sem_lock(sem_id, SEM_FERRY_CONTROL);
    printf(CLR_GREEN "[PRZEWODNIK %d] Przejąłem kontrolę nad promem" CLR_RESET "\n", guide_id);

    sem_lock(sem_id, SEM_PROM_MUTEX);

    if (park->ferry_position != my_shore) {
        printf(CLR_GREEN "[PRZEWODNIK %d] Prom na drugim brzegu. Przywołuję..." CLR_RESET "\n", guide_id);
        park->ferry_position = my_shore;
        printf(CLR_GREEN "[PRZEWODNIK %d] Prom przypłynął na mój brzeg" CLR_RESET "\n", guide_id);
    }

    sem_lock(sem_id, SEM_FERRY_CAP);

    park->ferry_passengers = 1;
    park->ferry_expected = group_size + 1;
    park->ferry_disembarked = 0;
    park->ferry_current_group = group_slot;

    sem_unlock(sem_id, SEM_PROM_MUTEX);

    int vip_priority_flags[M_GROUP_SIZE] = {0};
    for (int i = 0; i < group_size; i++) {
        if (park->groups[group_slot].member_vips[i]) {
            vip_priority_flags[i] = 1;
        }
    }
    for (int i = 0; i < group_size; i++) {
        if (park->groups[group_slot].member_vips[i] && park->groups[group_slot].member_ages[i] < 15) {
            int caretaker_idx = park->groups[group_slot].member_has_caretaker[i];
            if (caretaker_idx >= 0 && caretaker_idx < group_size) {
                vip_priority_flags[caretaker_idx] = 1;
            }
        }
    }

    int vip_slots = 0;
    for (int i = 0; i < group_size; i++) {
        if (vip_priority_flags[i]) {
            vip_slots++;
        }
    }
    int normal_slots = group_size - vip_slots;

    printf(CLR_GREEN "[PRZEWODNIK %d] Wsiadłem na prom jako pierwszy. Zapraszam grupę (%d osób, VIP-prio: %d)." CLR_RESET "\n", guide_id, group_size, vip_slots);

    for (int i = 0; i < vip_slots; i++) {
        sem_unlock(sem_id, SEM_FERRY_BOARD_VIP);
    }
    for (int i = 0; i < normal_slots; i++) {
        sem_unlock(sem_id, SEM_FERRY_BOARD);
    }

    printf(CLR_GREEN "[PRZEWODNIK %d] Czekam aż wszyscy wsiądą..." CLR_RESET "\n", guide_id);
    for (int i = 0; i < group_size; i++) {
        sem_lock(sem_id, SEM_FERRY_ALL_ABOARD);
    }

    printf(CLR_GREEN "[PRZEWODNIK %d] Wszyscy na pokładzie! Odpływamy." CLR_RESET "\n", guide_id);

    sem_lock(sem_id, SEM_PROM_MUTEX);
    park->ferry_position = destination;
    sem_unlock(sem_id, SEM_PROM_MUTEX);

    printf(CLR_GREEN "[PRZEWODNIK %d] Dopłynęliśmy na brzeg %d!" CLR_RESET "\n", guide_id, destination);

    for (int i = 0; i < group_size; i++) {
        sem_unlock(sem_id, SEM_FERRY_ARRIVE);
    }

    printf(CLR_GREEN "[PRZEWODNIK %d] Czekam aż wszyscy wysiądą..." CLR_RESET "\n", guide_id);
    for (int i = 0; i < group_size; i++) {
        sem_lock(sem_id, SEM_FERRY_DISEMBARK);
    }

    printf(CLR_GREEN "[PRZEWODNIK %d] Wszyscy wysiedli z promu." CLR_RESET "\n", guide_id);

    sem_lock(sem_id, SEM_PROM_MUTEX);
    park->ferry_passengers = 0;
    park->ferry_expected = 0;
    park->ferry_current_group = -1;
    sem_unlock(sem_id, SEM_PROM_MUTEX);

    sem_unlock(sem_id, SEM_FERRY_CAP);
    sem_unlock(sem_id, SEM_FERRY_CONTROL);
}

int main(int argc, char* argv[]) {

    if (argc < 2) {
        printf(CLR_RED "[PRZEWODNIK] Błąd: Brak ID przewodnika!" CLR_RESET "\n");
        exit(1);
    }

    int id = atoi(argv[1]);

    int shm_id = shmget(SHM_KEY_ID, sizeof(struct ParkSharedMemory), 0600);
    if (shm_id == -1) {
        fatal_error("[PRZEWODNIK] Błąd shmget");
    }

    struct ParkSharedMemory *park = (struct ParkSharedMemory*)shmat(shm_id, NULL, 0);
    if (park == (void*)-1) {
        fatal_error("[PRZEWODNIK] Błąd shmat");
    }

    int sem_id = semget(SEM_KEY_ID, TOTAL_SEMAPHORES, 0600);
    if (sem_id == -1) {
        fatal_error("[PRZEWODNIK] Błąd semget");
    }

    int msg_id = msgget(MSG_KEY_ID, 0600);
    if (msg_id == -1) {
        fatal_error("[PRZEWODNIK] Błąd msgget");
    }

    srand(time(NULL) + id * 100);

    struct sigaction sa_term;
    sa_term.sa_handler = sigterm_handler;
    sigemptyset(&sa_term.sa_mask);
    sa_term.sa_flags = 0;  

    if (sigaction(SIGTERM, &sa_term, NULL) == -1) {
        fatal_error("[PRZEWODNIK] Błąd sigaction SIGTERM");
    }

    printf(CLR_GREEN "[PRZEWODNIK %d] Melduję się w pracy! Czekam na grupy..." CLR_RESET "\n", id);

    while (!shutdown_flag) {

        printf(CLR_GREEN "[PRZEWODNIK %d] Czekam na grupę..." CLR_RESET "\n", id);

        if (sem_lock_interruptible(sem_id, SEM_PRZEWODNIK, &shutdown_flag) == -1) {

            printf(CLR_GREEN "[PRZEWODNIK %d] Przerwano czekanie na grupę - kończę pracę." CLR_RESET "\n", id);
            break;
        }

        if (shutdown_flag) {
            printf(CLR_GREEN "[PRZEWODNIK %d] Otrzymano sygnał shutdown po obudzeniu." CLR_RESET "\n", id);
            break;
        }

        printf(CLR_GREEN "[PRZEWODNIK %d] Obudzony! Czekam na wolny slot grupy..." CLR_RESET "\n", id);

        if (sem_lock_interruptible(sem_id, SEM_GROUP_SLOTS, &shutdown_flag) == -1) {
            printf(CLR_GREEN "[PRZEWODNIK %d] Przerwano czekanie na slot grupy - kończę pracę." CLR_RESET "\n", id);
            break;
        }

        if (shutdown_flag) {
            sem_unlock(sem_id, SEM_GROUP_SLOTS);
            printf(CLR_GREEN "[PRZEWODNIK %d] Shutdown po uzyskaniu slotu - zwalniam i kończę." CLR_RESET "\n", id);
            break;
        }

        printf(CLR_GREEN "[PRZEWODNIK %d] Slot grupy dostępny! Przydzielam grupę." CLR_RESET "\n", id);

        sem_lock(sem_id, SEM_GROUP_MUTEX);

        int group_slot = find_free_group_slot(park);
        if (group_slot == -1) {
            printf(CLR_RED "[PRZEWODNIK %d] Błąd: Mam slot ale find_free_group_slot zwrócił -1!" CLR_RESET "\n", id);
            sem_unlock(sem_id, SEM_GROUP_MUTEX);
            sem_unlock(sem_id, SEM_GROUP_SLOTS);
            continue;
        }

        struct GroupState *group = &park->groups[group_slot];

        sem_lock(sem_id, SEM_STATS_MUTEX);
        int all_entered = park->total_entered;
        int all_expected = park->total_expected;
        sem_unlock(sem_id, SEM_STATS_MUTEX);

        sem_lock(sem_id, SEM_QUEUE_MUTEX);

        int queue_size = park->people_in_queue;
        int actual_group_size = queue_size;

        if (queue_size < M_GROUP_SIZE) {
            if (all_entered == all_expected && queue_size > 0) {
                printf(CLR_GREEN "[PRZEWODNIK %d] Ostatnia niepełna grupa! Biorę %d osób." CLR_RESET "\n", id, queue_size);
            } else {
                printf(CLR_GREEN "[PRZEWODNIK %d] Fałszywy alarm - kolejka niepełna (%d). Rezygnuję." CLR_RESET "\n", id, queue_size);
                sem_unlock(sem_id, SEM_QUEUE_MUTEX);
                sem_unlock(sem_id, SEM_GROUP_MUTEX);
                sem_unlock(sem_id, SEM_GROUP_SLOTS);
                continue;
            }
        } else {
            actual_group_size = M_GROUP_SIZE;
        }

        for (int i = 0; i < M_GROUP_SIZE; i++) {
            group->member_is_caretaker[i] = 0;
            group->member_caretaker_of[i] = -1;
            group->member_has_caretaker[i] = -1;
            group->member_caretaker_is_guide[i] = 0;
            if (i >= actual_group_size) {
                group->member_pids[i] = 0;
                group->member_ids[i] = 0;
                group->member_ages[i] = 0;
                group->member_vips[i] = 0;
            }
        }

        for (int i = 0; i < actual_group_size; i++) {
            group->member_pids[i] = park->queue_pids[i];
            group->member_ids[i] = park->queue_ids[i];
            group->member_ages[i] = park->queue_ages[i];
            group->member_vips[i] = park->queue_vips[i];

            park->assigned_group_id[i] = group_slot;
            park->assigned_member_index[i] = i;
        }

        for (int i = 0; i < actual_group_size; i++) {
            if (group->member_ages[i] < 15) {
                for (int j = 0; j < actual_group_size; j++) {
                    if (group->member_ages[j] >= 18 && !group->member_is_caretaker[j]) {
                        group->member_is_caretaker[j] = 1;
                        group->member_caretaker_of[j] = i;
                        group->member_has_caretaker[i] = j;
                        if (group->member_ages[i] <= 5) {
                            printf(CLR_GREEN "[PRZEWODNIK %d] Turysta %d (wiek %d) jest opiekunem dziecka %d (wiek %d) - nie wejdą na wieżę" CLR_RESET "\n",
                                   id, group->member_ids[j], group->member_ages[j], group->member_ids[i], group->member_ages[i]);
                        } else {
                            printf(CLR_GREEN "[PRZEWODNIK %d] Turysta %d (wiek %d) jest opiekunem dziecka %d (wiek %d)" CLR_RESET "\n",
                                   id, group->member_ids[j], group->member_ages[j], group->member_ids[i], group->member_ages[i]);
                        }
                        break;
                    }
                }
            }
        }

        for (int i = 0; i < actual_group_size; i++) {
            if (group->member_ages[i] < 15 && group->member_has_caretaker[i] == -1) {
                group->member_caretaker_is_guide[i] = 1;
                printf(CLR_YELLOW "[PRZEWODNIK %d] Brak dorosłego opiekuna dla turysty %d (wiek %d) - przejmuję opiekę jako przewodnik." CLR_RESET "\n",
                       id, group->member_ids[i], group->member_ages[i]);
            }
        }

        sem_unlock(sem_id, SEM_QUEUE_MUTEX);

        {
            union semun reset_arg;
            reset_arg.val = 0;
            for (int i = 0; i < actual_group_size; i++) {
                if (semctl(sem_id, SEM_TOURIST_ASSIGNED(i), SETVAL, reset_arg) == -1) {
                    report_error("[PRZEWODNIK] Błąd semctl SEM_TOURIST_ASSIGNED");
                }
                if (semctl(sem_id, SEM_TOURIST_READ_DONE(i), SETVAL, reset_arg) == -1) {
                    report_error("[PRZEWODNIK] Błąd semctl SEM_TOURIST_READ_DONE");
                }
            }
        }

        for (int i = 0; i < actual_group_size; i++) {
            sem_unlock(sem_id, SEM_TOURIST_ASSIGNED(i));
        }

        printf(CLR_GREEN "[PRZEWODNIK %d] Czekam na potwierdzenie odczytu od turystów..." CLR_RESET "\n", id);
        for (int i = 0; i < actual_group_size; i++) {
            sem_lock(sem_id, SEM_TOURIST_READ_DONE(i));
        }

        sem_lock(sem_id, SEM_QUEUE_MUTEX);
        park->people_in_queue = 0;
        sem_unlock(sem_id, SEM_QUEUE_MUTEX);

        for(int i=0; i<actual_group_size; i++) {
            sem_unlock(sem_id, SEM_QUEUE_SLOTS);
        }

        printf(CLR_GREEN "[PRZEWODNIK %d] Przydzieliłem turystów do grupy %d" CLR_RESET "\n", id, group_slot);

        group->active = 1;
        group->guide_id = id;
        group->guide_pid = getpid();
        group->size = actual_group_size;
        union semun arg;
        arg.val = 0;
        if (semctl(sem_id, SEM_GROUP_DONE(group_slot), SETVAL, arg) == -1) {
            report_error("[PRZEWODNIK] Błąd semctl SEM_GROUP_DONE");
        }
        if (semctl(sem_id, SEM_BRIDGE_GUIDE_READY(group_slot), SETVAL, arg) == -1) {
            report_error("[PRZEWODNIK] Błąd semctl SEM_BRIDGE_GUIDE_READY");
        }
        for (int k = 0; k < M_GROUP_SIZE; k++) {
            if (semctl(sem_id, SEM_MEMBER_GO(group_slot, k), SETVAL, arg) == -1) {
                report_error("[PRZEWODNIK] Błąd semctl SEM_MEMBER_GO");
            }
        }
        group->route = (rand() % 2) + 1; 

        group->current_attraction = ATTR_NONE;
        group->attraction_step = 0;
        group->tourists_ready = 0;
        group->signal_tower_evacuate = 0;
        group->signal_emergency_exit = 0;

        sem_unlock(sem_id, SEM_GROUP_MUTEX);

        printf(CLR_GREEN "[PRZEWODNIK %d] Przejąłem grupę w slocie %d. Trasa: %d" CLR_RESET "\n", id, group_slot, group->route);

        printf(CLR_GREEN "[PRZEWODNIK %d] Skład grupy (%d osób): " CLR_RESET, id, group->size);
        for (int i = 0; i < group->size; i++) {
            printf(CLR_GREEN "T%d (W: %d%s) " CLR_RESET, group->member_ids[i], group->member_ages[i], group->member_vips[i] ? CLR_GREEN ", VIP" CLR_GREEN : "");
        }
        printf("\n");

        int has_young_children = 0;
        for (int i = 0; i < group->size; i++) {
            if (group->member_ages[i] < 12) {
                has_young_children = 1;
                break;
            }
        }
        if (has_young_children) {
            printf(CLR_GREEN "[PRZEWODNIK %d] Uwaga: grupa z dziećmi < 12 lat - czas wydłużony o 50%%" CLR_RESET "\n", id);
        }

        int emergency_before_start = 0;
        if ((rand() % 100) < 2) {
            printf(CLR_BG_RED CLR_WHITE "[PRZEWODNIK %d] Awaria przed startem!" CLR_RESET "\n", id);
            emergency_before_start = 1;
            send_emergency_exit(group, id);

            for (int k = 0; k < group->size; k++) {
                sem_unlock(sem_id, SEM_MEMBER_GO(group_slot, k));
            }

            int fifo_fd = open(FIFO_PATH, O_WRONLY);
            if (fifo_fd == -1) {
                if (errno != ENXIO) {
                    report_error("[PRZEWODNIK] Błąd open FIFO");
                }
            } else {
                char report[256];
                sprintf(report, "Przewodnik %d - awaria przed startem\n", id);
                if (write(fifo_fd, report, strlen(report)) == -1) {
                    report_error("[PRZEWODNIK] Błąd write FIFO (awaria)");
                }
                if (close(fifo_fd) == -1) {
                    report_error("[PRZEWODNIK] Błąd close FIFO (awaria)");
                }
            }

            printf(CLR_RED "[PRZEWODNIK %d] Czekam na turystów w trybie ewakuacji..." CLR_RESET "\n", id);
        }

        if (!emergency_before_start) {
            if (group->route == 1) {
                printf(CLR_BOLD CLR_GREEN "[PRZEWODNIK %d] Trasa: [Most] - [Wieża] - [Prom]" CLR_RESET "\n", id);
            } else {
                printf(CLR_BOLD CLR_GREEN "[PRZEWODNIK %d] Trasa: [Prom] - [Wieża] - [Most]" CLR_RESET "\n", id);
            }

            printf(CLR_GREEN "[PRZEWODNIK %d] Startujemy! Budzę turystów." CLR_RESET "\n", id);

            for (int k = 0; k < group->size; k++) {
                sem_unlock(sem_id, SEM_MEMBER_GO(group_slot, k));
            }

        }

        for (int step = 0; step < 3; step++) {
            int attraction = get_attraction_for_step(group->route, step);
            group->current_attraction = attraction;
            group->attraction_step = step;

            if (!emergency_before_start) {
                printf(CLR_GREEN "\n[PRZEWODNIK %d] FAZA %d: Atrakcja %d" CLR_RESET "\n", id, step + 1, attraction);

                switch (attraction) {
                    case ATTR_BRIDGE:

                        guide_enter_bridge(id, group_slot, group->size, get_bridge_direction(group->route), park, sem_id);
                        break;

                    case ATTR_TOWER:

                        printf(CLR_GREEN "[PRZEWODNIK %d] Czekam pod wieżą (nie wchodzę)." CLR_RESET "\n", id);

                        if ((rand() % 100) < 3) {
                            send_tower_evacuation(group, park, id);
                        }
                        break;

                    case ATTR_FERRY:

                        guide_take_ferry(id, group_slot, get_ferry_direction(group->route), park, sem_id, group->size);
                        break;
                }
            } else {
                printf(CLR_RED "\n[PRZEWODNIK %d] Ewakuacja! Pomijam atrakcję %d, czekam na turystów." CLR_RESET "\n", id, attraction);
            }

            printf(CLR_GREEN "[PRZEWODNIK %d] Czekam aż wszyscy turyści skończą atrakcję %d..." CLR_RESET "\n", id, attraction);

            for (int k = 0; k < group->size; k++) {
                sem_lock(sem_id, SEM_GROUP_DONE(group_slot));
            }

            printf(CLR_GREEN "[PRZEWODNIK %d] Wszyscy gotowi! Idziemy dalej." CLR_RESET "\n", id);

            group->tourists_ready = 0;

            if (step < 2) {
                union semun reset_arg;
                reset_arg.val = 0;
                if (semctl(sem_id, SEM_GROUP_DONE(group_slot), SETVAL, reset_arg) == -1) {
                    report_error("[PRZEWODNIK] Błąd semctl reset SEM_GROUP_DONE");
                }

                printf(CLR_GREEN "[PRZEWODNIK %d] Przechodzimy do następnej atrakcji." CLR_RESET "\n", id);

                int walk_time = 1;
                if (has_young_children) {
                    //walk_time = (int)(walk_time * 1.5) + 1;
                    printf(CLR_GREEN "[PRZEWODNIK %d] Wolniejsze tempo (dzieci) - %ds" CLR_RESET "\n", id, walk_time);
                }

                for (int k = 0; k < group->size; k++) {
                    sem_unlock(sem_id, SEM_MEMBER_GO(group_slot, k));
                }
            }
        }

        printf(CLR_GREEN "\n[PRZEWODNIK %d] Koniec wycieczki!" CLR_RESET "\n", id);
        printf(CLR_GREEN "[PRZEWODNIK %d] Odprowadzam grupę do kasy." CLR_RESET "\n", id);

        if (!emergency_before_start) {
            int fifo_fd = open(FIFO_PATH, O_WRONLY);
            if (fifo_fd == -1) {
                if (errno != ENXIO) {
                    report_error("[PRZEWODNIK] Błąd open FIFO");
                }
            } else {
                char report[256];
                sprintf(report, "Przewodnik %d zakończył wycieczkę (trasa %d, %d osób)\n", id, group->route, M_GROUP_SIZE);
                if (write(fifo_fd, report, strlen(report)) == -1) {
                    report_error("[PRZEWODNIK] Błąd write FIFO");
                }
                if (close(fifo_fd) == -1) {
                    report_error("[PRZEWODNIK] Błąd close FIFO");
                }
            }
        }

        group->active = 0;

        sem_unlock(sem_id, SEM_GROUP_SLOTS);

        sem_lock(sem_id, SEM_STATS_MUTEX);
        int check_entered = park->total_entered;
        int check_expected = park->total_expected;
        sem_unlock(sem_id, SEM_STATS_MUTEX);

        if (check_entered == check_expected) {
            sem_lock(sem_id, SEM_QUEUE_MUTEX);
            int check_queue = park->people_in_queue;
            sem_unlock(sem_id, SEM_QUEUE_MUTEX);
            if (check_queue > 0 && check_queue < M_GROUP_SIZE) {
                printf(CLR_GREEN "[PRZEWODNIK %d] Wykryłem niepełną ostatnią grupę (%d osób). Budzę przewodnika." CLR_RESET "\n", id, check_queue);
                sem_unlock(sem_id, SEM_PRZEWODNIK);
            }
        }

        printf(CLR_GREEN "[PRZEWODNIK %d] Zwolniłem slot grupy. Wracam do bazy." CLR_RESET "\n\n", id);

    }

    printf(CLR_GREEN "[PRZEWODNIK %d] Kończę pracę - otrzymano sygnał zakończenia." CLR_RESET "\n", id);

    shmdt(park);

    return 0;
}