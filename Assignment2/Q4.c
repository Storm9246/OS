#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <stdbool.h>

#define NUM_RWY 3
#define WAIT_LIM 5
#define Q_MAX 50

typedef enum { EMRG, LOW_FUEL, LAND, TKOFF, CARGO } f_type_t;
typedef struct {
    int id;
    f_type_t type;
    int prio; 
    time_t arr_t;
} flight_t;

static flight_t q[Q_MAX];
static int q_size = 0;
static bool rw_busy[NUM_RWY] = {false};
static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv = PTHREAD_COND_INITIALIZER;

static void push_f(flight_t f) {
    pthread_mutex_lock(&mtx);
    if (q_size < Q_MAX) {
        q[q_size++] = f;
        
        for (int i = 0; i < q_size - 1; i++) {
            for (int j = 0; j < q_size - i - 1; j++) {
                if (q[j].prio > q[j+1].prio) {
                    flight_t tmp = q[j];
                    q[j] = q[j+1];
                    q[j+1] = tmp;
                }
            }
        }
        pthread_cond_signal(&cv);
    }
    pthread_mutex_unlock(&mtx);
}

static flight_t pop_f() {
    flight_t f = q[0];
    for (int i = 0; i < q_size - 1; i++) {
        q[i] = q[i+1];
    }
    q_size--;
    return f;
}

void* do_gen(void* arg) {
    int next_id = 100;
    while (1) {
        flight_t f;
        f.id = next_id++;
        f.type = rand() % 5;
        f.prio = (int)f.type;
        f.arr_t = time(NULL);

        printf("[Gen] Flt %d (T:%d) arr\n", f.id, f.type);
        push_f(f);
        
        sleep((rand() % 3) + 1);
    }
    return NULL;
}

void* do_runway(void* arg) {
    int id = *((int*)arg);
    while (1) {
        pthread_mutex_lock(&mtx);
        while (q_size == 0) {
            pthread_cond_wait(&cv, &mtx);
        }

        if ((rand() % 10) == 0) {
            printf("[RW %d] Maint break!\n", id);
            pthread_mutex_unlock(&mtx);
            sleep(5);
            continue;
        }

        flight_t f = pop_f();
        rw_busy[id] = true;
        pthread_mutex_unlock(&mtx);

        printf("[RW %d] Proc Flt %d (P:%d)\n", id, f.id, f.prio);
        sleep(3); 

        printf("[RW %d] Flt %d done\n", id, f.id);
        rw_busy[id] = false;
    }
    return NULL;
}

void* do_mon(void* arg) {
    while (1) {
        sleep(2);
        pthread_mutex_lock(&mtx);
        time_t now = time(NULL);
        
        for (int i = 0; i < q_size; i++) {
            if (difftime(now, q[i].arr_t) > WAIT_LIM) {
                if (q[i].prio > 0) {
                    q[i].prio--;
                    printf("[Mon] Boost Flt %d prio\n", q[i].id);
                }
            }
            if (q[i].type == EMRG) {
                printf("[Mon] EMRG Flt %d!\n", q[i].id);
            }
        }
        pthread_mutex_unlock(&mtx);
    }
    return NULL;
}

int main() {
    srand(time(NULL));
    pthread_t t_gen, t_mon, t_rw[NUM_RWY];
    int rw_ids[NUM_RWY];

    pthread_create(&t_gen, NULL, do_gen, NULL);
    pthread_create(&t_mon, NULL, do_mon, NULL);

    for (int i = 0; i < NUM_RWY; i++) {
        rw_ids[i] = i;
        pthread_create(&t_rw[i], NULL, do_runway, &rw_ids[i]);
    }

    pthread_join(t_gen, NULL);
    
    return 0;
}