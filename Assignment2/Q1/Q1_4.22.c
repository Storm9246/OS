#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

int *numbers;
int size;
int avg = 0, min, max;

void *average(void *arg) {
    int sum = 0;
    for (int i = 0; i < size; i++){
        sum += numbers[i];
    }
    avg = sum / size;
    pthread_exit(0);
}

void *minimum(void *arg) {
    min = numbers[0];
    for (int i = 1; i < size; i++){
        if (numbers[i] < min){
            min = numbers[i];
        }
    }
    pthread_exit(0);
}

void *maximum(void *arg) {
    max = numbers[0];
    for (int i = 1; i < size; i++){
        if (numbers[i] > max){
            max = numbers[i];
        }
    }
    pthread_exit(0);
}

int main(int argc, char *argv[]) {

    if (argc < 2){
        printf("Usage: %s numbers...\n", argv[0]);
        return 1;
    }
    size = argc - 1;
    numbers = (int *)malloc(size * sizeof(int));
    for (int i = 0; i < size; i++){
        numbers[i] = atoi(argv[i + 1]);
    }
    pthread_t t1, t2, t3;

    pthread_create(&t1, NULL, average, NULL);
    pthread_create(&t2, NULL, minimum, NULL);
    pthread_create(&t3, NULL, maximum, NULL);

    pthread_join(t1, NULL);
    pthread_join(t2, NULL);
    pthread_join(t3, NULL);
    
    printf("Average = %d\n", avg);
    printf("Minimum = %d\n", min);
    printf("Maximum = %d\n", max);

    free(numbers);
    return 0;
}