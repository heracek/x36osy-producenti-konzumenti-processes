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

const int M_PRODUCENTU = 5;
const int N_KONZUMENTU = 3;
const int K_POLOZEK = 3;
const int LIMIT_PRVKU = 10;
const int MAX_NAME_SIZE = 30;
const int NUM_CHILDREN = M_PRODUCENTU + N_KONZUMENTU;
const int MAX_NAHODNA_DOBA = 50000;

pid_t *child_pids;
int shared_mem_segment_id;
Shared *shared;
Fronta **fronty;
char process_name[MAX_NAME_SIZE];

struct Prvek {
    int cislo_producenta;
    int poradove_cislo;
    int pocet_precteni;
    
    Prvek(int cislo_producenta, int poradove_cislo) :
        cislo_producenta(cislo_producenta),
        poradove_cislo(poradove_cislo),
        pocet_precteni(0) { }
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
public:
    void init() {
        this->_size = 0;
        this->_index_of_first = 0;
    }
    
    int size() {
        return this->_size;
    }
    
    int full() {
        return this->_size == K_POLOZEK;
    }
    
    int empty() {
        return this->_size == 0;
    }
    
    void front() {
        
    }
    
    void push(Prvek &prvek) {
        
    }
    
    void pop() {
        
    }
    
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

pthread_cond_t condy_front[M_PRODUCENTU];
pthread_mutex_t mutexy_front[M_PRODUCENTU];
queue<Prvek*> xfronty[M_PRODUCENTU];

void pockej_nahodnou_dobu() {
    double result = 0.0;
    int doba = random() % MAX_NAHODNA_DOBA;
    
    for (int j = 0; j < doba; j++)
        result = result + (double)random();
}

/**
 * void *my_producent(void *idp)
 *
 * pseudokod:
 *
def my_producent():
    for cislo_prvku in range(LIMIT_PRVKU):
        while je_fronta_plna():
            pockej_na_uvolneni_prvku()
        
        zamkni_frontu()
        pridej_prvek_do_fronty()
        odemkni_frontu()
        
        pockej_nahodnou_dobu()
    
    ukonci_vlakno()
 */
void *xmy_producent(void *idp) {
    int cislo_producenta = *((int *) idp);
    Prvek *prvek;
    int velikos_fronty;
    for (int poradove_cislo = 0; poradove_cislo < LIMIT_PRVKU; poradove_cislo++) {
        prvek = new Prvek(cislo_producenta, poradove_cislo);
        
        pthread_mutex_lock(&mutexy_front[cislo_producenta]);
        
        while ((velikos_fronty = xfronty[cislo_producenta].size()) > K_POLOZEK) {
            printf("PRODUCENT %i je zablokovan - velikost fronty %i\n", cislo_producenta, velikos_fronty);
            pthread_cond_wait(&condy_front[cislo_producenta], &mutexy_front[cislo_producenta]);
        }
        
        xfronty[cislo_producenta].push(prvek);
        
        pthread_mutex_unlock(&mutexy_front[cislo_producenta]);
        
        printf("PRODUCENT %i pridal prvek %i - velikos fronty %i\n",
            cislo_producenta,
            poradove_cislo,
            velikos_fronty
            );
        
        pockej_nahodnou_dobu();
    }
    
    printf("PRODUCENT %i konec\n", cislo_producenta);
    pthread_exit(NULL);
}

/**
 * void *konzument(void *idp)
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
void *xkonzument(void *idp) {
    int cislo_konzumenta = *((int *) idp) - M_PRODUCENTU;
    bool ukonci_cteni;
    int cislo_producenta;
    int prectene_poradove_cislo;
    int prectene_cislo_producenta;
    int velikos_fronty;
    Prvek *prvek;
    int poradova_cisla_poslednich_prectenych_prveku[M_PRODUCENTU];
    bool prvek_odstranen = false;
    int nova_velikos_fronty;
    bool nove_nacteny = false;
    int pocet_precteni;
    
    for (int cislo_producenta = 0; cislo_producenta < M_PRODUCENTU; cislo_producenta++) {
        poradova_cisla_poslednich_prectenych_prveku[cislo_producenta] = -1;
    }
    
    while (1) {
        pockej_nahodnou_dobu();
        
        ukonci_cteni = true;
        for (cislo_producenta = 0; cislo_producenta < M_PRODUCENTU; cislo_producenta++) {
            pthread_mutex_lock(&mutexy_front[cislo_producenta]);
            
            if ( ! xfronty[cislo_producenta].empty()) {
                prvek = xfronty[cislo_producenta].front();
                velikos_fronty = xfronty[cislo_producenta].size();
                
                prectene_poradove_cislo = prvek->poradove_cislo;
                prectene_cislo_producenta = prvek->cislo_producenta;
                
                if (prectene_poradove_cislo
                    != poradova_cisla_poslednich_prectenych_prveku[cislo_producenta]) {
                    nove_nacteny = true;
                    pocet_precteni = ++(prvek->pocet_precteni);
                    
                    if (prvek->pocet_precteni == N_KONZUMENTU) {
                        xfronty[cislo_producenta].pop();
                        delete prvek;
                        
                        pthread_cond_signal(&condy_front[cislo_producenta]);
                        
                        prvek_odstranen = true;
                        nova_velikos_fronty = xfronty[cislo_producenta].size();
                        if (nova_velikos_fronty == (K_POLOZEK - 1)) {
                            pthread_cond_signal(&condy_front[cislo_producenta]);
                            printf("konzument %i odblokoval frontu %i - velikost fronty %i\n",
                                cislo_konzumenta,
                                cislo_producenta,
                                velikos_fronty);
                        }
                    }
                    
                    poradova_cisla_poslednich_prectenych_prveku[cislo_producenta]
                        = prectene_poradove_cislo;
                }
            }
            
            pthread_mutex_unlock(&mutexy_front[cislo_producenta]);
            
            if (nove_nacteny) {
                printf("konzument %i cte z fronty %i prvek %i - pocet precteni %i\n",
                    cislo_konzumenta,
                    cislo_producenta,
                    prectene_poradove_cislo,
                    pocet_precteni);
                nove_nacteny = false;
            }
            
            if (prvek_odstranen) {
                printf("konzument %i odsranil z fronty %i prvek %i - velikost fronty %i\n",
                    cislo_konzumenta,
                    cislo_producenta,
                    prectene_poradove_cislo,
                    nova_velikos_fronty);
                prvek_odstranen = false;
            }
            
            if (ukonci_cteni) {
                ukonci_cteni = poradova_cisla_poslednich_prectenych_prveku[cislo_producenta]
                    == (LIMIT_PRVKU - 1);
            }
        }
        
        if (ukonci_cteni) {
            printf("konzument %i konec\n", cislo_konzumenta);
            pthread_exit(NULL);
            return NULL;
        }
    }
    pthread_exit(NULL);
}

void producent(int cislo_producenta) {
    snprintf(process_name, MAX_NAME_SIZE, "Producent %d [PID:%d]", cislo_producenta, (int) getpid());
    printf("%s.\n", process_name);
    
    Shared::connect_and_init_local_ptrs();
    
    sleep(3);
    printf("%s done.\n", process_name);
}

void konzument(int cislo_konzumenta) {
    snprintf(process_name, MAX_NAME_SIZE, "Konzument %d [PID:%d]", cislo_konzumenta, (int) getpid());
    printf("%s.\n", process_name);
    
    Shared::connect_and_init_local_ptrs();
    
    sleep(3);
    printf("%s done.\n", process_name);
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

void root_init() {
    child_pids = new pid_t[NUM_CHILDREN];
    alloc_shared_mem();
}

void dealloc_shared_resources() {
    dealloc_shared_mem();
    printf("%s dealokoval sdilene prostredky.\n", process_name);
}

void root_finish() {
    wait_for_all_children();
    
    dealloc_shared_resources();
    delete[] child_pids;
}

void singnal_handler(int signal_number) {
    printf("%s prijal signal %d", process_name, signal_number);
    shared->pokracovat_ve_vypoctu = 0;
}

void init_sinals() {
    struct sigaction sa;
    memset (&sa, 0, sizeof (sa));
    sa.sa_handler = &singnal_handler;
    sigaction(SIGQUIT, &sa, NULL);
}

int semaphore_allocation(key_t key, int semnum, int sem_flags) {
    return semget(key, semnum, sem_flags);
}

int semaphore_initialize(int semid, int semnum, int init_value) {
    semun argument;
    u_short *values = new u_short[semnum];
    
    for (int i = 0; i < semnum; i++) {
        values[i] = init_value;
    }
    
    argument.array = values;
    int ret_val = semctl(semid, 0, SETALL, argument);
    
    delete[] values;
    return ret_val;
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
    operations[0].sem_flg= SEM_UNDO;
    
    return semop(semid, operations, 1);
}

int main(int argc, char * const argv[]) {
    bool exception = false;
    
    try {
        pid_t root_pid = getpid();
        pid_t child_pid;
    
        snprintf(process_name, MAX_NAME_SIZE, "Hlavni proces [PID:%d]", (int) getpid());
        printf ("%s.\n", process_name, (int) root_pid);
        
        root_init();
    
        printf("Velikos Shared: %d, velikost Fronta: %d, velikost Prvek: %d.\n",
            Shared::get_total_sizeof(), Fronta::get_total_sizeof(), sizeof(Prvek));
    
        Shared::connect_and_init_local_ptrs();
    
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
    root_finish();
    
    if ( ! exception) {
        printf("All OK. Done.\n");
        return 0;
    } else {
        printf("Exception caught in main(). Done.\n");
        return 1;
    }
}