/**
 * X36OSY - Producenti a konzumenti 
 * 
 * vypracoval: Tomas Horacek <horact1@fel.cvut.cz>
 *
 */

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <queue>
#include <signal.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
using namespace std;

struct Prvek;
struct Fronta;
struct Shared;

int M_PRODUCENTU = 9;
int N_KONZUMENTU = 3;
int K_POLOZEK = 4;
int LIMIT_PRVKU = 50;
int NUM_CHILDREN = M_PRODUCENTU + N_KONZUMENTU;
int MAX_NAHODNA_DOBA = 5000000;
const int MAX_NAME_SIZE = 30;

pid_t *child_pids;
int shared_mem_segment_id;
Fronta **fronty;
char process_name[MAX_NAME_SIZE];
int sem_pristup_ke_fronte;
int sem_mohu_vlozit;
Shared *shared;

struct Prvek {
    int cislo_producenta;
    int poradove_cislo;
    int pocet_precteni;
    
    Prvek() :
        cislo_producenta(0),
        poradove_cislo(0),
        pocet_precteni(0) { }
    
    Prvek(int cislo_producenta, int poradove_cislo) :
        cislo_producenta(cislo_producenta),
        poradove_cislo(poradove_cislo),
        pocet_precteni(0) { }
    
    void init(int _cislo_producenta, int _poradove_cislo) {
        this->cislo_producenta = _cislo_producenta;
        this->poradove_cislo = _poradove_cislo;
        this->pocet_precteni = 0;
    }
};

class Fronta {
    /**
     * Pametove rozlozeni fronty:
    
    Fronta:
        Instance tridy Fronta:
            [_size]
            [_index_of_first]
        Prvky fronty: (nasleduji okamzite za instanci Fronta)
            [instance 0 (tridy Prvek) fronty]
            [instance 1 (tridy Prvek) fronty]

            ...

            [instance K_POLOZEK - 1 (tridy Prvek) fronty] (fixni pocet prvku)

     */
    unsigned int _size;
    unsigned int _index_of_first;
    
    Prvek *_get_array_of_prvky() {
        unsigned char *tmp_ptr = (unsigned char *) this;
        tmp_ptr += sizeof(Fronta);
        
        return (Prvek *) tmp_ptr;
    }
    
    Prvek* next_back() {
        int i = (this->_index_of_first + this->_size) % K_POLOZEK;
        return &(this->_get_array_of_prvky()[i]);
    }
public:
    void init() {
        this->_size = 0;
        this->_index_of_first = 0;
    }
    
    int size() {
        return this->_size;
    }
        
    Prvek* front();
    void push(Prvek &prvek);
    void pop();
    
    static int get_total_sizeof() {
        return sizeof(Fronta) + sizeof(Prvek) * K_POLOZEK;
    }
};

typedef Fronta *p_Fronta;

struct Shared {
    /**
     *
     * Struktura sdilene pameti:
    
    Shared: 
        [pokracovat_ve_vypoctu]
    Fronta 0:
        [instance 0 tridy Fronta]
        [prveky fronty 0]
    Fronta 1:
        [instance 1 tridy Fronta]
        [prveky fronty 1]

    ...
    
    Fronta M_PRODUCENTU - 1:
        [instance M_PRODUCENTU - 1 tridy Fronta]
        [prveky fronty M_PRODUCENTU - 1]
     
     */
     
    volatile sig_atomic_t pokracovat_ve_vypoctu;
    
    static void connect_and_init_local_ptrs() {
        /** Pripoji pament a inicializuje lokalni ukazatele na sdilenou pamet. */
        
        shared = (Shared *) shmat(shared_mem_segment_id, NULL, NULL);
        
        fronty = new p_Fronta[M_PRODUCENTU];
        
        unsigned char *tmp_ptr = (unsigned char *) shared;
        tmp_ptr += sizeof(Shared);
        
        for (int i = 0; i < M_PRODUCENTU; i++) {
            fronty[i] = (Fronta *) tmp_ptr;
            tmp_ptr += Fronta::get_total_sizeof();
        }
    }
    
    static int get_total_sizeof() {
        int velikost_vesech_front = Fronta::get_total_sizeof() * M_PRODUCENTU;
        return sizeof(Shared) + velikost_vesech_front;
    }
};

void Fronta::push(Prvek &prvek) {
    if (this->_size >= K_POLOZEK) {
        shared->pokracovat_ve_vypoctu = 0;
        throw 2;
    }
    memcpy(this->next_back(), &prvek, sizeof(Prvek));
    this->_size++;
};

Prvek* Fronta::front() {
    if (this->_size <= 0) {
        shared->pokracovat_ve_vypoctu = 0;
        throw 1;
    }
    int i = _index_of_first % K_POLOZEK;
    return &this->_get_array_of_prvky()[i];
}

void Fronta::pop() {
    if (this->_size <= 0) {
        shared->pokracovat_ve_vypoctu = 0;
        throw 3;
    }
    this->_size--;
    this->_index_of_first = (this->_index_of_first + 1) % K_POLOZEK;
}

void pockej_nahodnou_dobu() {
    double result = 0.0;
    int doba = random() % MAX_NAHODNA_DOBA;
    
    for (int j = 0; j < doba; j++)
        result = result + (double)random();
}

void alloc_shared_mem() {
    int shared_segment_size = Shared::get_total_sizeof();
    printf("%s alokuje pamet velikosti %dB.\n", process_name, shared_segment_size);
    shared_mem_segment_id = shmget(IPC_PRIVATE, shared_segment_size,
        IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR);
}

void dealloc_shared_mem() {
    shmctl(shared_mem_segment_id, IPC_RMID, NULL);
}

void wait_for_all_children() {
    int status;
    
    for (int i = 0; i < NUM_CHILDREN; i++) {
        wait(&status);
    }
}

void singnal_handler(int signal_number) {
    printf("%s prijal signal %d\n", process_name, signal_number);
    shared->pokracovat_ve_vypoctu = 0;
}

void init_sinals() {
    signal(SIGQUIT, singnal_handler);
    signal(SIGINT, singnal_handler);
    signal(SIGHUP, singnal_handler);
    signal(SIGTERM, singnal_handler);
}

int semaphore_allocation(int semnum) {
    int sem_flags = IPC_CREAT | IPC_EXCL | SEM_R | SEM_A | SEM_R>>3 | SEM_A>>3 | SEM_R>>6 | SEM_A>>6;
    int ret_val = semget(IPC_PRIVATE, semnum, sem_flags);
    
    if (ret_val < 0) {
        printf("Nepodarilo se inicializovat semafor.\n");
        exit(1);
    }
    
    return ret_val;
}

void semaphore_initialize(int semid, int semnum, int init_value) {
    semun argument;
    
    for (int i = 0; i < semnum; i++) {
        argument.val = init_value;
        semctl(semid, i, SETVAL, argument);
    }
}

int semaphore_deallocate(int semid, int semnum) {
    semun ignored_argument; 
    return semctl(semid, semnum, IPC_RMID, ignored_argument); 
}

int semaphore_down(int semid, int sem_index) {
    /* Wait on a binary semaphore. Block until the semaphore
       value is positive, then decrement it by one. */
    sembuf operations[1];
    operations[0].sem_num = sem_index;
    operations[0].sem_op = -1;
    operations[0].sem_flg = SEM_UNDO;
    
    return semop(semid, operations, 1);
}


int semaphore_up(int semid, int sem_index) {
    /* Post to a semaphore: increment its value by one. This returns immediately. */ 
    sembuf operations[1];
    operations[0].sem_num = sem_index;
    operations[0].sem_op = 1;
    operations[0].sem_flg = SEM_UNDO;
    
    return semop(semid, operations, 1);
}

int semaphore_get(int semafor, int sem_index){
    return semctl(semafor, sem_index, GETVAL, 0);
}

void dealloc_shared_resources() {
    dealloc_shared_mem();
    semaphore_deallocate(sem_mohu_vlozit, M_PRODUCENTU);
    semaphore_deallocate(sem_pristup_ke_fronte, M_PRODUCENTU);
    printf("%s dealokoval sdilene prostredky.\n", process_name);
}

void child_init() {
    init_sinals();
    
    Shared::connect_and_init_local_ptrs();
}


void root_init() {
    child_pids = new pid_t[NUM_CHILDREN];
    alloc_shared_mem();
    
    init_sinals();
    
    Shared::connect_and_init_local_ptrs();
    
    shared->pokracovat_ve_vypoctu = 1;
    
    sem_mohu_vlozit = semaphore_allocation(M_PRODUCENTU);
    semaphore_initialize(sem_mohu_vlozit, M_PRODUCENTU, K_POLOZEK);
        
    sem_pristup_ke_fronte = semaphore_allocation(M_PRODUCENTU);
    semaphore_initialize(sem_pristup_ke_fronte, M_PRODUCENTU, 1);
    
    for (int i = 0; i < M_PRODUCENTU; i++) {
        printf("Semafory nastaveny na: sem_mohu_vlozit(%d), sem_pristup_ke_fronte(%d)\n",
            semaphore_get(sem_mohu_vlozit, i),
            semaphore_get(sem_pristup_ke_fronte, i));
    }
}


void root_finish() {
    wait_for_all_children();
    
    dealloc_shared_resources();
    delete[] child_pids;
}

void zamkni_frontu(int cislo_fronty) {
    semaphore_down(sem_pristup_ke_fronte, cislo_fronty);
}

void odemkni_frontu(int cislo_fronty) {
    semaphore_up(sem_pristup_ke_fronte, cislo_fronty);
}

void pockej_na_moznost_vkladat_prvek(int cislo_fronty) {
    semaphore_down(sem_mohu_vlozit, cislo_fronty);
}

void umozni_vlozeni_prvku(int cislo_fronty) {
    semaphore_up(sem_mohu_vlozit, cislo_fronty);
}


/**
 * void producent(int cislo_producenta)
 *
 * pseudokod:
 *
def producent():
    for poradove_cislo in range(LIMIT_PRVKU):
        pockej_na_moznost_vkladat_prvek()
        
        zamkni_frontu()
        pridej_prvek_do_fronty()
        odemkni_frontu()
        
        
        pockej_nahodnou_dobu()
    
    ukonci_vlakno()
 */
void producent(int cislo_producenta) {
    snprintf(process_name, MAX_NAME_SIZE, "PRODUCENT %d [PID:%d]", cislo_producenta, (int) getpid());
    printf("%s zacatek.\n", process_name);
    
    child_init();
    
    Prvek prvek;
    int velikos_fronty;
    
    for (int poradove_cislo = 0; poradove_cislo < LIMIT_PRVKU; poradove_cislo++) {
        if (shared->pokracovat_ve_vypoctu) {
            prvek.init(cislo_producenta, poradove_cislo);
            
            zamkni_frontu(cislo_producenta);
            
            velikos_fronty = fronty[cislo_producenta]->size();
            
            if (velikos_fronty >= K_POLOZEK) {
                odemkni_frontu(cislo_producenta);
                
                printf("%s ceka na uvolneni fronty.\n", process_name);
                pockej_na_moznost_vkladat_prvek(cislo_producenta);
                printf("%s muze vlozit do fronty.\n", process_name);
                
                zamkni_frontu(cislo_producenta);
            } else {
                pockej_na_moznost_vkladat_prvek(cislo_producenta);
            }
            
            // pridej_prvek_do_fronty()
            fronty[cislo_producenta]->push(prvek);
            velikos_fronty = fronty[cislo_producenta]->size();
            
            odemkni_frontu(cislo_producenta);
            
            printf("%s pridal prvek %i - velikos fronty %i\n",
               process_name, poradove_cislo, velikos_fronty);
            
            pockej_nahodnou_dobu();
        } else {
            break;
        }
    }
    
    printf("%s done.\n", process_name);
}

/**
 * void konzument(int cislo_konzumenta)
 *
 * pseudokod:
 *
def konzument():
    while True:
        pockej_nahodnou_dobu()
        
        ukonci_cteni = True
        for producent in range(M_PRODUCENTU):
            zamkni_frontu()
            if not fornta_je_prazdna():
                prvek = fronta[producent].front()
                
                if prvek.poradove_cislo != poradova_cisla_poslednich_prectenych_prveku[producent]:
                    prvek.pocet_precteni++
                    
                    if prvek.pocet_precteni == N_KONZUMENTU:
                        fronta[producent].pop()
                        delete prvek;
                        zavolej_uvolneni_prvku()
                    
                    poradova_cisla_poslednich_prectenych_prveku[producent] = prvek.poradove_cislo
            odemkni_frontu()
            
            if ukonci_cteni:
                ukonci_cteni = poradova_cisla_poslednich_prectenych_prveku[producent] == (LIMIT_PRVKU - 1)
            
        if ukonci_cteni:
            ukonci_vlakno()
 */
void konzument(int cislo_konzumenta) {
    snprintf(process_name, MAX_NAME_SIZE, "Konzument %d [PID:%d]", cislo_konzumenta, (int) getpid());
    printf("%s zacatek.\n", process_name);
    
    child_init();
    
    int *poradova_cisla_poslednich_prectenych_prveku = new int[M_PRODUCENTU];
    
    for (int cislo_producenta = 0; cislo_producenta < M_PRODUCENTU; cislo_producenta++) {
        poradova_cisla_poslednich_prectenych_prveku[cislo_producenta] = -1;
    }
    
    Prvek *p_prvek;
    int prectene_poradove_cislo;
    int pocet_precteni;
    int velikos_fronty;
    int prectene_cislo_producenta;
    int nova_velikos_fronty;
    bool nove_nacteny = false;
    bool prvek_odstranen = false;
    
    bool ukonci_cteni = false;
    while (shared->pokracovat_ve_vypoctu && ! ukonci_cteni) {
        pockej_nahodnou_dobu();
        
        ukonci_cteni = true;
        for (int cislo_producenta = 0; cislo_producenta < M_PRODUCENTU; cislo_producenta++) {
            zamkni_frontu(cislo_producenta);
            velikos_fronty = fronty[cislo_producenta]->size();
            
            if (velikos_fronty > 0) {
                p_prvek = fronty[cislo_producenta]->front();
                
                prectene_poradove_cislo = p_prvek->poradove_cislo;
                prectene_cislo_producenta = p_prvek->cislo_producenta;
                
                if (prectene_poradove_cislo
                        != poradova_cisla_poslednich_prectenych_prveku[cislo_producenta]) {
                        nove_nacteny = true;
                        pocet_precteni = ++(p_prvek->pocet_precteni);

                        if (pocet_precteni >= N_KONZUMENTU) {
                            fronty[cislo_producenta]->pop();
                            nova_velikos_fronty = fronty[cislo_producenta]->size();
                            umozni_vlozeni_prvku(prectene_cislo_producenta);
                            
                            
                            prvek_odstranen = true;
                        }

                        poradova_cisla_poslednich_prectenych_prveku[cislo_producenta]
                            = prectene_poradove_cislo;
                    }
            }
            odemkni_frontu(cislo_producenta);
            
            /*
            if (!nove_nacteny && !prvek_odstranen) {
                printf("%s cte z fronty %i prvek %i - pocet precteni %i - POSLEDNI PRECTENY %d - NEDELE NIC - N_KONZUMENTU: %d.\n",
                    process_name,
                    cislo_producenta,
                    prectene_poradove_cislo,
                    pocet_precteni,
                    poradova_cisla_poslednich_prectenych_prveku[cislo_producenta],
                    N_KONZUMENTU);
                
                for (int i = 0; i < M_PRODUCENTU; i++) {
                    printf("Semafory nastaveny na: sem_mohu_vlozit(%d), sem_pristup_ke_fronte(%d)\n",
                        semaphore_get(sem_mohu_vlozit, i),
                        semaphore_get(sem_pristup_ke_fronte, i));
                }
            }
            */
            if (nove_nacteny) {
                printf("%s cte z fronty %i prvek %i - pocet precteni %i\n",
                    process_name,
                    cislo_producenta,
                    prectene_poradove_cislo,
                    pocet_precteni);
                nove_nacteny = false;
            }
            
            if (prvek_odstranen) {
                printf("%s odsranil z fronty %i prvek %i - velikost fronty %i\n",
                    process_name,
                    cislo_producenta,
                    prectene_poradove_cislo,
                    nova_velikos_fronty);
                prvek_odstranen = false;
            }
            
            if (ukonci_cteni) {
                ukonci_cteni =
                    poradova_cisla_poslednich_prectenych_prveku[cislo_producenta] == (LIMIT_PRVKU - 1);
            }
        }
    }
    
    printf("%s done.\n", process_name);
}

int main(int argc, char * const argv[]) {
    if (argc != 1) {
        sscanf(argv[1], "%d", &M_PRODUCENTU);
        sscanf(argv[2], "%d", &N_KONZUMENTU);
        sscanf(argv[3], "%d", &K_POLOZEK);
        sscanf(argv[4], "%d", &LIMIT_PRVKU);
        NUM_CHILDREN = M_PRODUCENTU + N_KONZUMENTU;
    } else {
        printf("Usage:\n\t %s M_PRODUCENTU N_KONZUMENTU K_POLOZEK LIMIT_PRVKU\n\n", argv[0]);
    }
    
    pid_t root_pid = getpid();
    bool exception = false;
    
    try {
        pid_t child_pid;
    
        snprintf(process_name, MAX_NAME_SIZE, "Hlavni proces [PID:%d]", (int) getpid());
        printf ("%s.\n", process_name, (int) root_pid);
        
        root_init();
    
        printf("Velikos Shared: %d, velikost Fronta: %d, velikost Prvek: %d.\n",
            Shared::get_total_sizeof(), Fronta::get_total_sizeof(), sizeof(Prvek));
    
        printf("Shared addr: %p (%d).\n", shared, (unsigned int) shared);
    
        for (int i = 0; i < M_PRODUCENTU; i++) {
            printf("Fronta %d addr: %p (relativni: %d).\n",
                i , fronty[i], (unsigned int) fronty[i] - (unsigned int) shared);
        
            fronty[i]->init();
        
            child_pid = fork();
            if (child_pid != 0) {
                child_pids[i] = child_pid;
            } else {
                producent(i);
                exit(0);
            }
        }
    
        for (int i = 0; i < N_KONZUMENTU; i++) {
            child_pid = fork ();
            if (child_pid != 0) {
                child_pids[i + M_PRODUCENTU] = child_pid;
            } else {
                konzument(i);
                exit(0);
            }
        }
    } catch(...) {
        exception = true;
    }
    
    if (getpid() == root_pid) {
        atexit(root_finish);
    }
    
    if ( ! exception) {
        printf("All OK. Done.\n");
        return 0;
    } else {
        printf("--------------------------------- %s exception caught in main(). Done.\n", process_name);
        return 1;
    }
}