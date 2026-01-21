#include "common.h"
#include <signal.h>
#include <fcntl.h>

void send_emergency_exit(struct GroupState *group, int guide_id) {
    printf("\n[PRZEWODNIK %d] Sytuacja awaryjna! Wysyłam SIGUSR2 do grupy!\n", guide_id);
    
    group->signal_emergency_exit = 1;
    
    for (int i = 0; i < M_GROUP_SIZE; i++) {
        if (group->member_pids[i] > 0) {
            printf("[PRZEWODNIK %d] SIGUSR2 -> Turysta PID=%d\n", guide_id, group->member_pids[i]);
            kill(group->member_pids[i], SIGUSR2);
        }
    }
    
    //usleep(300000);  // 300ms na reakcje
    printf("[PRZEWODNIK %d] Odprowadzam grupę bezpośrednio do kasy.\n", guide_id);
}

void send_tower_evacuation(struct GroupState *group, struct ParkSharedMemory *park, int guide_id) {
    printf("\n[PRZEWODNIK %d] Ewakuacja wieży! Wysyłam SIGUSR1!\n", guide_id);
    
    group->signal_tower_evacuate = 1;
    
    // wyslij sigusr1 do wszystkich turystow ktorzy sa na wiezy
    for (int i = 0; i < M_GROUP_SIZE; i++) {
        pid_t pid = group->member_pids[i];
        if (pid > 0 && tower_has_visitor(park, pid)) {
            printf("[PRZEWODNIK %d] SIGUSR1 -> Turysta PID=%d (na wieży)\n", guide_id, pid);
            kill(pid, SIGUSR1);
        }
    }
}

// znajdz wolny slot grupy
int find_free_group_slot(struct ParkSharedMemory *park) {
    for (int i = 0; i < MAX_GROUPS; i++) {
        if (!park->groups[i].active) {
            return i;
        }
    }
    return -1; // brak wolnego slotu
}

// przewodnik przechodzi przez most
void guide_cross_bridge(int guide_id, int direction, struct ParkSharedMemory *park, int sem_id) {
    
    printf("[PRZEWODNIK %d] Podchodzę do mostu jako pierwszy (kier: %s)\n", guide_id, direction == DIR_KA ? "K->A" : "A->K");
    
    // czekaj na odpowiedni kierunek
    int can_enter = 0;
    while (!can_enter) {
        sem_lock(sem_id, SEM_MOST_MUTEX);
        
        if (park->bridge_direction == DIR_NONE || 
            park->bridge_direction == direction) {
            
            // probujemy zarezerwowac
            if (sem_trylock(sem_id, SEM_MOST_LIMIT) == 0) {
                park->bridge_direction = direction;
                park->bridge_current_count++;
                park->bridge_crossing[direction]++;
                can_enter = 1;
                printf("[PRZEWODNIK %d] Wchodzę na most pierwszy!\n", guide_id);
            }
        }
        
        sem_unlock(sem_id, SEM_MOST_MUTEX);
        
        if (!can_enter) {
            //usleep(100000);
        }
    }
    
    // przechodzenie
    printf("[PRZEWODNIK %d] Przechodzę przez most...\n", guide_id);
    //sleep(BRIDGE_CROSS_TIME);
    
    // schodzenie
    sem_lock(sem_id, SEM_MOST_MUTEX);
    park->bridge_current_count--;
    park->bridge_crossing[direction]--;
    if (park->bridge_crossing[direction] == 0 && park->bridge_crossing[1-direction] == 0) {
        park->bridge_direction = DIR_NONE;
    }
    sem_unlock(sem_id, SEM_MOST_MUTEX);
    
    sem_unlock(sem_id, SEM_MOST_LIMIT);
    
    printf("[PRZEWODNIK %d] Zszedłem z mostu. Czekam na grupę.\n", guide_id);
}

// przewodnik wsiada na prom
void guide_take_ferry(int guide_id, int group_id, int destination, struct ParkSharedMemory *park, int sem_id) {
    
    int my_shore = 1 - destination;
    
    printf("[PRZEWODNIK %d] Podchodzę do promu jako pierwszy (z brzegu %d)\n", guide_id, my_shore);
    
    // czekamy na prom
    int on_ferry = 0;
    while (!on_ferry) {
        sem_lock(sem_id, SEM_PROM_MUTEX);
        
        // SYTUACJA 1: Prom jest u mnie, pusty i wolny -> WSIADAM
        if (park->ferry_position == my_shore && !park->ferry_moving && park->ferry_current_count == 0 && park->ferry_group_id == -1) {       
            if (sem_trylock(sem_id, SEM_PROM_LIMIT) == 0) {
                park->ferry_group_id = group_id;
                park->ferry_current_count++;
                on_ferry = 1;
                printf("[PRZEWODNIK %d] Wsiadłem na prom pierwszy! Rezerwacja dla grupy %d.\n", guide_id, group_id);
            }
        }
        else if (park->ferry_position != my_shore && !park->ferry_moving && park->ferry_current_count == 0 && park->ferry_group_id == -1) {
            
            printf("[PRZEWODNIK %d] Prom na drugim brzegu. Wołam go!\n", guide_id);
            park->ferry_moving = 1; // zaznaczam ze prom plynie
            sem_unlock(sem_id, SEM_PROM_MUTEX);
            
            // symulacja czasu przeplyniecia
            //sleep(FERRY_TRAVEL_TIME);
            
            sem_lock(sem_id, SEM_PROM_MUTEX);
            park->ferry_position = my_shore; // prom przyplynal do mnie
            park->ferry_moving = 0;
            printf("[PRZEWODNIK %d] Prom przypłynął na mój brzeg.\n", guide_id);
            // nie robimy unlock tutaj
        }
        
        sem_unlock(sem_id, SEM_PROM_MUTEX);
        
        if (!on_ferry) {
            //usleep(200000);
        }
    }
    
    // plyniecie
    printf("[PRZEWODNIK %d] Płynę promem...\n", guide_id);
    //sleep(FERRY_TRAVEL_TIME);
    
    // wysiadanie
    sem_lock(sem_id, SEM_PROM_MUTEX);
    park->ferry_current_count--;
    // aktualizacja pozycji promu
    sem_unlock(sem_id, SEM_PROM_MUTEX);
    
    sem_unlock(sem_id, SEM_PROM_LIMIT);
    
    printf("[PRZEWODNIK %d] Wysiadłem z promu. Czekam na grupę.\n", guide_id);
}

int main(int argc, char* argv[]) {
    // walidacja argumentow
    if (argc < 2) {
        printf("[PRZEWODNIK] Błąd: Brak ID przewodnika!\n");
        exit(1);
    }
    
    int id = atoi(argv[1]);
    
    // polaczenie z zasobami ipc
    int shm_id = shmget(SHM_KEY_ID, sizeof(struct ParkSharedMemory), 0600);
    if (shm_id == -1) {
        perror("[PRZEWODNIK] Błąd shmget");
        exit(1);
    }
    
    struct ParkSharedMemory *park = (struct ParkSharedMemory*)shmat(shm_id, NULL, 0);
    if (park == (void*)-1) {
        perror("[PRZEWODNIK] Błąd shmat");
        exit(1);
    }
    
    int sem_id = semget(SEM_KEY_ID, TOTAL_SEMAPHORES, 0600);
    if (sem_id == -1) {
        perror("[PRZEWODNIK] Błąd semget");
        exit(1);
    }
    
    int msg_id = msgget(MSG_KEY_ID, 0600);
    if (msg_id == -1) {
        perror("[PRZEWODNIK] Błąd msgget");
        exit(1);
    }
    
    // inicjalizacja generatora losowego
    srand(time(NULL) + id * 100);
    
    printf("[PRZEWODNIK %d] Melduję się w pracy! Czekam na grupy...\n", id);
    
    // glowna petla przewodnika
    while (1) {
        // czekanie na skompletowanie grupy
        printf("[PRZEWODNIK %d] Czekam na grupę...\n", id);
        sem_lock(sem_id, SEM_PRZEWODNIK);
        
        printf("[PRZEWODNIK %d] Obudzony! Przydzielam grupę.\n", id);
        
        // przydzielanie grupy do slotu
        sem_lock(sem_id, SEM_GROUP_MUTEX);
        
        // znajdz wolny slot
        int group_slot = find_free_group_slot(park);
        if (group_slot == -1) {
            printf("[PRZEWODNIK %d] Błąd: Brak wolnego slotu grupy!\n", id);
            sem_unlock(sem_id, SEM_GROUP_MUTEX);
            continue;
        }
        
        struct GroupState *group = &park->groups[group_slot];
        
        // kopiuj dane turystow z kolejki do grupy
        sem_lock(sem_id, SEM_QUEUE_MUTEX);
        
        for (int i = 0; i < M_GROUP_SIZE; i++) {
            group->member_pids[i] = park->queue_pids[i];
            group->member_ids[i] = park->queue_ids[i];
            group->member_ages[i] = park->queue_ages[i];
            group->member_vips[i] = park->queue_vips[i];
        }
        
        sem_unlock(sem_id, SEM_QUEUE_MUTEX);
        
        // ustaw dane grupy
        group->active = 1;
        group->guide_id = id;
        group->guide_pid = getpid();
        union semun arg;
        arg.val = 0;
        semctl(sem_id, SEM_GROUP_START(group_slot), SETVAL, arg);
        semctl(sem_id, SEM_GROUP_DONE(group_slot), SETVAL, arg);
        group->route = (rand() % 2) + 1; // losowa trasa 1 lub 2
        group->current_attraction = ATTR_NONE;
        group->attraction_step = 0;
        group->tourists_ready = 0;
        group->signal_tower_evacuate = 0;
        group->signal_emergency_exit = 0;
        
        sem_unlock(sem_id, SEM_GROUP_MUTEX);
        
        printf("[PRZEWODNIK %d] Przejąłem grupę w slocie %d. Trasa: %d\n", id, group_slot, group->route);
        
        // Wypisz sklad grupy
        printf("[PRZEWODNIK %d] Skład grupy: ", id);
        for (int i = 0; i < M_GROUP_SIZE; i++) {
            printf("T%d(w:%d%s) ", group->member_ids[i], group->member_ages[i], group->member_vips[i] ? ",VIP" : "");
        }
        printf("\n");
        
        // sprawdz czy sa dzieci < 12 lat
        int has_young_children = 0;
        for (int i = 0; i < M_GROUP_SIZE; i++) {
            if (group->member_ages[i] < 12) {
                has_young_children = 1;
                break;
            }
        }
        if (has_young_children) {
            printf("[PRZEWODNIK %d] Uwaga: grupa z dziećmi < 12 lat - czas wydłużony o 50%%\n", id);
        }
        
        // losowa awaria przed startem
        if ((rand() % 100) < 2) {
            printf("[PRZEWODNIK %d] Awaria przed startem!\n", id);
            send_emergency_exit(group, id);
            
            // obudz turystow zeby mogli wyjsc
            for (int k = 0; k < M_GROUP_SIZE; k++) {
                sem_unlock(sem_id, SEM_GROUP_START(group_slot));
            }
            
            // wyslij raport
            int fifo_fd = open(FIFO_PATH, O_WRONLY | O_NONBLOCK);
            if (fifo_fd != -1) {
                char report[256];
                sprintf(report, "Przewodnik %d - awaria przed startem\n", id);
                write(fifo_fd, report, strlen(report));
                close(fifo_fd);
            }
            
            // zwolnij slot
            group->active = 0;
            //sleep(1);
            continue;
        }
        
        // start wycieczki
        if (group->route == 1) {
            printf("[PRZEWODNIK %d] Trasa: (Most) -> (Wieża) -> (Prom)\n", id);
        } else {
            printf("[PRZEWODNIK %d] Trasa: (Prom) -> (Wieża) -> (Most)\n", id);
        }
        
        printf("[PRZEWODNIK %d] Startujemy! Budzę turystów.\n", id);
        
        // obudz wszystkich turystow w grupie
        for (int k = 0; k < M_GROUP_SIZE; k++) {
            sem_unlock(sem_id, SEM_GROUP_START(group_slot));
        }
        
        //usleep(200000);  // 200ms na zgromadzenie
        
        // petla po 3 atrakcjach
        for (int step = 0; step < 3; step++) {
            int attraction = get_attraction_for_step(group->route, step);
            group->current_attraction = attraction;
            group->attraction_step = step;
            
            printf("\n[PRZEWODNIK %d] FAZA %d: Atrakcja %d\n", id, step + 1, attraction);
            
            // przewodnik wchodzi pierwszy lub czeka
            switch (attraction) {
                case ATTR_BRIDGE:
                    // przewodnik wchodzi pierwszy na most
                    guide_cross_bridge(id, get_bridge_direction(group->route), park, sem_id);
                    break;
                    
                case ATTR_TOWER:
                    // przewodnik nie wchodzi na wieze
                    printf("[PRZEWODNIK %d] Czekam pod wieżą (nie wchodzę).\n", id);
                    
                    // losowa ewakujaca wiezy
                    if ((rand() % 100) < 3) {
                        //usleep(500000);  // 500ms
                        send_tower_evacuation(group, park, id);
                    }
                    break;
                    
                case ATTR_FERRY:
                    // przewodnik wchodzi pierwszy na prom
                    guide_take_ferry(id, group_slot, get_ferry_direction(group->route), park, sem_id);
                    break;
            }
            
            // czekanie na turystow
            printf("[PRZEWODNIK %d] Czekam aż wszyscy turyści skończą atrakcję %d...\n", id, attraction);
            
            // czekamy na M sygnalow od turystow
            for (int k = 0; k < M_GROUP_SIZE; k++) {
                sem_lock(sem_id, SEM_GROUP_DONE(group_slot));
            }

            sem_lock(sem_id, SEM_PROM_MUTEX);
            park->ferry_position = get_ferry_direction(group->route);
            park->ferry_group_id = -1;
            sem_unlock(sem_id, SEM_PROM_MUTEX);
            
            printf("[PRZEWODNIK %d] Wszyscy gotowi! Idziemy dalej.\n", id);
            
            // reset licznika
            group->tourists_ready = 0;
            
            // sygnal do nastepnej fazy (jesli nie ostatnia)
            if (step < 2) {
                printf("[PRZEWODNIK %d] Przechodzimy do następnej atrakcji.\n", id);
                
                // Symulacja przejscia miedzy atrakcjami
                int walk_time = 1;
                if (has_young_children) {
                    walk_time = (int)(walk_time * 1.5) + 1;
                    printf("[PRZEWODNIK %d] Wolniejsze tempo (dzieci) - %ds\n", id, walk_time);
                }
                //sleep(walk_time);
                
                // obudz turystow do nastepnej fazy
                for (int k = 0; k < M_GROUP_SIZE; k++) {
                    sem_unlock(sem_id, SEM_GROUP_START(group_slot));
                }
            }
        }

        // koniec wycieczki
        printf("\n[PRZEWODNIK %d] KONIEC WYCIECZKI\n", id);
        printf("[PRZEWODNIK %d] Odprowadzam grupę do kasy.\n", id);
        
        // wyslij raport przez fifo
        int fifo_fd = open(FIFO_PATH, O_WRONLY | O_NONBLOCK);
        if (fifo_fd == -1) {
            perror("[PRZEWODNIK] Błąd open FIFO");
        } else {
            char report[256];
            sprintf(report, "Przewodnik %d zakończył wycieczkę (trasa %d, %d osób)\n", id, group->route, M_GROUP_SIZE);
            if (write(fifo_fd, report, strlen(report)) == -1) {
                perror("[PRZEWODNIK] Błąd write FIFO");
            }
            close(fifo_fd);
        }
        
        // zwolnij slot grupy
        group->active = 0;
        
        printf("[PRZEWODNIK %d] Wracam do bazy. Czekam na kolejną grupę...\n\n", id);
        
        //sleep(1);  // mala przerwa
    }
    
    // odlaczenie pamieci dzielonej
    shmdt(park);
    
    return 0;
}