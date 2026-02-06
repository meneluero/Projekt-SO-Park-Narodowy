#include "common.h"
#include <signal.h>
#include <fcntl.h>

// flaga do bezpiecznego konczenia pracy po otrzymaniu sygnalu
volatile sig_atomic_t shutdown_flag = 0;

// handler sygnalu sigterm - ustawia flage zakonczenia
void sigterm_handler(int sig) {
    (void)sig;  

    shutdown_flag = 1;
    char msg[] = "\n\033[1;31m[PRZEWODNIK] Otrzymano SIGTERM. Kończę pracę.\033[0m\n";
    if (write(STDOUT_FILENO, msg, sizeof(msg) - 1) == -1) {
        report_error("[PRZEWODNIK] Błąd write w handlerze SIGTERM");
    }
}

// specjalna funkcja dla procesu-reportera (przekazuje info o wyjsciu do kasy)
static int run_exit_reporter(void) {
    int report_msg_id = msgget(ftok(FTOK_PATH, FTOK_MSG_REPORT_ID), 0600);
    if (report_msg_id == -1) {
        fatal_error("[PRZEWODNIK-RAPORTER] Błąd msgget (report queue)");
    }

    int msg_id = msgget(ftok(FTOK_PATH, FTOK_MSG_ID), 0600);
    if (msg_id == -1) {
        fatal_error("[PRZEWODNIK-RAPORTER] Błąd msgget (main queue)");
    }

    printf(CLR_GREEN "[PRZEWODNIK-RAPORTER] Gotowy. Przekazuję wyjścia do kasy." CLR_RESET "\n");

    // petla odbierajaca powiadomienia od turystow i wysylajaca je do kasy
    while (1) {
        struct msg_buffer notice;
        // jesli flaga ustawiona, nie blokujemy zeby wyjsc z petli
        int flags = shutdown_flag ? IPC_NOWAIT : 0;

        if (msgrcv(report_msg_id, &notice, sizeof(notice) - sizeof(long), MSG_TYPE_EXIT_NOTICE, flags) == -1) {
            if (errno == EINTR) {
                if (shutdown_flag) {
                    printf(CLR_GREEN "[PRZEWODNIK-RAPORTER] Otrzymano SIGTERM, kończę pracę." CLR_RESET "\n");
                    continue;
                }
                continue;
            }
            if (shutdown_flag && errno == ENOMSG) {
                break; // wyjscie z petli przy zamykaniu
            }
            report_error("[PRZEWODNIK-RAPORTER] Błąd msgrcv (notice)");
            break;
        }

        // przygotowanie i wyslanie komunikatu do kasy
        struct msg_buffer exit_msg;
        exit_msg.msg_type = MSG_TYPE_EXIT;
        exit_msg.tourist_id = notice.tourist_id;
        exit_msg.tourist_pid = notice.tourist_pid;
        exit_msg.age = notice.age;
        exit_msg.is_vip = notice.is_vip;
        size_t info_len = strlen(notice.info);
        if (info_len >= sizeof(exit_msg.info)) {
            info_len = sizeof(exit_msg.info) - 1;
        }
        memcpy(exit_msg.info, notice.info, info_len);
        exit_msg.info[info_len] = '\0';

        if (msgsnd(msg_id, &exit_msg, sizeof(exit_msg) - sizeof(long), 0) == -1) {
            report_error("[PRZEWODNIK-RAPORTER] Błąd msgsnd (wyjście)");
        }
    }

    return 0;
}

// funkcja wysylajaca sygnal ewakuacji do czlonkow grupy
void send_emergency_exit(struct GroupState *group, int guide_id) {
    printf(CLR_RED "\n[PRZEWODNIK %d] Sytuacja awaryjna! Wysyłam SIGUSR2 do grupy!" CLR_RESET "\n", guide_id);

    group->signal_emergency_exit = 1;

    for (int i = 0; i < M_GROUP_SIZE; i++) {
        if (group->member_pids[i] > 0) {
            printf(CLR_RED "[PRZEWODNIK %d] SIGUSR2 -> [T %d | PID %d]" CLR_RESET "\n", guide_id, group->member_ids[i], group->member_pids[i]);
            if (kill(group->member_pids[i], SIGUSR2) == -1) {
                report_error("[PRZEWODNIK] Błąd kill SIGUSR2");
            }
        }
    }
    
    printf(CLR_RED "[PRZEWODNIK %d] Odprowadzam grupę bezpośrednio do kasy." CLR_RESET "\n", guide_id);
}

// funkcja wysylajaca sygnal ewakuacji z wiezy
void send_tower_evacuation(struct GroupState *group, struct ParkSharedMemory *park, int guide_id) {
    printf(CLR_RED "\n[PRZEWODNIK %d] Ewakuacja wieży! Wysyłam SIGUSR1!" CLR_RESET "\n", guide_id);

    group->signal_tower_evacuate = 1;

    for (int i = 0; i < M_GROUP_SIZE; i++) {
        pid_t pid = group->member_pids[i];
        if (pid > 0) {
            int on_tower = tower_has_visitor(park, pid);
            printf(CLR_RED "[PRZEWODNIK %d] SIGUSR1 -> [T %d | PID %d] (%s)" CLR_RESET "\n", guide_id, group->member_ids[i], pid, on_tower ? "na wieży" : "czeka");
            if (kill(pid, SIGUSR1) == -1) {
                report_error("[PRZEWODNIK] Błąd kill SIGUSR1");
            }
        }
    }
}

// funkcja pomocnicza do wysylania listy obecnosci przy wyjsciu
static void send_exit_list_to_cashier(struct GroupState *group, int msg_id) {
    for (int i = 0; i < group->size; i++) {
        struct msg_buffer exit_msg;
        exit_msg.msg_type = MSG_TYPE_EXIT;
        exit_msg.tourist_id = group->member_ids[i];
        exit_msg.tourist_pid = group->member_pids[i];
        exit_msg.age = group->member_ages[i];
        exit_msg.is_vip = group->member_vips[i];

        char timestamp[20];
        get_timestamp(timestamp, sizeof(timestamp));
        size_t len = strlen(timestamp);
        if (len >= sizeof(exit_msg.info)) {
            len = sizeof(exit_msg.info) - 1;
        }
        memcpy(exit_msg.info, timestamp, len);
        exit_msg.info[len] = '\0';

        if (msgsnd(msg_id, &exit_msg, sizeof(exit_msg) - sizeof(long), 0) == -1) {
            report_error("[PRZEWODNIK] Błąd msgsnd (wyjście turysty)");
        }
    }
}

// szukanie wolnego miejsca w tablicy grup w pamieci dzielonej
int find_free_group_slot(struct ParkSharedMemory *park) {
    for (int i = 0; i < MAX_GROUPS; i++) {
        if (!park->groups[i].active) {
            return i;
        }
    }
    return -1; 

}

// logika sterowania ruchem na moscie przez przewodnika
void guide_enter_bridge(int guide_id, int group_slot, int group_size, int direction, struct ParkSharedMemory *park, int sem_id) {
    printf(CLR_GREEN "[PRZEWODNIK %d] Podchodzę do mostu (kierunek: %s)" CLR_RESET "\n", guide_id, direction == DIR_KA ? "K->A" : "A->K");

    sem_lock(sem_id, SEM_MOST_MUTEX);

    // sprawdzenie czy most jest wolny lub ma zgodny kierunek
    if (park->bridge_direction == DIR_NONE || park->bridge_direction == direction) {

        park->bridge_direction = direction;
        park->bridge_on_bridge++;
        sem_unlock(sem_id, SEM_MOST_MUTEX);

        // czekanie na miejsce na moscie
        sem_lock(sem_id, SEM_MOST_LIMIT);

        printf(CLR_GREEN "[PRZEWODNIK %d] Wchodzę na most pierwszy! (osób na moście: %d)" CLR_RESET "\n", guide_id, park->bridge_on_bridge);
    } else {

        // jesli zly kierunek, czekamy na zmiane
        park->bridge_waiting[direction]++;
        printf(CLR_GREEN "[PRZEWODNIK %d] Most zajęty w przeciwnym kierunku. Czekam..." CLR_RESET "\n", guide_id);
        sem_unlock(sem_id, SEM_MOST_MUTEX);

        sem_lock(sem_id, SEM_BRIDGE_WAIT(direction));

        // po obudzeniu czekamy na miejsce
        sem_lock(sem_id, SEM_MOST_LIMIT);

        sem_lock(sem_id, SEM_MOST_MUTEX);
        park->bridge_on_bridge++;
        int count = park->bridge_on_bridge;
        sem_unlock(sem_id, SEM_MOST_MUTEX);

        printf(CLR_GREEN "[PRZEWODNIK %d] Obudzony! Wchodzę na most. (osób: %d)" CLR_RESET "\n", guide_id, count);
    }

    // wpuszczenie grupy na most
    for (int i = 0; i < group_size; i++) {
        sem_unlock(sem_id, SEM_BRIDGE_GUIDE_READY(group_slot));
    }

    printf(CLR_GREEN "[PRZEWODNIK %d] Przechodzę przez most..." CLR_RESET "\n", guide_id);
    sim_sleep(BRIDGE_CROSS_TIME_MIN, BRIDGE_CROSS_TIME_MAX, 0);

    // zejscie z mostu
    sem_unlock(sem_id, SEM_MOST_LIMIT);

    printf(CLR_GREEN "[PRZEWODNIK %d] Zszedłem z mostu." CLR_RESET "\n", guide_id);

    // logika zmiany kierunku mostu jesli nikt nie zostal
    int other_dir = 1 - direction;
    sem_lock(sem_id, SEM_MOST_MUTEX);
    park->bridge_on_bridge--;

    if (park->bridge_on_bridge == 0) {
        if (park->bridge_waiting[other_dir] > 0) {
            // zmiana kierunku i budzenie czekajacych z naprzeciwka
            park->bridge_direction = other_dir;
            int to_wake = park->bridge_waiting[other_dir];
            park->bridge_waiting[other_dir] = 0;

            for (int i = 0; i < to_wake; i++) {
                sem_unlock(sem_id, SEM_BRIDGE_WAIT(other_dir));
            }
            
            sem_unlock(sem_id, SEM_MOST_MUTEX);
            
        } else if (park->bridge_waiting[direction] == 0) {
            // reset kierunku jesli nikt nie czeka
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

// logika sterowania promem przez przewodnika
void guide_take_ferry(int guide_id, int group_slot, int destination, struct ParkSharedMemory *park, int sem_id, int group_size) {

    printf(CLR_GREEN "[PRZEWODNIK %d] Podchodzę do promu (kierunek: %d)" CLR_RESET "\n", guide_id, destination);

    // wejscie na prom
    if (ferry_enter(park, sem_id, destination, 0, NULL) == -1) {
        printf(CLR_RED "[PRZEWODNIK %d] Przerwano wejście na prom." CLR_RESET "\n", guide_id);
        return;
    }

    {
        union semun reset_arg;
        reset_arg.val = 0;
        if (semctl(sem_id, SEM_FERRY_GUIDE_READY(group_slot), SETVAL, reset_arg) == -1) {
            report_error("[PRZEWODNIK] Błąd semctl SEM_FERRY_GUIDE_READY");
        }
    }

    // wpuszczenie grupy na prom
    printf(CLR_GREEN "[PRZEWODNIK %d] Wsiadłem na prom jako pierwszy. Zapraszam grupę (%d osób)." CLR_RESET "\n", guide_id, group_size);
    for (int i = 0; i < group_size; i++) {
        sem_unlock(sem_id, SEM_FERRY_GUIDE_READY(group_slot));
    }

    printf(CLR_GREEN "[PRZEWODNIK %d] Prom płynie w kierunku %d." CLR_RESET "\n", guide_id, destination);
    sim_sleep(FERRY_TRAVEL_TIME_MIN, FERRY_TRAVEL_TIME_MAX, 0);

    // zejscie z promu
    ferry_leave(park, sem_id, destination);
    printf(CLR_GREEN "[PRZEWODNIK %d] Dopłynąłem i schodzę z promu." CLR_RESET "\n", guide_id);
}

int main(int argc, char* argv[]) {

    if (argc < 2) {
        printf(CLR_RED "[PRZEWODNIK] Błąd: Brak ID przewodnika!" CLR_RESET "\n");
        exit(1);
    }

    // jesli argument "reporter" - uruchomienie trybu specjalnego
    if (strcmp(argv[1], "reporter") == 0) {
        struct sigaction sa_term;
        sa_term.sa_handler = sigterm_handler;
        if (sigemptyset(&sa_term.sa_mask) == -1) {
            fatal_error("[PRZEWODNIK-RAPORTER] Błąd sigemptyset(SIGTERM)");
        }
        sa_term.sa_flags = 0;

        if (sigaction(SIGTERM, &sa_term, NULL) == -1) {
            fatal_error("[PRZEWODNIK-RAPORTER] Błąd sigaction SIGTERM");
        }

        return run_exit_reporter();
    }

    int id = atoi(argv[1]);

    // polaczenie z pamiecia dzielona i semaforami
    int shm_id = shmget(ftok(FTOK_PATH, FTOK_SHM_ID), sizeof(struct ParkSharedMemory), 0600);
    if (shm_id == -1) {
        fatal_error("[PRZEWODNIK] Błąd shmget");
    }

    struct ParkSharedMemory *park = (struct ParkSharedMemory*)shmat(shm_id, NULL, 0);
    if (park == (void*)-1) {
        fatal_error("[PRZEWODNIK] Błąd shmat");
    }

    int sem_id = semget(ftok(FTOK_PATH, FTOK_SEM_ID), TOTAL_SEMAPHORES, 0600);
    if (sem_id == -1) {
        fatal_error("[PRZEWODNIK] Błąd semget");
    }

    int msg_id = msgget(ftok(FTOK_PATH, FTOK_MSG_ID), 0600);
    if (msg_id == -1) {
        fatal_error("[PRZEWODNIK] Błąd msgget");
    }

    time_t seed_time = time(NULL);
    if (seed_time == (time_t)-1) {
        report_error("[PRZEWODNIK] Błąd time (srand seed)");
        srand(id * 100); // uzyj id jako seed
    } else {
        srand(seed_time + id * 100);
    }

    // rejestracja obslugi sigterm
    struct sigaction sa_term;
    sa_term.sa_handler = sigterm_handler;
    if (sigemptyset(&sa_term.sa_mask) == -1) {
        fatal_error("[PRZEWODNIK] Błąd sigemptyset(SIGTERM)");
    }
    sa_term.sa_flags = 0;  

    if (sigaction(SIGTERM, &sa_term, NULL) == -1) {
        fatal_error("[PRZEWODNIK] Błąd sigaction SIGTERM");
    }

    printf(CLR_GREEN "[PRZEWODNIK %d] Melduję się w pracy! Czekam na grupy..." CLR_RESET "\n", id);

    // glowna petla pracy przewodnika
    while (!shutdown_flag) {

        printf(CLR_GREEN "[PRZEWODNIK %d] Czekam na grupę..." CLR_RESET "\n", id);

        // oczekiwanie na semaforze (budzenie przez turyste lub kase)
        if (sem_lock_interruptible(sem_id, SEM_PRZEWODNIK, &shutdown_flag) == -1) {

            printf(CLR_GREEN "[PRZEWODNIK %d] Przerwano czekanie na grupę - kończę pracę." CLR_RESET "\n", id);
            break;
        }

        if (shutdown_flag) {
            printf(CLR_GREEN "[PRZEWODNIK %d] Otrzymano sygnał shutdown po obudzeniu." CLR_RESET "\n", id);
            break;
        }

        printf(CLR_GREEN "[PRZEWODNIK %d] Obudzony! Czekam na wolny slot grupy..." CLR_RESET "\n", id);

        // zajmowanie slotu na grupe
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
        int daily_limit_reached = (park->daily_entered_count >= park->daily_visitor_limit);
        sem_unlock(sem_id, SEM_STATS_MUTEX);

        sem_lock(sem_id, SEM_QUEUE_MUTEX);

        int queue_size = park->people_in_queue;
        int actual_group_size = queue_size;

        // obsluga niepelnej grupy pod koniec dzialania parku lub po osiagnieciu limitu dziennego
        if (queue_size < M_GROUP_SIZE) {
            if ((all_entered == all_expected || park->park_closed || daily_limit_reached) && queue_size > 0) {
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

        // czyszczenie struktury grupy
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

        // pobieranie danych turystow z kolejki glownej
        for (int i = 0; i < actual_group_size; i++) {
            int idx = (park->queue_head + i) % N_PARK_CAPACITY;
            group->member_pids[i] = park->queue_pids[idx];
            group->member_ids[i] = park->queue_ids[idx];
            group->member_ages[i] = park->queue_ages[idx];
            group->member_vips[i] = park->queue_vips[idx];

            park->assigned_group_id[idx] = group_slot;
            park->assigned_member_index[idx] = i;
        }

        // przydzielanie opiekunow dla dzieci ponizej 15 lat
        for (int i = 0; i < actual_group_size; i++) {
            if (group->member_ages[i] < 15) {
                for (int j = 0; j < actual_group_size; j++) {
                    if (group->member_ages[j] >= 18 && !group->member_is_caretaker[j]) {
                        group->member_is_caretaker[j] = 1;
                        group->member_caretaker_of[j] = i;
                        group->member_has_caretaker[i] = j;
                        if (group->member_ages[i] <= 5) {
                            printf(CLR_GREEN "[PRZEWODNIK %d] [T %d | PID %d] (wiek %d) jest opiekunem dziecka [T %d | PID %d] (wiek %d) - nie wejdą na wieżę" CLR_RESET "\n",
                                   id, group->member_ids[j], group->member_pids[j], group->member_ages[j], group->member_ids[i], group->member_pids[i], group->member_ages[i]);
                        } else {
                            printf(CLR_GREEN "[PRZEWODNIK %d] [T %d | PID %d] (wiek %d) jest opiekunem dziecka [T %d | PID %d] (wiek %d)" CLR_RESET "\n",
                                   id, group->member_ids[j], group->member_pids[j], group->member_ages[j], group->member_ids[i], group->member_pids[i], group->member_ages[i]);
                        }
                        break;
                    }
                }
            }
        }

        // jesli brakuje opiekunow, przewodnik przejmuje role opiekuna
        for (int i = 0; i < actual_group_size; i++) {
            if (group->member_ages[i] < 15 && group->member_has_caretaker[i] == -1) {
                group->member_caretaker_is_guide[i] = 1;
                printf(CLR_YELLOW "[PRZEWODNIK %d] Brak dorosłego opiekuna dla [T %d | PID %d] (wiek %d) - przejmuję opiekę jako przewodnik." CLR_RESET "\n",
                       id, group->member_ids[i], group->member_pids[i], group->member_ages[i]);
            }
        }

        sem_unlock(sem_id, SEM_QUEUE_MUTEX);

        // reset semaforow synchronizacji z turystami
        {
            union semun reset_arg;
            reset_arg.val = 0;
            for (int i = 0; i < actual_group_size; i++) {
                int idx = (park->queue_head + i) % N_PARK_CAPACITY;
                if (semctl(sem_id, SEM_TOURIST_ASSIGNED(idx), SETVAL, reset_arg) == -1) {
                    report_error("[PRZEWODNIK] Błąd semctl SEM_TOURIST_ASSIGNED");
                }
                if (semctl(sem_id, SEM_TOURIST_READ_DONE(idx), SETVAL, reset_arg) == -1) {
                    report_error("[PRZEWODNIK] Błąd semctl SEM_TOURIST_READ_DONE");
                }
            }
        }

        // powiadomienie turystow ze zostali przypisani
        for (int i = 0; i < actual_group_size; i++) {
            int idx = (park->queue_head + i) % N_PARK_CAPACITY;
            sem_unlock(sem_id, SEM_TOURIST_ASSIGNED(idx));
        }

        // czekanie na potwierdzenie odczytu przez turystow
        printf(CLR_GREEN "[PRZEWODNIK %d] Czekam na potwierdzenie odczytu od turystów..." CLR_RESET "\n", id);
        for (int i = 0; i < actual_group_size; i++) {
            int idx = (park->queue_head + i) % N_PARK_CAPACITY;
            sem_lock(sem_id, SEM_TOURIST_READ_DONE(idx));
        }

        // aktualizacja kolejki glownej
        sem_lock(sem_id, SEM_QUEUE_MUTEX);
        park->queue_head = (park->queue_head + actual_group_size) % N_PARK_CAPACITY;
        park->people_in_queue -= actual_group_size;
        sem_unlock(sem_id, SEM_QUEUE_MUTEX);

        // zwolnienie miejsc w kolejce
        for(int i=0; i<actual_group_size; i++) {
            sem_unlock(sem_id, SEM_QUEUE_SLOTS);
        }

        printf(CLR_GREEN "[PRZEWODNIK %d] Przydzieliłem turystów do grupy %d" CLR_RESET "\n", id, group_slot);

        // inicjalizacja stanu grupy
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
        if (semctl(sem_id, SEM_FERRY_GUIDE_READY(group_slot), SETVAL, arg) == -1) {
            report_error("[PRZEWODNIK] Błąd semctl SEM_FERRY_GUIDE_READY");
        }
        for (int k = 0; k < M_GROUP_SIZE; k++) {
            if (semctl(sem_id, SEM_MEMBER_GO(group_slot, k), SETVAL, arg) == -1) {
                report_error("[PRZEWODNIK] Błąd semctl SEM_MEMBER_GO");
            }
        }
        group->route = (rand() % 2) + 1; // losowanie trasy

        group->current_attraction = ATTR_NONE;
        group->attraction_step = 0;
        group->tourists_ready = 0;
        group->signal_tower_evacuate = 0;
        group->signal_emergency_exit = 0;

        sem_unlock(sem_id, SEM_GROUP_MUTEX);

        printf(CLR_GREEN "[PRZEWODNIK %d] Przejąłem grupę w slocie %d. Trasa: %d" CLR_RESET "\n", id, group_slot, group->route);

        printf(CLR_GREEN "[PRZEWODNIK %d] Skład grupy (%d osób): " CLR_RESET, id, group->size);
        for (int i = 0; i < group->size; i++) {
            printf(CLR_GREEN "[T %d | PID %d] (W: %d%s) " CLR_RESET, group->member_ids[i], group->member_pids[i], group->member_ages[i], group->member_vips[i] ? CLR_GREEN ", VIP" CLR_GREEN : "");
        }
        printf("\n");

        // sprawdzenie czy sa male dzieci - wplywa na czas chodzenia
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

        // losowa awaria przed startem
        int emergency_before_start = 0;
        if ((rand() % 100) < 2) {
            printf(CLR_BG_RED CLR_WHITE "[PRZEWODNIK %d] Awaria przed startem!" CLR_RESET "\n", id);
            emergency_before_start = 1;
            send_emergency_exit(group, id);

            for (int k = 0; k < group->size; k++) {
                sem_unlock(sem_id, SEM_MEMBER_GO(group_slot, k));
            }

            // raportowanie awarii przez fifo
            int fifo_fd = open(FIFO_PATH, O_WRONLY);
            if (fifo_fd == -1) {
                if (errno != ENXIO) {
                    report_error("[PRZEWODNIK] Błąd open FIFO");
                }
            } else {
                char report[256];
                int written = snprintf(report, sizeof(report), "Przewodnik %d - awaria przed startem\n", id);
                if (written < 0) {
                    report_error("[PRZEWODNIK] Błąd snprintf (raport awarii)");
                } else {
                    size_t report_len = (size_t)written;
                    if (report_len >= sizeof(report)) {
                        report_len = sizeof(report) - 1;
                    }
                    if (write(fifo_fd, report, report_len) == -1) {
                        report_error("[PRZEWODNIK] Błąd write FIFO (awaria)");
                    }
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

            //sim_sleep(WALK_TIME_MIN, WALK_TIME_MAX, has_young_children);

            // rozpoczecie wycieczki
            for (int k = 0; k < group->size; k++) {
                sem_unlock(sem_id, SEM_MEMBER_GO(group_slot, k));
            }

        }

        // petla zwiedzania - 3 atrakcje
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
                        break;

                    case ATTR_FERRY:

                        guide_take_ferry(id, group_slot, get_ferry_direction(group->route), park, sem_id, group->size);
                        break;
                }
            } else {
                printf(CLR_RED "\n[PRZEWODNIK %d] Ewakuacja! Pomijam atrakcję %d, czekam na turystów." CLR_RESET "\n", id, attraction);
            }

            printf(CLR_GREEN "[PRZEWODNIK %d] Czekam aż wszyscy turyści skończą atrakcję %d..." CLR_RESET "\n", id, attraction);

            // losowa ewakuacja wiezy
            if (!emergency_before_start && attraction == ATTR_TOWER && group->size > 1 && (rand() % 100) < 3) {
                sem_lock(sem_id, SEM_GROUP_DONE(group_slot));
                send_tower_evacuation(group, park, id);
                for (int k = 1; k < group->size; k++) {
                    sem_lock(sem_id, SEM_GROUP_DONE(group_slot));
                }
            } else {
                // czekanie na wszystkich czlonkow grupy
                for (int k = 0; k < group->size; k++) {
                    sem_lock(sem_id, SEM_GROUP_DONE(group_slot));
                }
            }

            printf(CLR_GREEN "[PRZEWODNIK %d] Wszyscy gotowi! Idziemy dalej." CLR_RESET "\n", id);

            group->tourists_ready = 0;

            if (step < 2) {
                union semun reset_arg;
                reset_arg.val = 0;
                if (semctl(sem_id, SEM_GROUP_DONE(group_slot), SETVAL, reset_arg) == -1) {
                    report_error("[PRZEWODNIK] Błąd semctl reset SEM_GROUP_DONE");
                }

                // losowa awaria w trakcie trasy
                if (!emergency_before_start && (rand() % 100) < 2) {
                    printf(CLR_BG_RED CLR_WHITE "[PRZEWODNIK %d] Awaria! Ewakuacja w trakcie trasy (po atrakcji %d)!" CLR_RESET "\n", id, attraction);
                    emergency_before_start = 1;
                    send_emergency_exit(group, id);
                }

                printf(CLR_GREEN "[PRZEWODNIK %d] Przechodzimy do następnej atrakcji." CLR_RESET "\n", id);

                // symulacja czasu przejscia
                sim_sleep(WALK_TIME_MIN, WALK_TIME_MAX, has_young_children);
                if (has_young_children) {
                    printf(CLR_GREEN "[PRZEWODNIK %d] Wolniejsze tempo (dzieci <12 w grupie - czas +50%%)" CLR_RESET "\n", id);
                }

                for (int k = 0; k < group->size; k++) {
                    sem_unlock(sem_id, SEM_MEMBER_GO(group_slot, k));
                }
            }
        }

        printf(CLR_GREEN "\n[PRZEWODNIK %d] Koniec wycieczki!" CLR_RESET "\n", id);
        printf(CLR_GREEN "[PRZEWODNIK %d] Odprowadzam grupę do kasy." CLR_RESET "\n", id);

        //sim_sleep(WALK_TIME_MIN, WALK_TIME_MAX, has_young_children);

        // raportowanie wyjscia do kasy
        send_exit_list_to_cashier(group, msg_id);

        // zapis do fifo
        if (!emergency_before_start) {
            int fifo_fd = open(FIFO_PATH, O_WRONLY);
            if (fifo_fd == -1) {
                if (errno != ENXIO) {
                    report_error("[PRZEWODNIK] Błąd open FIFO");
                }
            } else {
                char report[256];
                int written = snprintf(report, sizeof(report), "Przewodnik %d zakończył wycieczkę (trasa %d, %d osób)\n", id, group->route, group->size);
                if (written < 0) {
                    report_error("[PRZEWODNIK] Błąd snprintf (raport zakończenia)");
                } else {
                    size_t report_len = (size_t)written;
                    if (report_len >= sizeof(report)) {
                        report_len = sizeof(report) - 1;
                    }
                    if (write(fifo_fd, report, report_len) == -1) {
                        report_error("[PRZEWODNIK] Błąd write FIFO");
                    }
                }
                if (close(fifo_fd) == -1) {
                    report_error("[PRZEWODNIK] Błąd close FIFO");
                }
            }
        }

        group->active = 0;

        sem_unlock(sem_id, SEM_GROUP_SLOTS);

        // sprawdzenie czy nie ma niedobitkow w kolejce na koniec dnia
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

    if (shmdt(park) == -1) {
        report_error("[PRZEWODNIK] Błąd shmdt");
    }

    return 0;
}
