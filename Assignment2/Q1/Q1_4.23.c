#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

int n;

int isPrime(int num){
    if (num < 2) return 0;
    for (int i = 2; i * i <= num; i++){
        if (num % i == 0){
            return 0;
        }
    }
    return 1;
}

void *printPrimes(void *arg){
    for (int i = 2; i <= n; i++){
        if (isPrime(i)){
            printf("%d ", i);
        }
    }
    pthread_exit(0);
}

int main(int argc, char *argv[]){

    if (argc != 2){
        printf("Usage: %s number\n", argv[0]);
        return 1;
    }

    n = atoi(argv[1]);

    pthread_t tid;

    pthread_create(&tid, NULL, printPrimes, NULL);
    pthread_join(tid, NULL);

    printf("\n");
    return 0;
}