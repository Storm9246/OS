#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

#define MAX 100

int critical_q[MAX], serious_q[MAX], normal_q[MAX];
int c_front=0,c_rear=0,s_front=0,s_rear=0,n_front=0,n_rear=0;

int serious_waiting = 0;

pthread_mutex_t lock;
pthread_cond_t cond_doctor;

typedef struct {
    int id;
    int type; // 1=senior, 2=junior
    int normal_count;
} doctor;

void enqueue(int *q, int *rear, int val) {
    q[(*rear)++] = val;
}

int dequeue(int *q, int *front, int rear) {
    if (*front == rear) return -1;
    return q[(*front)++];
}

void promote_serious() {
    if (serious_waiting >= 5) {
        int p = dequeue(serious_q, &s_front, s_rear);
        if (p != -1) {
            enqueue(critical_q, &c_rear, p);
            serious_waiting--;
        }
    }
}

void *patient(void *arg) {
    int type = *(int*)arg;

    pthread_mutex_lock(&lock);

    if (type == 1)
        enqueue(critical_q, &c_rear, rand()%100);
    else if (type == 2) {
        enqueue(serious_q, &s_rear, rand()%100);
        serious_waiting++;
    }
    else
        enqueue(normal_q, &n_rear, rand()%100);

    promote_serious();

    pthread_cond_signal(&cond_doctor);
    pthread_mutex_unlock(&lock);

    return NULL;
}

void *doctor_thread(void *arg) {
    doctor *doc = (doctor*)arg;

    while (1) {
        pthread_mutex_lock(&lock);

        while (c_front==c_rear && s_front==s_rear && n_front==n_rear)
            pthread_cond_wait(&cond_doctor, &lock);

        int patient = -1;

        if (c_front != c_rear && doc->type == 1) {
            patient = dequeue(critical_q, &c_front, c_rear);
            doc->normal_count = 0;
            printf("Senior doctor %d treated CRITICAL\n", doc->id);
        }
        else if (doc->normal_count >= 3 && s_front != s_rear) {
            patient = dequeue(serious_q, &s_front, s_rear);
            serious_waiting--;
            doc->normal_count = 0;
            printf("Doctor %d forced SERIOUS\n", doc->id);
        }
        else if (s_front != s_rear) {
            patient = dequeue(serious_q, &s_front, s_rear);
            serious_waiting--;
            doc->normal_count = 0;
            printf("Doctor %d treated SERIOUS\n", doc->id);
        }
        else if (n_front != n_rear) {
            patient = dequeue(normal_q, &n_front, n_rear);
            doc->normal_count++;
            printf("Doctor %d treated NORMAL\n", doc->id);
        }

        pthread_mutex_unlock(&lock);
        sleep(1);
    }
}

int main() {

    pthread_mutex_init(&lock, NULL);
    pthread_cond_init(&cond_doctor, NULL);

    pthread_t docs[3], patients[20];

    doctor d[3] = {
        {1,1,0},
        {2,1,0},
        {3,2,0}
    };

    for (int i=0;i<3;i++)
        pthread_create(&docs[i], NULL, doctor_thread, &d[i]);

    for (int i=0;i<20;i++) {
        int *type = malloc(sizeof(int));
        *type = rand()%3 + 1;
        pthread_create(&patients[i], NULL, patient, type);
        sleep(1);
    }

    for (int i=0;i<20;i++)
        pthread_join(patients[i], NULL);

    return 0;
}