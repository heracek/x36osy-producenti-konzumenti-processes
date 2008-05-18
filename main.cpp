/**
 * X36OSY - Producenti a konzumenti 
 * 
 * vypracoval: Tomas Horacek <horact1@fel.cvut.cz>
 *
 */

#include <pthread.h>
#include <stdio.h>
#include <iostream>
#include <queue>
#include <sys/types.h>
#include <unistd.h>
using namespace std;

#define M_PRODUCENTU 5
#define N_KONZUMENTU 3
#define K_POLOZEK 3
#define LIMIT_PRVKU 10

#define NUM_THREADS M_PRODUCENTU + N_KONZUMENTU
#define MAX_NAHODNA_DOBA 50000

int thread_ids[NUM_THREADS];
pthread_cond_t condy_front[M_PRODUCENTU];
pthread_mutex_t mutexy_front[M_PRODUCENTU];
struct Prvek;
queue<Prvek*> fronty[M_PRODUCENTU];

struct Prvek {
    int cislo_producenta;
    int poradove_cislo;
    int pocet_precteni;
    
    Prvek(int cislo_producenta, int poradove_cislo) :
        cislo_producenta(cislo_producenta),
        poradove_cislo(poradove_cislo),
        pocet_precteni(0) { }
};

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
void *my_producent(void *idp) {
    int cislo_producenta = *((int *) idp);
    Prvek *prvek;
    int velikos_fronty;
    for (int poradove_cislo = 0; poradove_cislo < LIMIT_PRVKU; poradove_cislo++) {
        prvek = new Prvek(cislo_producenta, poradove_cislo);
        
        pthread_mutex_lock(&mutexy_front[cislo_producenta]);
        
        while ((velikos_fronty = fronty[cislo_producenta].size()) > K_POLOZEK) {
            printf("PRODUCENT %i je zablokovan - velikost fronty %i\n", cislo_producenta, velikos_fronty);
            pthread_cond_wait(&condy_front[cislo_producenta], &mutexy_front[cislo_producenta]);
        }
        
        fronty[cislo_producenta].push(prvek);
        
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
void *konzument(void *idp) {
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
            
            if ( ! fronty[cislo_producenta].empty()) {
                prvek = fronty[cislo_producenta].front();
                velikos_fronty = fronty[cislo_producenta].size();
                
                prectene_poradove_cislo = prvek->poradove_cislo;
                prectene_cislo_producenta = prvek->cislo_producenta;
                
                if (prectene_poradove_cislo != poradova_cisla_poslednich_prectenych_prveku[cislo_producenta]) {
                    nove_nacteny = true;
                    pocet_precteni = ++(prvek->pocet_precteni);
                    
                    if (prvek->pocet_precteni == N_KONZUMENTU) {
                        fronty[cislo_producenta].pop();
                        delete prvek;
                        
                        pthread_cond_signal(&condy_front[cislo_producenta]);
                        
                        prvek_odstranen = true;
                        nova_velikos_fronty = fronty[cislo_producenta].size();
                        if (nova_velikos_fronty == (K_POLOZEK - 1)) {
                            pthread_cond_signal(&condy_front[cislo_producenta]);
                            printf("konzument %i odblokoval frontu %i - velikost fronty %i\n",
                                cislo_konzumenta,
                                cislo_producenta,
                                velikos_fronty);
                        }
                    }
                    
                    poradova_cisla_poslednich_prectenych_prveku[cislo_producenta] = prectene_poradove_cislo;
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
                ukonci_cteni = poradova_cisla_poslednich_prectenych_prveku[cislo_producenta] == (LIMIT_PRVKU - 1);
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

int main(int argc, char * const argv[]) {
    pid_t root_pid = getpid();
    pid_t child_pid;
    char typ;
    int cislo;
    
    printf ("the main program process id is %d\n", (int) getpid ());
    
    for (int i = 0; i < M_PRODUCENTU; i++) {
        typ = 'P';
        cislo = i;
        
        child_pid = fork ();
        if (child_pid != 0) {
            if (root_pid == 0) {
                root_pid = getpid();
            }
            printf ("this is the parent process, with id %d\n", (int) getpid ());
            printf ("the child's process id is %d\n", (int) child_pid);
            sleep(1);
        }
        else {
            printf ("this is the child process, with id %d, ROOT: %d, typ: %c, cislo: %i\n", (int) getpid (), (int) root_pid, typ, cislo);
            sleep(20);
            break;
        }
    }
    
    if (root_pid == getpid()) {
        for (int i = 0; i < N_KONZUMENTU; i++) {
            typ = 'K';
            cislo = i;

            child_pid = fork ();
            if (child_pid != 0) {
                if (root_pid == 0) {
                    root_pid = getpid();
                }
                printf ("this is the parent process, with id %d\n", (int) getpid ());
                printf ("the child's process id is %d\n", (int) child_pid);
                sleep(1);
            }
            else {
                printf ("this is the child process, with id %d, ROOT: %d, typ: %c, cislo: %i\n", (int) getpid (), (int) root_pid, typ, cislo);
                sleep(20);
                break;
            }
        }
        if (root_pid == getpid()) {
            sleep(20);
        }
    }
    
    return 0;
}