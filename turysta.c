#include "common.h"
#include <signal.h>

// zmienne globalne dla handlerow sygnalow
volatile sig_atomic_t tower_evacuation_flag = 0;
volatile sig_atomic_t emergency_exit_flag = 0;

// globalne zmienne ipc
int g_sem_id = -1;
int g_id = -1;
struct ParkSharedMemory *g_park = NULL;

// handlery sygnalow
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

// przejscie przez most
void do_bridge(int id, int age, int is_vip, int direction, struct ParkSharedMemory *park, int sem_id) {
    
    printf("[TURYSTA %d] Podchodzę do mostu (kierunek: %s)\n", id, direction == DIR_KA ? "K->A" : "A->K");
    
    // dzieci < 15 lat - komunikat o opiece
    if (age < 15) {
        printf("[TURYSTA %d] Mam %d lat - idę przez most pod opieką dorosłego\n", id, age);
    }
    
    // czekaj na odpowiedni kierunek mostu
    int can_enter = 0;
    while (!can_enter && !emergency_exit_flag) {
        sem_lock(sem_id, SEM_MOST_MUTEX);
        
        // sprawdz czy most jest wolny lub idzie w naszym kierunku
        if (park->bridge_direction == DIR_NONE || 
            park->bridge_direction == direction) {
            
            // probujemy zarezerwowac miejsce
            if (sem_trylock(sem_id, SEM_MOST_LIMIT) == 0) {
                // udalo sie, ustawiamy kierunek i wchodzimy
                park->bridge_direction = direction;
                park->bridge_current_count++;
                park->bridge_crossing[direction]++;
                can_enter = 1;
                printf("[TURYSTA %d] Wchodzę na most (%d/%d osób)\n", id, park->bridge_current_count, X1_BRIDGE_CAP);
            }
        } else {
            // most zajety w przeciwnym kierunku - czekamy
            park->bridge_waiting[direction]++;
            sem_unlock(sem_id, SEM_MOST_MUTEX);
            //usleep(100000);
            sem_lock(sem_id, SEM_MOST_MUTEX);
            park->bridge_waiting[direction]--;
        }
        
        sem_unlock(sem_id, SEM_MOST_MUTEX);
        
        if (!can_enter && !emergency_exit_flag) {
            //usleep(50000);
        }
    }
    
    // sprawdz czy musimy wracac awaryjnie
    if (emergency_exit_flag) {
        printf("[TURYSTA %d] Ewakuacja! Rezygnuję z mostu.\n", id);
        return;
    }
    
    // przechodzenie przez most
    printf("[TURYSTA %d] Przechodzę przez most...\n", id);
    
    int cross_time = BRIDGE_CROSS_TIME;
    while (cross_time > 0 && !emergency_exit_flag) {
        //sleep(1);
        cross_time--;
    }
    
    // schodzenie z mostu
    sem_lock(sem_id, SEM_MOST_MUTEX);
    
    park->bridge_current_count--;
    park->bridge_crossing[direction]--;
    
    // jesli nikt juz nie idzie zwalniamy kierunek
    if (park->bridge_crossing[direction] == 0 && 
        park->bridge_crossing[1 - direction] == 0) {
        park->bridge_direction = DIR_NONE;
    }
    
    sem_unlock(sem_id, SEM_MOST_MUTEX);
    
    // zwalniamy miejsce na moscie
    sem_unlock(sem_id, SEM_MOST_LIMIT);
    
    printf("[TURYSTA %d] Zszedłem z mostu\n", id);
}

// wejscie na wieze
void do_tower(int id, int age, int is_vip, struct ParkSharedMemory *park, int sem_id) {
    
    // dzieci <= 5 lat nie wchodza
    if (age <= 5) {
        printf("[TURYSTA %d] Mam %d lat - nie mogę wejść na wieżę. Czekam na dole.\n", id, age);
        //sleep(TOWER_VISIT_TIME);
        return;
    }
    
    printf("[TURYSTA %d] Podchodzę do wieży\n", id);
    
    // dzieci < 15 lat - komunikat o opiece
    if (age < 15) {
        printf("[TURYSTA %d] Mam %d lat - wchodzę na wieżę pod opieką dorosłego\n", id, age);
    }
    
    // vip ma priorytet
    if (is_vip) {
        printf("[TURYSTA %d] Jestem VIPem - omijam kolejkę do wieży!\n", id);
    }
    
    // czekamy na wolne miejsce
    // vip tez musi czekac na limit ale "omija kolejke" = wchodzi jak jest miejsce
    int entered = 0;
    while (!entered && !emergency_exit_flag) {
        if (sem_trylock(sem_id, SEM_WIEZA_LIMIT) == 0) {
            entered = 1;
        } else {
            //usleep(100000);
        }
    }
    
    if (emergency_exit_flag) {
        printf("[TURYSTA %d] Ewakuacja! Rezygnuję z wieży.\n", id);
        return;
    }
    
    // wchodzimy na wieze
    sem_lock(sem_id, SEM_WIEZA_MUTEX);
    park->tower_current_count++;
    tower_add_visitor(park, getpid());
    printf("[TURYSTA %d] Wchodzę na wieżę (%d/%d osób)\n", id, park->tower_current_count, X2_TOWER_CAP);
    sem_unlock(sem_id, SEM_WIEZA_MUTEX);
    
    // zwiedzanie wiezy
    printf("[TURYSTA %d] Podziwiam widoki z wieży...\n", id);
    
    int visit_time = TOWER_VISIT_TIME;
    tower_evacuation_flag = 0; // reset flagi przed zwiedzaniem
    
    while (visit_time > 0 && !tower_evacuation_flag && !emergency_exit_flag) {
        sleep(1);
        visit_time--;
    }
    
    // sprawdzamy czy byla ewakuacja
    if (tower_evacuation_flag) {
        printf("[TURYSTA %d] Ewakuacja wieży! Natychmiast schodzę!\n", id);
        tower_evacuation_flag = 0; // reset
    }
    
    // zejscie z wiezy
    sem_lock(sem_id, SEM_WIEZA_MUTEX);
    park->tower_current_count--;
    tower_remove_visitor(park, getpid());
    sem_unlock(sem_id, SEM_WIEZA_MUTEX);
    
    sem_unlock(sem_id, SEM_WIEZA_LIMIT);
    
    printf("[TURYSTA %d] Zszedłem z wieży\n", id);
}

// przeprawa promem
void do_ferry(int id, int my_group_id, int age, int is_vip, int destination, struct ParkSharedMemory *park, int sem_id) {
    
    printf("[TURYSTA %d] Podchodzę do promu (chcę na brzeg %d)\n", id, destination);
    
    // dzieci < 15 lat - komunikat o opiece
    if (age < 15) {
        printf("[TURYSTA %d] Mam %d lat - wsiadam na prom pod opieką dorosłego\n", id, age);
    }
    
    // vip ma priorytet
    if (is_vip) {
        printf("[TURYSTA %d] Jestem VIPem - omijam kolejkę do promu!\n", id);
    }
    
    // czekamy na prom po naszej stronie i wolne miejsce
    int on_ferry = 0;
    int my_shore = 1 - destination; // jesli chcemy na brzeg 1 stoimy na brzegu 0
    
    while (!on_ferry && !emergency_exit_flag) {
        sem_lock(sem_id, SEM_PROM_MUTEX);
        
        // sprawdzamy czy prom jest po naszej stronie i nie plynie
        if (park->ferry_position == my_shore && 
            !park->ferry_moving &&
            park->ferry_group_id == my_group_id) {
            // probujemy wejsc
            if (sem_trylock(sem_id, SEM_PROM_LIMIT) == 0) {
                park->ferry_current_count++;
                on_ferry = 1;
                printf("[TURYSTA %d] Wsiadłem na prom (%d/%d osob)\n", id, park->ferry_current_count, X3_FERRY_CAP);
            }
        } else {
            // prom jest gdzie indziej lub plynie
            park->ferry_waiting[my_shore]++;
        }
        
        sem_unlock(sem_id, SEM_PROM_MUTEX);
        
        if (!on_ferry && !emergency_exit_flag) {
            //usleep(200000);
            
            // zmniejsz licznik czekajacych po sprawdzeniu
            sem_lock(sem_id, SEM_PROM_MUTEX);
            park->ferry_waiting[my_shore]--;
            sem_unlock(sem_id, SEM_PROM_MUTEX);
        }
    }
    
    if (emergency_exit_flag) {
        printf("[TURYSTA %d] Ewakuacja! Rezygnuję z promu.\n", id);
        return;
    }
    
    // czekamy az prom doplynie
    printf("[TURYSTA %d] Czekam na odpłynięcie promu...\n", id);
    
    // czekamy na sygnal od przewodnika ze prom doplynal
    int travel_time = FERRY_TRAVEL_TIME;
    while (travel_time > 0 && !emergency_exit_flag) {
        //sleep(1);
        travel_time--;
    }
    
    // wysiadamy z promu
    sem_lock(sem_id, SEM_PROM_MUTEX);
    park->ferry_current_count--;
    sem_unlock(sem_id, SEM_PROM_MUTEX);
    
    sem_unlock(sem_id, SEM_PROM_LIMIT);
    
    printf("[TURYSTA %d] Wysiadłem z promu na brzegu %d\n", id, destination);
}

int main(int argc, char* argv[]) {
    // walidacja argumentow
    if (argc < 2) {
        printf("[TURYSTA] Błąd: Brak ID turysty! Uruchamiaj przez main.\n");
        exit(1);
    }
    
    int id = atoi(argv[1]);
    g_id = id; // dla handlerow sygnalow
    
    // rejestracja handlerow sygnalow
    struct sigaction sa1;
    sa1.sa_handler = sigusr1_handler;
    sigemptyset(&sa1.sa_mask);
    sa1.sa_flags = 0; // nie uzywamy SA_RESTART zeby sleep() zostal przerwany
    sigaction(SIGUSR1, &sa1, NULL);
    
    struct sigaction sa2;
    sa2.sa_handler = sigusr2_handler;
    sigemptyset(&sa2.sa_mask);
    sa2.sa_flags = 0;
    sigaction(SIGUSR2, &sa2, NULL);
    
    // losowanie atrybutow turysty
    srand(time(NULL) + id);
    
    int age = (rand() % 68) + 3; // wiek 3-70 lat
    int is_vip = (rand() % 100) < 10; // 10% szans na VIP
    
    // polaczenie z zasobami ipc
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
    
    // zapisujemy globalne referencje dla funkcji atrakcji
    g_sem_id = sem_id;
    g_park = park;
    
    // wyswietlenie informacji o turyscie
    if (is_vip) {
        printf("[TURYSTA %d] Jestem VIPem (wiek: %d). Mam legitymację PTTK!\n", id, age);
    } else if (age < 7) {
        printf("[TURYSTA %d] Jestem dzieckiem (wiek: %d). Wchodzę za darmo!\n", id, age);
    } else {
        printf("[TURYSTA %d] Przychodzę do parku (wiek: %d).\n", id, age);
    }
    
    // wejscie do parku
    printf("[TURYSTA %d] Jestem przed kasą. Czekam na bilet...\n", id);
    
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
    
    // wejscie do parku
    sem_lock(sem_id, SEM_PARK_LIMIT);
    
    // aktualizacja statystyk
    sem_lock(sem_id, SEM_STATS_MUTEX);
    park->people_in_park++;
    if (is_vip) {
        park->vip_in_park++;
    }
    sem_unlock(sem_id, SEM_STATS_MUTEX);
    
    printf("[TURYSTA %d] Wszedłem do parku! Idę do punktu zbiórki.\n", id);
    
    // dolaczenie do kolejki na grupe
    sem_lock(sem_id, SEM_QUEUE_MUTEX);
    
    // zapisujemy sie do kolejki
    int my_position = park->people_in_queue;
    park->queue_ages[my_position] = age;
    park->queue_vips[my_position] = is_vip;
    park->queue_pids[my_position] = getpid();
    park->queue_ids[my_position] = id;
    park->people_in_queue++;
    
    int current_count = park->people_in_queue;
    
    // jesli to ostatni turysta w grupie resetujemy licznik
    if (current_count == M_GROUP_SIZE) {
        park->people_in_queue = 0;
    }
    
    sem_unlock(sem_id, SEM_QUEUE_MUTEX);
    
    printf("[TURYSTA %d] Czekam na przewodnika. (Kolejka: %d/%d)\n", id, current_count, M_GROUP_SIZE);
    
    // budzenie przewodnika
    if (current_count == M_GROUP_SIZE) {
        printf("[TURYSTA %d] Komplet! Budzę przewodnika!\n", id);
        sem_unlock(sem_id, SEM_PRZEWODNIK);
    }
    
    // czekanie na przydzielenie do grupy

    // przewodnik przydzieli nas do grupy i obudzi przez SEM_GROUP_START
    // musimy znalezc nasz group_id - szukamy siebie w groups[]
    
    int my_group_id = -1;
    
    // czekamy az przewodnik nas przydzieli
    while (my_group_id == -1 && !emergency_exit_flag) {
        //usleep(50000);
        
        // szukamy siebie w aktywnych grupach
        for (int g = 0; g < MAX_GROUPS; g++) {
            if (park->groups[g].active) {
                for (int m = 0; m < M_GROUP_SIZE; m++) {
                    if (park->groups[g].member_pids[m] == getpid()) {
                        my_group_id = g;
                        break;
                    }
                }
            }
            if (my_group_id != -1) break;
        }
    }
    
    if (emergency_exit_flag) {
        printf("[TURYSTA %d] Ewakuacja przed wycieczką!\n", id);
        goto cleanup;
    }
    
    printf("[TURYSTA %d] Przydzielony do grupy %d. Czekam na start.\n", id, my_group_id);
    
    // czekamy na sygnal startu od przewodnika
    sem_lock(sem_id, SEM_GROUP_START(my_group_id));
    
    // petla wycieczki
    struct GroupState *my_group = &park->groups[my_group_id];
    int route = my_group->route;
    
    printf("[TURYSTA %d] Wycieczka start! Trasa %d\n", id, route);
    
    for (int step = 0; step < 3 && !emergency_exit_flag; step++) {
        // czekamy na sygnal od przewodnika ze mozemy isc do atrakcji
        int attraction = get_attraction_for_step(route, step);
        
        printf("[TURYSTA %d] Faza %d: idę do atrakcji %d\n", id, step + 1, attraction);
        
        // wykonaj odpowiednia atrakcje
        switch (attraction) {
            case ATTR_BRIDGE:
                do_bridge(id, age, is_vip, get_bridge_direction(route), park, sem_id);
                break;
                
            case ATTR_TOWER:
                do_tower(id, age, is_vip, park, sem_id);
                break;
                
            case ATTR_FERRY:
                do_ferry(id, my_group_id, age, is_vip, get_ferry_direction(route), park, sem_id);
                break;
        }
        
        // sprawdz czy byla ewakuacja
        if (emergency_exit_flag) {
            printf("[TURYSTA %d] Ewakuacja! Przerywam zwiedzanie.\n", id);
            break;
        }
        
        // sygnalizujemy przewodnikowi ze skonczylismy atrakcje
        sem_unlock(sem_id, SEM_GROUP_DONE(my_group_id));
        
        printf("[TURYSTA %d] Zakończyłem atrakcje %d. Czekam na grupę.\n", id, attraction);
        
        // czekamy na reszte grupy
        if (step < 2) {  // nie czekamy po ostatniej atrakcji
            sem_lock(sem_id, SEM_GROUP_START(my_group_id));
        }
    }
    
    // ostatni sygnal - koniec wycieczki
    if (!emergency_exit_flag) {
        sem_unlock(sem_id, SEM_GROUP_DONE(my_group_id));
    }
    
    printf("[TURYSTA %d] Koniec wycieczki. Wracam do kasy.\n", id);
    
    // wyjscie z parku
cleanup:
    printf("[TURYSTA %d] Wychodzę z parku.\n", id);
    
    // komunikat do kasjera
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
    
    // aktualizacja statystyk
    sem_lock(sem_id, SEM_STATS_MUTEX);
    if (park->people_in_park > 0) {
        park->people_in_park--;
    }
    if (is_vip && park->vip_in_park > 0) {
        park->vip_in_park--;
    }
    sem_unlock(sem_id, SEM_STATS_MUTEX);
    
    // zwalniamy miejsce w parku
    sem_unlock(sem_id, SEM_PARK_LIMIT);
    
    printf("[TURYSTA %d] Do widzenia!\n", id);
    
    // odlaczenie pamieci dzielonej
    shmdt(park);
    
    return 0;
}