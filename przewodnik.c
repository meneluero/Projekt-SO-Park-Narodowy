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
void cross_bridge(int guide_id, int direction, struct ParkSharedMemory *park, int sem_id, int ages[]) {
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

        // szukamy czy ktos jeszcze nie przeszedl
        for(int i=0; i<M_GROUP_SIZE; i++) {
            if (!processed[i]) {
                t1_idx = i;
                break;
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
                printf("[MOST] Wchodzą: Dziecko (lat %d) + Opiekun (lat %d)\n", local_ages[t1_idx], local_ages[t2_idx]);
                
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
                printf("[MOST] Dziecko (lat %d) idzie pod opieką przewodnika (brak wolnych opiekunów).\n", local_ages[t1_idx]);
                enter_bridge(guide_id, direction, park, sem_id);
                usleep(400000);
                exit_bridge(park, sem_id);
                processed[t1_idx] = 1;
                remaining--;
            }
        } else {
            // dorosly idzie sam
            printf("[MOST] Wchodzi turysta (lat %d)\n", local_ages[t1_idx]);
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
void visit_tower(int guide_id, struct ParkSharedMemory *park, int sem_id, int ages[], pid_t pids[]) {
    printf("[WIEŻA] Przewodnik %d dociera pod wieżę widokową.\n", guide_id);

    // filtracja: kto zostaje na dole
    // potrzebujemy tablicy flag: 1 = wchodzi, 0 = zostaje
    int can_enter[M_GROUP_SIZE];
    for(int i=0; i<M_GROUP_SIZE; i++) can_enter[i] = 1;
    
    int rejected_count = 0;

    // szukamy dzieci < 5 lat
    for (int i = 0; i < M_GROUP_SIZE; i++) {
        if (ages[i] < 5) {
            printf("[WIEŻA] Turysta %d (lat %d) jest za mały (<5 lat). Zostaje na dole.\n", pids[i], ages[i]);
            can_enter[i] = 0;
            rejected_count++;
            
            // musimy znalezc mu opiekuna (osoba >= 15 lat) ktora tez zostanie
            int guardian_found = 0;
            // najpierw szukamy kogos kto jeszcze wchodzi (zeby go wykluczyc)
            for (int j = 0; j < M_GROUP_SIZE; j++) {
                if (i != j && can_enter[j] == 1 && ages[j] >= 15) {
                    can_enter[j] = 0; // opiekun tez zostaje
                    printf("[WIEŻA] Turysta %d (lat %d) zostaje jako opiekun dla dziecka.\n", pids[j], ages[j]);
                    guardian_found = 1;
                    rejected_count++;
                    break;
                }
            }
            if (!guardian_found) {
                printf("[WIEŻA] UWAGA: Dziecko bez dedykowanego opiekuna! Zostaje z przewodnikiem.\n");
            }
        }
    }

    if (rejected_count == M_GROUP_SIZE) {
        printf("[WIEŻA] Nikt z grupy nie może wejść na wieżę! Idziemy dalej.\n");
        return;
    }

    // sortowanie vip
    // tworzymy liste indeksow do wejscia
    int entry_order[M_GROUP_SIZE];
    int count_to_enter = 0;
    
    // najpierw dodajemy vip-ow (dla symulacji przyjmijmy vip jesli wiek dzieli sie przez 10 lub > 60)
    // uproszczenie: uznajemy osoby starsze > 60 za priorytet
    for (int i = 0; i < M_GROUP_SIZE; i++) {
        if (can_enter[i] && ages[i] > 60) {
            entry_order[count_to_enter++] = i;
            printf("[WIEŻA] Turysta %d (lat %d) ma priorytet (Senior/VIP).\n", pids[i], ages[i]);
        }
    }
    // potem reszta
    for (int i = 0; i < M_GROUP_SIZE; i++) {
        if (can_enter[i] && ages[i] <= 60) {
            entry_order[count_to_enter++] = i;
        }
    }

    // proces wchodzenia na wieze
    printf("[WIEŻA] Przewodnik czeka na dole. Turyści wchodzą...\n");

    for (int k = 0; k < count_to_enter; k++) {
        int idx = entry_order[k];
        pid_t t_pid = pids[idx];
        int t_age = ages[idx];

        // czekamy na miejsce na wiezy (semafor licznikowy)
        sem_lock(sem_id, SEM_WIEZA_LIMIT);

        // rejestrujemy pid (sekcja krytyczna mutex)
        register_tower_entry(park, sem_id, t_pid);
        printf("[WIEŻA] Turysta %d (lat %d) wchodzi na górę. (Liczba osób: %d)\n", 
               t_pid, t_age, park->tower_current_count);

        // symulacja zagrozenia
        if ((rand() % 100) < 2) {
            printf("\n[WIEŻA] Awaria konstrukcji! Przewodnik zarządza ewakuację!\n");
            
            // wysylamy sygnal do wszystkich obecnie bedacych na wiezy
            sem_lock(sem_id, SEM_WIEZA_MUTEX);
            for(int j=0; j<N_PARK_CAPACITY; j++) {
                if (park->tower_visitors[j] != 0) {
                    kill(park->tower_visitors[j], SIGUSR1);
                    printf("[WIEŻA] Przewodnik wysłał SIGUSR1 do %d\n", park->tower_visitors[j]);
                }
            }
            sem_unlock(sem_id, SEM_WIEZA_MUTEX);
            
            // w przypadku awarii przerywamy zwiedzanie dla reszty grupy
            // musimy jednak zwolnic semafor dla tego ktory wlasnie "wszedl"
            unregister_tower_exit(park, sem_id, t_pid);
            sem_unlock(sem_id, SEM_WIEZA_LIMIT);
            
            printf("[WIEŻA] Grupa ewakuowana. Uciekamy od wieży!\n");
            return; // koniec atrakcji
        }

        // zwiedzanie
        // normalnie robi to proces turysty, ale tutaj symulujemy czas zajetosci zasobu
        usleep(300000 + (rand() % 300000)); 

        // wyjscie
        unregister_tower_exit(park, sem_id, t_pid);
        sem_unlock(sem_id, SEM_WIEZA_LIMIT); // zwalniamy miejsce dla innych
        
        printf("[WIEŻA] Turysta %d zszedł z wieży.\n", t_pid);
    }
    
    printf("[WIEŻA] Wszyscy chętni zwiedzili wieżę. Idziemy dalej.\n");
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
    int sem_id = semget(SEM_KEY_ID, 5, 0666);
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
        pid_t current_group_pids[M_GROUP_SIZE];

        for(int i=0; i<M_GROUP_SIZE; i++) {
            current_group_ages[i] = park->group_ages[i];
            current_group_pids[i] = park->group_pids[i];
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
        
        // losowanie trasy (1 lub 2)
        int route = (rand() % 2) + 1;
        
        printf("[PRZEWODNIK %d] Wybieram trasę %d\n", id, route);
        
        if (route == 1) {
            printf("[PRZEWODNIK %d] Trasa: K → A → B → C → K\n", id);
            // do zrobienia: implementacja trasy 1
            cross_bridge(id, 0, park, sem_id, current_group_ages);
            visit_tower(id, park, sem_id, current_group_ages, current_group_pids);
            // take_ferry(id);
        } else {
            printf("[PRZEWODNIK %d] Trasa: K → C → B → A → K\n", id);
            // do zrobienia: implementacja trasy 2
            // take_ferry(id);
            visit_tower(id, park, sem_id, current_group_ages, current_group_pids);
            cross_bridge(id, 1, park, sem_id, current_group_ages);
        }
        
        // symulacja wycieczki
        int tour_time = 3 + (rand() % 4); // 3-6 sekund
        printf("[PRZEWODNIK %d] Oprowadzam wycieczkę (czas: %ds)...\n", id, tour_time);
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