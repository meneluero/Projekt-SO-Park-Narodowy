#include "common.h"
#include <string.h>
#include <signal.h>

// funkcja pomocnicza (wejscie na most)
void enter_bridge(int id, int direction, struct ParkSharedMemory *park, int sem_id){
    while (1) {
        // blokujemy dostep danych do mostu
        sem_lock(sem_id, SEM_MOST_MUTEX);

        // sprawdzamy warunki wejscia
        // most musi miec mniej niz X1 osob
        // oraz most pusty lub kierunek zgodny z naszym
        int can_enter = (park->bridge_current_count < X1_BRIDGE_CAP) && 
        (park->bridge_current_count == 0 || park->bridge_direction == direction);

        if (can_enter) {
            // mozliwosc wejscia
            park->bridge_direction = direction;
            park->bridge_current_count++;
            
            // odblokuj mutex i wyjdz z petli czekania
            sem_unlock(sem_id, SEM_MOST_MUTEX);
            break; 

        } else {
            // brak mozliwosci wejscia - odblokuj mutex i poczekaj chwile
            sem_unlock(sem_id, SEM_MOST_MUTEX);
            usleep(10000); // czekamy 10ms przed ponowna proba
        }
    }
}

// funkcja pomocnicza zejscie z mostu
void exit_bridge(struct ParkSharedMemory *park, int sem_id) {
    sem_lock(sem_id, SEM_MOST_MUTEX);
    park->bridge_current_count--;
    sem_unlock(sem_id, SEM_MOST_MUTEX);
}

// glowna funkcja logiki mostu
void cross_bridge(int guide_id, int direction, struct ParkSharedMemory *park, int sem_id, int ages[], int ids[]) {
    char* dir_name = (direction == 0) ? "K->A" : "A->K";
    printf("[MOST] Przewodnik %d podchodzi do mostu (%s). Grupa czeka.\n", guide_id, dir_name);

    // przewodnik wchodzi pierwszy
    enter_bridge(guide_id, direction, park, sem_id);
    printf("[MOST] Przewodnik %d wchodzi na most.\n", guide_id);
    usleep(200000 + (rand() % 300000)); // czas przejscia przewodnika
    exit_bridge(park, sem_id);
    printf("[MOST] Przewodnik %d przeszedł. Wpuszczam grupę...\n", guide_id);

    // zarzadzanie turystami (logika opiekunow)
    // pobieramy wiek turystow z pamieci dzielonej
    int local_ages[M_GROUP_SIZE];
    int processed[M_GROUP_SIZE]; // flaga czy turysta juz przeszedl
    for(int i=0; i<M_GROUP_SIZE; i++) {
        local_ages[i] = ages[i]; // czytamy z parametru
        processed[i] = 0;
    }

    int remaining = M_GROUP_SIZE;
    
    while (remaining > 0) {
        int t1_idx = -1;
        int t2_idx = -1;

        // najpierw szukamy dzieci < 15 lat
        for(int i=0; i<M_GROUP_SIZE; i++) {
            if (!processed[i] && local_ages[i] < 15) {
                t1_idx = i;
                break;
            }
        }

        // jesli nie ma juz dzieci bierzemy dowolnego doroslego
        if (t1_idx == -1) {
            for(int i=0; i<M_GROUP_SIZE; i++) {
                if (!processed[i]) {
                    t1_idx = i;
                    break;
                }
            }
        }

        if (t1_idx == -1) break; // wszyscy przeszli

        // logika opiekuna: jesli znaleziony turysta to dziecko < 15 lat
        if (local_ages[t1_idx] < 15) {
            // musimy znalezc mu opiekuna (dorosłego >= 15)
            for (int i=0; i<M_GROUP_SIZE; i++) {
                if (!processed[i] && i != t1_idx && local_ages[i] >= 15) {
                    t2_idx = i; // znaleziono opiekuna
                    break;
                }
            }
            
            if (t2_idx != -1) {
                // wejscie we dwojke (dziecko + opiekun)
                int id_c = (local_ages[t1_idx] < 15) ? ids[t1_idx] : ids[t2_idx];
                int id_g = (local_ages[t1_idx] >= 15) ? ids[t1_idx] : ids[t2_idx];
                int age_c = (local_ages[t1_idx] < 15) ? local_ages[t1_idx] : local_ages[t2_idx];
                int age_g = (local_ages[t1_idx] >= 15) ? local_ages[t1_idx] : local_ages[t2_idx];
                
                printf("[MOST] Wchodzą: Dziecko (Turysta %d (lat %d)) + Opiekun (Turysta %d (lat %d))\n", id_c, age_c, id_g, age_g);
                
                // musimy zajac dwa miejsca na moscie
                enter_bridge(guide_id, direction, park, sem_id); // dla dziecka
                enter_bridge(guide_id, direction, park, sem_id); // dla opiekuna
                
                usleep(300000 + (rand() % 400000)); // czas przejscia
                
                exit_bridge(park, sem_id);
                exit_bridge(park, sem_id);
                
                processed[t1_idx] = 1;
                processed[t2_idx] = 1;
                remaining -= 2;
            } else {
                // brak opiekuna w grupie (przewodnik bierze dziecko - symulacja pojedynczego wejscia)
                // wymaganie mowi "pod opieka osoby doroslej" ale jesli brak nie mozemy zablokowac programu
                printf("[MOST] Dziecko (Turysta %d (lat %d)) idzie pod opieką przewodnika.\n", ids[t1_idx], local_ages[t1_idx]);
                enter_bridge(guide_id, direction, park, sem_id);
                usleep(400000);
                exit_bridge(park, sem_id);
                processed[t1_idx] = 1;
                remaining--;
            }
        } else {
            // dorosly idzie sam
            printf("[MOST] Wchodzi turysta %d (lat %d)\n", ids[t1_idx], local_ages[t1_idx]);
            enter_bridge(guide_id, direction, park, sem_id);
            usleep(200000 + (rand() % 200000));
            exit_bridge(park, sem_id);
            processed[t1_idx] = 1;
            remaining--;
        }
    }

    printf("[MOST] Cała grupa przeszła przez most (%s).\n", dir_name);
}

// funkcja pomocnicza do rejestracji wejscia na wieze (zabezpieczona mutexem)
void register_tower_entry(struct ParkSharedMemory *park, int sem_id, pid_t tourist_pid) {
    sem_lock(sem_id, SEM_WIEZA_MUTEX);
    
    // szukamy wolnego slotu w tablicy pid
    for (int i = 0; i < N_PARK_CAPACITY; i++) {
        if (park->tower_visitors[i] == 0) {
            park->tower_visitors[i] = tourist_pid;
            break;
        }
    }
    park->tower_current_count++;
    
    sem_unlock(sem_id, SEM_WIEZA_MUTEX);
}

// funkcja pomocnicza do wyrejestrowania (mutex)
void unregister_tower_exit(struct ParkSharedMemory *park, int sem_id, pid_t tourist_pid) {
    sem_lock(sem_id, SEM_WIEZA_MUTEX);
    
    for (int i = 0; i < N_PARK_CAPACITY; i++) {
        if (park->tower_visitors[i] == tourist_pid) {
            park->tower_visitors[i] = 0; // czyscimy slot
            break;
        }
    }
    if (park->tower_current_count > 0) park->tower_current_count--;
    
    sem_unlock(sem_id, SEM_WIEZA_MUTEX);
}

// struktura pomocnicza do sortowania kolejki
typedef struct {
    int index;
    int age;
    pid_t pid;
    int is_vip;
} TouristInfo;

// kompletna funkcja wiezy
void visit_tower(int guide_id, struct ParkSharedMemory *park, int sem_id, int ages[], pid_t pids[], int vips[], int ids[]) {
    printf("[WIEŻA] Przewodnik %d dociera pod wieżę widokową.\n", guide_id);

    // filtracja - kto zostaje na dole (dzieci < 5 lat + opiekunowie)
    int can_enter[M_GROUP_SIZE]; // 1 = wchodzi, 0 = zostaje
    int is_guardian_down[M_GROUP_SIZE]; // flagi pomocnicze kto zostal jako opiekun na dole
    
    for(int i=0; i<M_GROUP_SIZE; i++) { 
        can_enter[i] = 1; 
        is_guardian_down[i] = 0; 
    }
    
    int rejected_count = 0;

    // najpierw oznaczamy dzieci < 5 lat i dobieramy im opiekunow do pozostania
    for (int i = 0; i < M_GROUP_SIZE; i++) {
        if (ages[i] < 5) {
            printf("[WIEŻA] Turysta %d (lat %d) jest za mały (<5 lat). Zostaje na dole.\n", ids[i], ages[i]);
            can_enter[i] = 0;
            rejected_count++;
            
            // wymog: opiekun tez nie moze wejsc
            // szukamy doroslego (>=18) ktory jeszcze nie zostal odrzucony
            int guardian_idx = -1;
            for (int j = 0; j < M_GROUP_SIZE; j++) {
                if (i != j && can_enter[j] == 1 && ages[j] >= 18 && !is_guardian_down[j]) {
                    guardian_idx = j;
                    break;
                }
            }
            
            if (guardian_idx != -1) {
                can_enter[guardian_idx] = 0; // opiekun zostaje na dole
                is_guardian_down[guardian_idx] = 1;
                rejected_count++;
                printf("[WIEŻA] Turysta %d (lat %d) zostaje na dole jako opiekun malucha.\n", ids[guardian_idx], ages[guardian_idx]);
            } else {
                printf("[WIEŻA] Dziecko %d (lat %d) zostaje pod opieka przewodnika (brak wolnych opiekunow).\n", ids[i], ages[i]);
            }
        }
    }

    if (rejected_count == M_GROUP_SIZE) {
        printf("[WIEŻA] Nikt z grupy nie może wejść na wieżę! Idziemy dalej.\n");
        return;
    }

    // kolejkowanie (vip/starsi wchodza pierwsi)
    int entry_queue[M_GROUP_SIZE];
    int count_to_enter = 0;
    
    // najpierw vipy
    for (int i = 0; i < M_GROUP_SIZE; i++) {
        if (can_enter[i] && vips[i]) entry_queue[count_to_enter++] = i;
    }
    // potem starsi > 60 lat
    for (int i = 0; i < M_GROUP_SIZE; i++) {
        if (can_enter[i] && !vips[i] && ages[i] > 60) entry_queue[count_to_enter++] = i;
    }
    // potem reszta
    for (int i = 0; i < M_GROUP_SIZE; i++) {
        if (can_enter[i] && !vips[i] && ages[i] <= 60) entry_queue[count_to_enter++] = i;
    }

    // tablica flag, kto juz wszedl (zeby nie wziac opiekuna dwa razy)
    int processed_entry[M_GROUP_SIZE];
    for(int i=0; i<M_GROUP_SIZE; i++) processed_entry[i] = 0;

    // proces wchodzenia (z parowaniem < 15 lat)
    printf("[WIEŻA] Przewodnik czeka na dole. Turyści wchodzą...\n");

    for (int k = 0; k < count_to_enter; k++) {
        int idx = entry_queue[k];

        // jesli ta osoba juz weszla (np. jako opiekun kogos wczesniej), pomijamy
        if (processed_entry[idx]) continue;

        pid_t t_pid = pids[idx];
        int t_age = ages[idx];
        int t_id = ids[idx];
        
        int companion_idx = -1;

        // logika dwukierunkowa
        if (t_age < 15) {
            // dziecko szuka opiekuna
            for (int m = 0; m < count_to_enter; m++) {
                int possible = entry_queue[m];
                // szukamy kogos kto nie wszedl, nie jest mna i jest dorosly
                if (possible != idx && !processed_entry[possible] && ages[possible] >= 18) {
                    companion_idx = possible;
                    break;
                }
            }
        } else if (t_age >= 18) {
            // dorosly sprawdza czy nie musi wziac dziecka
            for (int m = 0; m < count_to_enter; m++) {
                int possible = entry_queue[m];
                // szukamy kogos kto nie wszedl, nie jest mna i jest dzieckiem
                if (possible != idx && !processed_entry[possible] && ages[possible] < 15) {
                    companion_idx = possible;
                    break;
                }
            }
        }

        // wejscie na wieze
        if (companion_idx != -1) {
            // wchodzi para: dziecko + opiekun
            // sprawdzamy dostepnosc 2 miejsc (2 x sem_lock)
            sem_lock(sem_id, SEM_WIEZA_LIMIT);
            sem_lock(sem_id, SEM_WIEZA_LIMIT);
            
            pid_t g_pid = pids[companion_idx];
            int g_age = ages[companion_idx];
            int g_id = ids[companion_idx];

            int id_c = (t_age < 15) ? t_id : g_id;
            int age_c = (t_age < 15) ? t_age : g_age;
            int id_g = (t_age >= 15) ? t_id : g_id;
            int age_g = (t_age >= 15) ? t_age : g_age;
            
            printf("[WIEŻA] Wchodzą: Dziecko %d (lat %d) + Opiekun %d (lat %d). (Liczba osób: %d)\n", id_c, age_c, id_g, age_g, park->tower_current_count + 2);
            
            register_tower_entry(park, sem_id, t_pid);
            register_tower_entry(park, sem_id, g_pid);
            
            // symulacja awarii dla pary
            if ((rand() % 100) < 2) { 
                printf("\n[WIEŻA] Awaria konstrukcji! Ewakuacja! (SIGUSR1)\n");
                sem_lock(sem_id, SEM_WIEZA_MUTEX);
                for(int j=0; j<N_PARK_CAPACITY; j++) {
                    if (park->tower_visitors[j] != 0) kill(park->tower_visitors[j], SIGUSR1);
                }
                sem_unlock(sem_id, SEM_WIEZA_MUTEX);
                
                // sprzatanie po parze
                unregister_tower_exit(park, sem_id, t_pid);
                unregister_tower_exit(park, sem_id, g_pid);
                sem_unlock(sem_id, SEM_WIEZA_LIMIT);
                sem_unlock(sem_id, SEM_WIEZA_LIMIT);
                printf("[WIEŻA] Grupa ewakuowana.\n");
                return; 
            }
            
            usleep(300000 + (rand() % 300000)); 
            
            unregister_tower_exit(park, sem_id, t_pid);
            unregister_tower_exit(park, sem_id, g_pid);
            sem_unlock(sem_id, SEM_WIEZA_LIMIT);
            sem_unlock(sem_id, SEM_WIEZA_LIMIT);
            
            processed_entry[companion_idx] = 1; // oznaczamy opiekuna jako obsluzonego
            printf("[WIEŻA] Para (Dziecko (Turysta %d) + Opiekun (Turysta %d)) zeszła z wieży.\n", id_c, id_g);

        } else {
            // wchodzi pojedynczo (dorosly lub dziecko bez pary)
            sem_lock(sem_id, SEM_WIEZA_LIMIT);
            
            register_tower_entry(park, sem_id, t_pid);
            if (t_age < 15) {
                printf("[WIEŻA] Dziecko (Turysta %d (lat %d)) wchodzi pod opieką obsługi wieży (brak opiekuna w grupie).\n", t_id, t_age);
            } else {
                printf("[WIEŻA] Turysta %d (lat %d) wchodzi na górę. (Liczba osób: %d)\n", t_id, t_age, park->tower_current_count);
            }

            // symulacja awarii dla pojedynczej osoby
            if ((rand() % 100) < 2) {
                printf("\n[WIEŻA] Awaria konstrukcji! Ewakuacja! (SIGUSR1)\n");
                sem_lock(sem_id, SEM_WIEZA_MUTEX);
                for(int j=0; j<N_PARK_CAPACITY; j++) {
                    if (park->tower_visitors[j] != 0) kill(park->tower_visitors[j], SIGUSR1);
                }
                sem_unlock(sem_id, SEM_WIEZA_MUTEX);
                
                unregister_tower_exit(park, sem_id, t_pid);
                sem_unlock(sem_id, SEM_WIEZA_LIMIT);
                printf("[WIEŻA] Grupa ewakuowana.\n");
                return; 
            }

            usleep(300000 + (rand() % 300000)); 

            unregister_tower_exit(park, sem_id, t_pid);
            sem_unlock(sem_id, SEM_WIEZA_LIMIT);
            printf("[WIEŻA] Turysta %d zszedł z wieży.\n", t_id);
        }
        
        processed_entry[idx] = 1;
    }
    
    printf("[WIEŻA] Wszyscy chętni zwiedzili wieżę. Idziemy dalej.\n");
}


// funkcja obslugi promu
void take_ferry(int guide_id, int start_bank, struct ParkSharedMemory *park, int sem_id, int ages[], int vips[], int ids[]) {
    char* bank_name = (start_bank == 0) ? "Brzeg C (Wyspa)" : "Brzeg A (Ląd)"; 
    printf("[PROM] Przewodnik %d dociera do przeprawy promowej (%s).\n", guide_id, bank_name);

    // sprawdzenie i sciagniecie promu
    sem_lock(sem_id, SEM_PROM_MUTEX);
    if (park->ferry_position != start_bank) {
        printf("[PROM] Prom jest na drugim brzegu. Przewodnik %d przywołuje prom...\n", guide_id);
        sem_unlock(sem_id, SEM_PROM_MUTEX);
        usleep(500000); // czas na doplyniecie pustego promu
        sem_lock(sem_id, SEM_PROM_MUTEX);
        park->ferry_position = start_bank;
        printf("[PROM] Prom przypłynął.\n");
    } else {
        printf("[PROM] Prom czeka na miejscu.\n");
    }
    
    // przewodnik wchodzi (blokuje 1 miejsce z limitu)
    // przyjmijmy: zajmuje slot, wchodzi pierwszy
    park->ferry_current_count = 1; 
    sem_unlock(sem_id, SEM_PROM_MUTEX);

    // przygotowanie listy pasazerow (vip priorytet + parowanie dzieci)
    int processed[M_GROUP_SIZE];
    for(int i=0; i<M_GROUP_SIZE; i++) processed[i] = 0;
    
    int remaining = M_GROUP_SIZE;
    
    // sortowanie kolejki wejscia (vip/seniorzy przodem)
    // tworzymy tablice indeksow posortowana wg priorytetu
    int priority_indices[M_GROUP_SIZE];
    int p_count = 0;
    
    // najpierw vip
    for(int i=0; i<M_GROUP_SIZE; i++) {
        if(vips[i]) priority_indices[p_count++] = i;
    }
    // seniorzy > 60 
    for(int i=0; i<M_GROUP_SIZE; i++) {
        if(!vips[i] && ages[i] > 60) priority_indices[p_count++] = i;
    }
    // reszta
    for(int i=0; i<M_GROUP_SIZE; i++) {
        if(!vips[i] && ages[i] <= 60) priority_indices[p_count++] = i;
    }

    // petla kursowania promu
    while (remaining > 0) {
        int capacity_left = X3_FERRY_CAP; // limit miejsc dla turystow
        int current_batch = 0; // ile osob wsiadlo teraz
        
        printf("[PROM] Rozpoczynam załadunek (Wolnych miejsc: %d)...\n", capacity_left);

        // sekcja krytyczna zaladunku
        sem_lock(sem_id, SEM_PROM_MUTEX);
        
        // czy prom nadal jest po naszej stronie
        if (park->ferry_position != start_bank) {
            printf("[PROM] Błąd! Prom zmienił położenie podczas załadunku! Czekam na powrót...\n");
            park->ferry_position = start_bank; // wymuszamy powrot
            usleep(100000);
        }

        // algorytm zaladunku z listy priorytetowej
        while (capacity_left > 0 && current_batch < remaining) {
             int t1_idx = -1;
             int t2_idx = -1;

             // szukamy dziecka w liscie priorytetow
             for(int k=0; k<M_GROUP_SIZE; k++) {
                 int real_idx = priority_indices[k];
                 if(!processed[real_idx] && ages[real_idx] < 15) { 
                     t1_idx = real_idx; 
                     break; 
                 } 
             }
             
             // jesli brak dzieci bierzemy pierwszego doroslego z brzegu
             if (t1_idx == -1) {
                 for(int k=0; k<M_GROUP_SIZE; k++) {
                     int real_idx = priority_indices[k];
                     if(!processed[real_idx]) { 
                         t1_idx = real_idx; 
                         break; 
                     } 
                 }
             }
             
             if (t1_idx == -1) break; // brak chetnych

             // czy to dziecko < 15
             if (ages[t1_idx] < 15) {
                 // szukaj opiekuna - ignorujemy priorytet, bierzemy pierwszego wolnego
                 for(int i=0; i<M_GROUP_SIZE; i++) {
                     if(!processed[i] && i!=t1_idx && ages[i] >= 15) { 
                         t2_idx = i; 
                         break; 
                     } 
                 }
                 
                 // jesli mamy opiekuna i miejsce na 2 osoby
                 if (t2_idx != -1 && capacity_left >= 2) {
                    int id_c = (ages[t1_idx] < 15) ? ids[t1_idx] : ids[t2_idx];
                    int id_g = (ages[t1_idx] >= 15) ? ids[t1_idx] : ids[t2_idx];
                    int age_c = (ages[t1_idx] < 15) ? ages[t1_idx] : ages[t2_idx];
                    int age_g = (ages[t1_idx] >= 15) ? ages[t1_idx] : ages[t2_idx];

                    printf("[PROM] Wchodzą: Dziecko (Turysta %d (lat %d)) + Opiekun (Turysta %d (lat %d))\n", id_c, age_c, id_g, age_g);
                    processed[t1_idx] = 1; processed[t2_idx] = 1;
                    remaining -= 2; capacity_left -= 2; current_batch += 2;
                 } 
                 // jesli brak opiekuna lub miejsca ale jest miejsce na 1 osobe -> bierze przewodnik
                 else if (t2_idx == -1 && capacity_left >= 1) {
                    printf("[PROM] Dziecko (Turysta %d (lat %d)) pod opieką przewodnika.\n", ids[t1_idx], ages[t1_idx]);
                    processed[t1_idx] = 1;
                    remaining -= 1; capacity_left -= 1; current_batch += 1;
                 } else {
                    // dziecko z opiekunem nie zmiesci sie w tej turze szukamy doroslego singla
                    // upraszczajac: przerywamy ladowanie tej tury
                    break; 
                 }
             } else {
                 // dorosly
                 printf("[PROM] Wchodzi turysta %d (lat %d)\n", ids[t1_idx], ages[t1_idx]);
                 processed[t1_idx] = 1;
                 remaining--; capacity_left--; current_batch++;
             }
        }

        // koniec sekcji krytycznej zaladunku
        park->ferry_current_count = 1 + current_batch;
        sem_unlock(sem_id, SEM_PROM_MUTEX);

        if (current_batch == 0 && remaining > 0) {
            // zabezpieczenie przed petla nieskonczona (np. same dzieci i brak miejsc)
            printf("[PROM] Błąd logistyczny! Awaryjny transport.\n");
            break; 
        }

        // plyniemy
        printf("[PROM] Prom odbija od brzegu z %d pasażerami i przewodnikiem.\n", current_batch);
        usleep(400000 + (rand() % 300000)); // czas przeprawy
        
        // zmiana brzegu w pamieci
        sem_lock(sem_id, SEM_PROM_MUTEX);
        park->ferry_position = (start_bank == 0) ? 1 : 0;
        sem_unlock(sem_id, SEM_PROM_MUTEX);
        
        printf("[PROM] Dopłyneliśmy na drugi brzeg. Wysiadanie.\n");
        
        // jesli zostaly osoby prom musi wrocic "na pusto" po reszte
        if (remaining > 0) {
            printf("[PROM] Prom wraca na pusto po resztę grupy...\n");
            usleep(400000); // czas powrotu
            sem_lock(sem_id, SEM_PROM_MUTEX);
            park->ferry_position = start_bank; // wrocil
            sem_unlock(sem_id, SEM_PROM_MUTEX);
        }
    }
    printf("[PROM] Cała grupa przeprawiona.\n");
}

void trigger_emergency_evacuation(pid_t group_pids[], int count, int guide_id) {
    printf("\n[PRZEWODNIK %d] Sytuacja awaryjna! Ewakuacja!\n", guide_id);
    
    for(int i = 0; i < count; i++) {
        if (group_pids[i] > 0) {
            printf("[PRZEWODNIK %d] Wysyłam SIGUSR2 do turysty PID=%d\n", guide_id, group_pids[i]);
            kill(group_pids[i], SIGUSR2);
        }
    }
    
    usleep(200000); // dajemy turystom czas na reakcje
    printf("[PRZEWODNIK %d] Odprowadzam grupę bezpośrednio do kasy.\n", guide_id);
}

int main(int argc, char* argv[]) {
    // walidacja
    if (argc < 2) {
        printf("[PRZEWODNIK] Błąd: Brak ID!\n");
        exit(1);
    }
    
    int id = atoi(argv[1]);
    
    // podlaczenie do pamieci dzielonej
    int shm_id = shmget(SHM_KEY_ID, sizeof(struct ParkSharedMemory), 0666);
    if (shm_id == -1) {
        perror("[PRZEWODNIK] Błąd shmget");
        exit(1);
    }
    
    struct ParkSharedMemory *park = (struct ParkSharedMemory*)shmat(shm_id, NULL, 0);
    if (park == (void*)-1) {
        perror("[PRZEWODNIK] Błąd shmat");
        exit(1);
    }
    
    // pobranie id semaforow
    int sem_id = semget(SEM_KEY_ID, 10, 0666);
    if (sem_id == -1) {
        perror("[PRZEWODNIK] Błąd semget");
        exit(1);
    }
    
    // pobranie id kolejki komunikatow (do wysylania raportow)
    int msg_id = msgget(MSG_KEY_ID, 0666);
    if (msg_id == -1) {
        perror("[PRZEWODNIK] Błąd msgget");
        exit(1);
    }
    
    // inicjalizacja generatora losowego
    srand(time(NULL) + id);
    
    // petla zycia przewodnika
    printf("[PRZEWODNIK %d] Melduję się w pracy! Czekam na grupy...\n", id);
    
    // przewodnik w przeciwienstwie do turysty jest w petli nieskonczonej
    // (obsluguje wiele wycieczek)
    while (1) {
        // czekanie na grupe (semafor 1)
        sem_lock(sem_id, 1);
        
        // kopiujemy wiek turysty od razu po przebudzeniu
        // zanim nowi turysci nadpisza pamiec wspoldzielona
        int current_group_ages[M_GROUP_SIZE];
        int current_group_vips[M_GROUP_SIZE];

        for(int i=0; i<M_GROUP_SIZE; i++) {
            current_group_ages[i] = park->group_ages[i];
            current_group_vips[i] = park->group_vips[i];
        }

        // przejecie grupy
        printf("[PRZEWODNIK %d] Mam grupę %d osób! Zabieram was na wycieczkę!\n", id, M_GROUP_SIZE);
        
        // odblokowanie turystow - wisza na semaforze nr 2
        // musimy ich uwolnic M razy bo tylu jest w grupie
        for (int k = 0; k < M_GROUP_SIZE; k++) {
            sem_unlock(sem_id, 2);
        }
        
        // male opoznienie zeby turysci sie "zgromadzili"
        usleep(200000); // 200ms
        
        // kopiujemy pid zaraz po przebudzeniu
        pid_t current_group_pids[M_GROUP_SIZE];
        int current_group_ids[M_GROUP_SIZE];
        for(int i=0; i<M_GROUP_SIZE; i++) {
            current_group_pids[i] = park->group_pids[i];
            current_group_ids[i] = park->group_ids[i];
        }

        // symulacja sytuacji awaryjnej
        if ((rand() % 100) < 2) {
            trigger_emergency_evacuation(current_group_pids, M_GROUP_SIZE, id);

            // raport
            struct msg_buffer report;
            report.msg_type = MSG_TYPE_REPORT;
            report.tourist_id = id;
            sprintf(report.info, "Przewodnik %d - ewakuacja (awaria)", id);
            msgsnd(msg_id, &report, sizeof(report) - sizeof(long), 0);
            
            sleep(1);
            continue; // pomijamy trase, bierzemy kolejna grupe
        }
        // losowanie trasy (1 lub 2)
        int route = (rand() % 2) + 1;
        
        printf("[PRZEWODNIK %d] Wybieram trasę %d\n", id, route);
        
        if (route == 1) {
            printf("[PRZEWODNIK %d] Trasa: K → A → B → C → K\n", id);

            cross_bridge(id, 0, park, sem_id, current_group_ages, current_group_ids);

            visit_tower(id, park, sem_id, current_group_ages, current_group_pids, current_group_vips, current_group_ids);
            
            take_ferry(id, 0, park, sem_id, current_group_ages, current_group_vips, current_group_ids);
        } else {
            printf("[PRZEWODNIK %d] Trasa: K → C → B → A → K\n", id);

            take_ferry(id, 1, park, sem_id, current_group_ages, current_group_vips, current_group_ids);

            visit_tower(id, park, sem_id, current_group_ages, current_group_pids, current_group_vips, current_group_ids);

            cross_bridge(id, 1, park, sem_id, current_group_ages, current_group_ids);
        }
        
        // symulacja wycieczki
        int tour_time = 3 + (rand() % 4); // 3-6 sekund
        printf("[PRZEWODNIK %d] Oprowadzam wycieczkę (czas: %ds)...\n", id, tour_time);

        int has_young_children = 0;
        for(int i=0; i<M_GROUP_SIZE; i++) {
            if(current_group_ages[i] < 12) {
                has_young_children = 1;
                break;
            }
        }

        if(has_young_children) {
            tour_time = (int)(tour_time * 1.5);
            printf("[PRZEWODNIK %d] Grupa z dziećmi < 12 lat - czas wydłużony do %ds.\n", id, tour_time);
        }
        sleep(tour_time);

        // zwalniamy turystów
        printf("[PRZEWODNIK %d] Koniec czasu! Zwalniam grupę do domu.\n", id);
        
        // budzimy kazdego turyste z grupy osobno
        for (int k = 0; k < M_GROUP_SIZE; k++) {
            sem_unlock(sem_id, SEM_KONIEC_WYCIECZKI); // semafor nr 3
        }
        
        // koniec wycieczki
        printf("[PRZEWODNIK %d] Koniec wycieczki. Grupa wraca do kasy.\n", id);
        
        // przygotowanie raportu dla kasjera
        struct msg_buffer report_msg;
        report_msg.msg_type = MSG_TYPE_REPORT;
        report_msg.tourist_id = id; // id przewodnika
        report_msg.age = 0;
        report_msg.is_vip = 0;
        
        char report_info[256];
        sprintf(report_info, "Przewodnik %d zakończył wycieczkę (trasa %d, %d osób)", id, route, M_GROUP_SIZE);
        strcpy(report_msg.info, report_info);
        
        // wyslanie raportu do kasjera
        if (msgsnd(msg_id, &report_msg, sizeof(report_msg) - sizeof(long), 0) == -1) {
            perror("[PRZEWODNIK] Błąd msgsnd (raport)");
        }
        
        printf("[PRZEWODNIK %d] Wracam do bazy. Czekam na kolejną grupę...\n\n", id);
        
        // male opoznienie przed przyjeciem kolejnej grupy
        sleep(1);
    }
    
    // sprzatanie (nigdy nie powinno sie wykonac)
    shmdt(park);
    
    return 0;
}