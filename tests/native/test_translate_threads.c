// [XCDE] T282 review: the Maxwell translator must be re-entrant. Since T281 the precompile translates programs on
// several worker threads at once (exec.c xlat_lock skips its global lock for tl_inline_jobs), so the scratch rings
// (RSC2/img_coord), the lazy getenv caches and the guest-heap range must not be shared between threads. This test
// holds the workers at a start gate so their first translations run concurrently, then generates the serial
// reference only after every worker has joined. Repeated rounds exercise scratch-ring reuse.
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
// [XCDE] T275: runtime/log.c is not linked into this focused test.
void rt_logf(const char *fmt, ...) { (void)fmt; }

#include "../../runtime/nvn/maxwell/shader.h"

ShaderInfo *maxwell_translate(const uint8_t *blob, size_t max_size, int stage, const ShaderKey *key);
void maxwell_free(ShaderInfo *s);

static int fails;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); fails++; } } while (0)

#define EXIT_W 0xe30000000007000full
#define NOP_W 0x50b0000000070f00ull

// The same field encoders as tests/native/test_translate_tex.c (no game shaders).
static uint64_t tld4s(int d0, int x, int y, int d1, int tex, int comp) {
    return 0xdf00000000000000ull | ((uint64_t)tex << 36) | ((uint64_t)d1 << 28) | ((uint64_t)y << 20) | (7ull << 16) |
           ((uint64_t)x << 8) | (uint64_t)d0 | ((uint64_t)comp << 52);
}
static uint64_t suld32(int d, int x, int tex) {  // SULD, bound, 2D, 32-bit data, field at 8
    return 0xeb00000000000000ull | (8ull << 48) | (2ull << 52) | ((uint64_t)tex << 36) | (3ull << 33) | (8ull << 20) |
           (7ull << 16) | ((uint64_t)x << 8) | (uint64_t)d;
}

// which 0: a texture fetch through the integer-coordinate scratch ring (RSC2); 1: a surface load through img_coord().
static ShaderInfo *translate_one(int which, const ShaderKey *key) {
    uint8_t blob[0x80 + 8 * 16];
    memset(blob, 0, sizeof blob);
    uint64_t *code = (uint64_t *)(blob + 0x80);
    code[0] = 0;
    code[1] = which ? suld32(0, 8, 2) : tld4s(0, 8, 9, 4, 3, 0);
    code[2] = EXIT_W;
    code[3] = NOP_W;
    return maxwell_translate(blob, sizeof blob, STAGE_FRAGMENT, key);
}

enum { NTHREADS = 8, ROUNDS = 3, ITERS = 48, NPROGRAMS = 2 };
typedef struct Round Round;
typedef struct {
    pthread_t thread;
    bool started;
    int index;
    Round *round;
    char *first_src[NPROGRAMS];
} Worker;
struct Round {
    ShaderKey key;
    int index;
    _Atomic int ready, start;
};
static _Atomic int thread_fails;

static void *worker(void *arg) {
    Worker *w = arg;
    Round *r = w->round;
    atomic_fetch_add_explicit(&r->ready, 1, memory_order_release);
    while (!atomic_load_explicit(&r->start, memory_order_acquire)) sched_yield();

    ShaderKey key = r->key;
    for (int it = 0; it < ITERS; it++) {
        for (int which = 0; which < NPROGRAMS; which++) {
            ShaderInfo *s = translate_one(which, &key);
            if (!s || !s->msl) {
                atomic_fetch_add(&thread_fails, 1);
                if (it == 0)
                    printf("FAIL round %d worker %d program %d: %s\n", r->index, w->index, which,
                           s ? s->error : "no ShaderInfo");
            } else if (it == 0) {
                w->first_src[which] = strdup(s->msl);
                if (!w->first_src[which]) {
                    printf("FAIL round %d worker %d program %d: cannot save first output\n",
                           r->index, w->index, which);
                    atomic_fetch_add(&thread_fails, 1);
                }
            }
            if (s) maxwell_free(s);
        }
    }
    return NULL;
}

int main(void) {
    Worker workers[ROUNDS][NTHREADS] = {0};
    Round rounds[ROUNDS] = {0};
    ShaderKey key;
    memset(&key, 0, sizeof key);
    key.glsl = 1;

    for (int round = 0; round < ROUNDS; round++) {
        Round *r = &rounds[round];
        r->key = key;
        r->index = round;
        atomic_init(&r->ready, 0);
        atomic_init(&r->start, 0);
        atomic_store(&thread_fails, 0);

        int created = 0;
        for (int i = 0; i < NTHREADS; i++) {
            Worker *w = &workers[round][i];
            w->index = i;
            w->round = r;
            int err = pthread_create(&w->thread, NULL, worker, w);
            if (err != 0) {
                printf("FAIL %s:%d: round %d pthread_create: %s\n", __FILE__, __LINE__, round, strerror(err));
                fails++;
                continue;
            }
            w->started = true;
            created++;
        }
        if (!created) {
            printf("FAIL %s:%d: no worker thread started in round %d\n", __FILE__, __LINE__, round);
            fails++;
            goto done;
        }

        // No worker translates until every successfully created worker is ready at the gate.
        while (atomic_load_explicit(&r->ready, memory_order_acquire) < created) sched_yield();
        atomic_store_explicit(&r->start, 1, memory_order_release);
        for (int i = 0; i < NTHREADS; i++) {
            Worker *w = &workers[round][i];
            if (!w->started) continue;
            int err = pthread_join(w->thread, NULL);
            if (err != 0) {
                printf("FAIL %s:%d: round %d pthread_join: %s\n", __FILE__, __LINE__, round, strerror(err));
                return 1;  // a worker may still be using its saved output
            }
        }
        CHECK(atomic_load(&thread_fails) == 0);
    }

    // The main thread must not initialize the translator's lazy caches before the workers have finished.
    for (int which = 0; which < NPROGRAMS; which++) {
        ShaderInfo *s = translate_one(which, &key);
        CHECK(s && s->msl);
        if (!s || !s->msl) {
            if (s) printf("  serial error: %s\n", s->error);
            if (s) maxwell_free(s);
            goto done;
        }
        for (int round = 0; round < ROUNDS; round++) {
            for (int i = 0; i < NTHREADS; i++) {
                Worker *w = &workers[round][i];
                if (!w->started) continue;
                if (!w->first_src[which] || strcmp(w->first_src[which], s->msl) != 0) {
                    printf("FAIL round %d worker %d program %d: first output differs from serial translation\n",
                           round, i, which);
                    fails++;
                }
            }
        }
        maxwell_free(s);
    }

done:
    for (int round = 0; round < ROUNDS; round++)
        for (int i = 0; i < NTHREADS; i++)
            for (int which = 0; which < NPROGRAMS; which++)
                free(workers[round][i].first_src[which]);
    if (fails) { printf("%d check(s) failed\n", fails); return 1; }
    puts("translate threads: concurrent first translations match the post-join serial output");
    return 0;
}
