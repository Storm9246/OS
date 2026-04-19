#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <float.h>
#include <pthread.h>
#include <time.h>

#define N 1000
#define M 4
#define TILE_SIZE 100
#define NUM_TILES ((N / TILE_SIZE) * (N / TILE_SIZE))
#define NUM_WORKERS 8
#define T_HOT 35.0
#define T_COLD -10.0
#define NAN_VAL (1e38)

static double grid[N][N]; 
static int hits[N][N];
static double norm_grid[N][N];
static double risk_grid[N][N];
static double adj_grid[N][N];
static int is_hot[N][N];
static int is_cold[N][N];

static double g_max = -DBL_MAX, g_min = DBL_MAX;
static double g_sum = 0.0, g_sum_sq = 0.0;
static long g_count = 0, g_anom = 0;
static long g_hot_cnt = 0, g_cold_cnt = 0;

static pthread_mutex_t mtx_stats = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t mtx_hotcold = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t mtx_q = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t mtx_sync = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv_sync = PTHREAD_COND_INITIALIZER;

static int jobs[NUM_TILES];
static int job_ptr = 0;

static int f_hot = 0, f_cold = 0, f_norm = 0;

static inline int is_missing(double v) { return v >= NAN_VAL * 0.9; }
static inline int boundary(int x) { return x < 0 ? 0 : (x >= N ? N - 1 : x); }

/* Phase 1: Satellites */
typedef struct {
    int id;
} sat_ctx_t;

static void patch_holes(double m[N][N]) {
    int active = 1;
    while (active) {
        active = 0;
        for (int r = 0; r < N; r++) {
            for (int c = 0; c < N; c++) {
                if (!is_missing(m[r][c])) continue;
                
                double s = 0.0;
                int n = 0;
                int dr[] = {-1, 1, 0, 0}, dc[] = {0, 0, -1, 1};
                
                for (int i = 0; i < 4; i++) {
                    int rr = r + dr[i], cc = c + dc[i];
                    if (rr >= 0 && rr < N && cc >= 0 && cc < N && !is_missing(m[rr][cc])) {
                        s += m[rr][cc];
                        n++;
                    }
                }
                if (n > 0) {
                    m[r][c] = s / n;
                    active = 1;
                }
            }
        }
    }
}

static void* do_satellite(void* arg) {
    sat_ctx_t* ctx = (sat_ctx_t*)arg;
    unsigned int seed = (unsigned int)(ctx->id * 42 + time(NULL));
    
    double (*buf)[N] = malloc(sizeof(double) * N * N);
    if (!buf) return NULL;

    for (int r = 0; r < N; r++) {
        for (int c = 0; c < N; c++) {
            if (((double)rand_r(&seed) / RAND_MAX) < 0.05) {
                buf[r][c] = NAN_VAL;
            } else {
                buf[r][c] = -30.0 + ((double)rand_r(&seed) / RAND_MAX) * 75.0;
            }
        }
    }

    patch_holes(buf);

    pthread_mutex_lock(&mtx_stats);
    for (int r = 0; r < N; r++) {
        for (int c = 0; c < N; c++) {
            grid[r][c] += buf[r][c];
            hits[r][c]++;
        }
    }
    pthread_mutex_unlock(&mtx_stats);

    free(buf);
    return NULL;
}

/* Phase 2: Regions */
static void* do_work(void* arg) {
    while (1) {
        int idx;
        pthread_mutex_lock(&mtx_q);
        if (job_ptr >= NUM_TILES) {
            pthread_mutex_unlock(&mtx_q);
            break;
        }
        idx = jobs[job_ptr++];
        pthread_mutex_unlock(&mtx_q);

        int tpr = N / TILE_SIZE;
        int r0 = (idx / tpr) * TILE_SIZE, r1 = r0 + TILE_SIZE;
        int c0 = (idx % tpr) * TILE_SIZE, c1 = c0 + TILE_SIZE;

        double l_max = -DBL_MAX, l_min = DBL_MAX, l_sum = 0, l_sq = 0;
        
        for (int r = r0; r < r1; r++) {
            for (int c = c0; c < c1; c++) {
                double v = grid[r][c];
                if (v > l_max) l_max = v;
                if (v < l_min) l_min = v;
                l_sum += v;
                l_sq += v * v;
            }
        }

        double avg = l_sum / (TILE_SIZE * TILE_SIZE);
        double std = sqrt(fmax(0, (l_sq / (TILE_SIZE * TILE_SIZE)) - (avg * avg)));

        long l_anom = 0;
        for (int r = r0; r < r1; r++)
            for (int c = c0; c < c1; c++)
                if (fabs(grid[r][c] - avg) > 2.0 * std) l_anom++;

        pthread_mutex_lock(&mtx_stats);
        if (l_max > g_max) g_max = l_max;
        if (l_min < g_min) g_min = l_min;
        g_sum += l_sum;
        g_sum_sq += l_sq;
        g_count += (TILE_SIZE * TILE_SIZE);
        g_anom += l_anom;
        pthread_mutex_unlock(&mtx_stats);
    }
    return NULL;
}

/* Phase 3: Tasks */
static void* task_h(void* a) {
    long count = 0;
    for (int r = 0; r < N; r++) {
        for (int c = 0; c < N; c++) {
            if (grid[r][c] > T_HOT) { is_hot[r][c] = 1; count++; }
        }
    }
    pthread_mutex_lock(&mtx_sync);
    g_hot_cnt = count; f_hot = 1;
    pthread_cond_broadcast(&cv_sync);
    pthread_mutex_unlock(&mtx_sync);
    return NULL;
}

static void* task_c(void* a) {
    long count = 0;
    for (int r = 0; r < N; r++) {
        for (int c = 0; c < N; c++) {
            if (grid[r][c] < T_COLD) { is_cold[r][c] = 1; count++; }
        }
    }
    pthread_mutex_lock(&mtx_sync);
    g_cold_cnt = count; f_cold = 1;
    pthread_cond_broadcast(&cv_sync);
    pthread_mutex_unlock(&mtx_sync);
    return NULL;
}

static void* task_n(void* a) {
    double span = (g_max - g_min == 0) ? 1.0 : (g_max - g_min);
    for (int r = 0; r < N; r++)
        for (int c = 0; c < N; c++)
            norm_grid[r][c] = (grid[r][c] - g_min) / span;

    pthread_mutex_lock(&mtx_sync);
    while (!f_hot || !f_cold) pthread_cond_wait(&cv_sync, &mtx_sync);
    pthread_mutex_unlock(&mtx_sync);

    for (int r = 0; r < N; r++) {
        for (int c = 0; c < N; c++) {
            adj_grid[r][c] = norm_grid[r][c];
            int found_h = 0;
            for(int i=-2; i<=2 && !found_h; i++) 
                for(int j=-2; j<=2 && !found_h; j++)
                    if (boundary(r+i)==(r+i) && boundary(c+j)==(c+j) && is_hot[r+i][c+j]) found_h=1;
            
            if (found_h) adj_grid[r][c] = 0.6 * norm_grid[r][c] + 0.2;
        }
    }

    pthread_mutex_lock(&mtx_sync);
    f_norm = 1;
    pthread_cond_broadcast(&cv_sync);
    pthread_mutex_unlock(&mtx_sync);
    return NULL;
}

static void* task_r(void* a) {
    pthread_mutex_lock(&mtx_sync);
    while (!f_norm || !f_hot || !f_cold) pthread_cond_wait(&cv_sync, &mtx_sync);
    pthread_mutex_unlock(&mtx_sync);

    for (int r = 0; r < N; r++) {
        for (int c = 0; c < N; c++) {
            int nh = 0, nc = 0;
            for (int i = -5; i <= 5; i++) {
                for (int j = -5; j <= 5; j++) {
                    if (abs(i) + abs(j) > 5) continue;
                    int rr = boundary(r+i), cc = boundary(c+j);
                    if (is_hot[rr][cc]) nh++;
                    if (is_cold[rr][cc]) nc++;
                }
            }
            risk_grid[r][c] = adj_grid[r][c] * (double)nh / (nc + 1.0);
        }
    }
    return NULL;
}

int main() {
    srand(time(NULL));
    printf("Init: N=%d, Satellites=%d, Workers=%d\n", N, M, NUM_WORKERS);

    pthread_t sats[M];
    sat_ctx_t args[M];
    for (int i = 0; i < M; i++) {
        args[i].id = i;
        pthread_create(&sats[i], NULL, do_satellite, &args[i]);
    }
    for (int i = 0; i < M; i++) pthread_join(sats[i], NULL);

    for (int r = 0; r < N; r++)
        for (int c = 0; c < N; c++)
            if (hits[r][c] > 1) grid[r][c] /= hits[r][c];

    for (int i = 0; i < NUM_TILES; i++) jobs[i] = i;
    for (int i = NUM_TILES - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        int t = jobs[i]; jobs[i] = jobs[j]; jobs[j] = t;
    }

    pthread_t workers[NUM_WORKERS];
    for (int i = 0; i < NUM_WORKERS; i++) pthread_create(&workers[i], NULL, do_work, NULL);
    for (int i = 0; i < NUM_WORKERS; i++) pthread_join(workers[i], NULL);

    double m = g_sum / g_count;
    printf("\n--- Global Report ---\n");
    printf("Temp Range: [%.2f, %.2f] | Mean: %.2f\n", g_min, g_max, m);
    printf("Anomalies: %ld\n", g_anom);

    pthread_t t1, t2, t3, t4;
    pthread_create(&t1, NULL, task_h, NULL);
    pthread_create(&t2, NULL, task_c, NULL);
    pthread_create(&t3, NULL, task_n, NULL);
    pthread_create(&t4, NULL, task_r, NULL);

    pthread_join(t1, NULL); pthread_join(t2, NULL);
    pthread_join(t3, NULL); pthread_join(t4, NULL);

    printf("Analysis done. Hotspots: %ld, Coldspots: %ld\n", g_hot_cnt, g_cold_cnt);

    return 0;
}