#include "mutex.h"
#include "console.h"

#define MAX_REGISTERED_MUTEXES 32
static struct kmutex *mutex_registry[MAX_REGISTERED_MUTEXES];
static int mutex_registry_count = 0;

void kmutex_init(struct kmutex *m, const char *name)
{
    if (!m) return;
    m->locked = 0;
    m->owner = 0;
    m->depth = 0;
    m->name = name ? name : "mutex";

    /* Auto-registrar mutex en la tabla global si no existe */
    for (int i = 0; i < mutex_registry_count; ++i) {
        if (mutex_registry[i] == m) return;
    }
    if (mutex_registry_count < MAX_REGISTERED_MUTEXES) {
        mutex_registry[mutex_registry_count++] = m;
    }
}

void kmutex_lock(struct kmutex *m)
{
    if (!m) return;
    struct tcb *curr = thread_current();

    while (1) {
        if (!m->locked) {
            m->locked = 1;
            m->owner = curr;
            m->depth = 1;
            return;
        }
        if (m->owner == curr) {
            m->depth++;     /* Adquisicion recursiva por el mismo hilo */
            return;
        }
        thread_yield();
    }
}

int kmutex_trylock(struct kmutex *m)
{
    if (!m) return 0;
    struct tcb *curr = thread_current();

    if (!m->locked) {
        m->locked = 1;
        m->owner = curr;
        m->depth = 1;
        return 1;
    }
    if (m->owner == curr) {
        m->depth++;
        return 1;
    }
    return 0;
}

void kmutex_unlock(struct kmutex *m)
{
    if (!m) return;
    if (m->depth > 1) {
        m->depth--;         /* unlock interno: seguimos siendo propietarios */
        return;
    }
    m->depth = 0;
    m->locked = 0;
    m->owner = 0;
}

void kmutex_release_all_for_thread(struct tcb *t)
{
    if (!t) return;
    for (int i = 0; i < mutex_registry_count; ++i) {
        struct kmutex *m = mutex_registry[i];
        if (m && m->locked && m->owner == t) {
            kprint("\n[MUTEX RECOVERY] Mutex '");
            kprint(m->name);
            kprint("' liberado forzosamente tras muerte de TID ");
            kprint_dec(t->tid);
            kprint("\n");

            m->depth = 0;
            m->locked = 0;
            m->owner = 0;
        }
    }
}

/* Auto-test de sincronizacion */
static struct kmutex test_mtx;
static volatile int shared_counter = 0;
static volatile int mtx_worker1_done = 0;
static volatile int mtx_worker2_done = 0;

static void mtx_worker_func(void *arg)
{
    int id = (int)(uintptr_t)arg;
    for (int i = 0; i < 50; ++i) {
        kmutex_lock(&test_mtx);
        int val = shared_counter;
        thread_yield(); /* Forzar conmutacion en seccion critica */
        shared_counter = val + 1;
        kmutex_unlock(&test_mtx);
    }
    if (id == 1) mtx_worker1_done = 1;
    if (id == 2) mtx_worker2_done = 1;
}

int kmutex_test_self(void)
{
    kprint("\n[KMUTEX AUTO-TEST] Verificando exclusion mutua con dos hilos en seccion critica...\n");

    kmutex_init(&test_mtx, "test_counter_lock");
    shared_counter = 0;
    mtx_worker1_done = 0;
    mtx_worker2_done = 0;

    thread_create("mtx_w1", mtx_worker_func, (void *)1);
    thread_create("mtx_w2", mtx_worker_func, (void *)2);

    while (!mtx_worker1_done || !mtx_worker2_done) {
        thread_yield();
    }

    /* Limpiar los dos hilos muertos con el reaper */
    thread_reap_dead();

    if (shared_counter != 100) {
        kprint("  FALLO: Condicion de carrera detectada (contador=");
        kprint_dec(shared_counter);
        kprint(", esperado 100)\n");
        return 0;
    }

    kprint("  Seccion critica protegida OK (100 incrementos concurrentes sin conflicto)\n");

    /* Prueba 2: Recuperacion forzada de mutex huerfano tras thread_kill */
    kprint("  Probando recuperacion de mutex huerfano tras kill...\n");
    kmutex_lock(&test_mtx);

    /* Simular que el dueño era un hilo efimero con TID ficticio 99 */
    struct tcb fake_dead_thread;
    fake_dead_thread.tid = 99;
    test_mtx.owner = &fake_dead_thread;

    /* Invocar recuperacion forzada */
    kmutex_release_all_for_thread(&fake_dead_thread);

    if (test_mtx.locked != 0 || test_mtx.owner != 0) {
        kprint("  FALLO: Mutex no fue limpiado tras muerte del hilo\n");
        return 0;
    }

    /* Debe poder re-adquirirse limpiamente sin deadlock */
    kmutex_lock(&test_mtx);
    kmutex_unlock(&test_mtx);
    kprint("  Recuperacion de bloqueo huerfano verificada OK (sin deadlocks)\n");

    kprint("[KMUTEX AUTO-TEST] SUPERADO CON EXITO (Concurrencia y Kill Seguro OK).\n\n");
    return 1;
}
