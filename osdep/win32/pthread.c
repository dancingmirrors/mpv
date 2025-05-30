/* Copyright (C) 2017 the mpv developers
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <pthread.h>
#include <semaphore.h>

#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <sys/time.h>
#include <assert.h>
#include <windows.h>

#include "common/common.h"
#include "osdep/timer.h"  // mp_{start,end}_hires_timers

int pthread_once(pthread_once_t *once_control, void (*init_routine)(void))
{
    BOOL pending;
    if (!InitOnceBeginInitialize(once_control, 0, &pending, NULL))
        abort();
    if (pending) {
        init_routine();
        InitOnceComplete(once_control, 0, NULL);
    }
    return 0;
}

int pthread_mutex_destroy(pthread_mutex_t *mutex)
{
    if (mutex->use_cs)
        DeleteCriticalSection(&mutex->lock.cs);
    return 0;
}

int pthread_mutex_init(pthread_mutex_t *restrict mutex,
                       const pthread_mutexattr_t *restrict attr)
{
    mutex->use_cs = attr && (*attr & PTHREAD_MUTEX_RECURSIVE);
    if (mutex->use_cs) {
        InitializeCriticalSection(&mutex->lock.cs);
    } else {
        InitializeSRWLock(&mutex->lock.srw);
    }
    return 0;
}

int pthread_mutex_lock(pthread_mutex_t *mutex)
{
    if (mutex->use_cs) {
        EnterCriticalSection(&mutex->lock.cs);
    } else {
        AcquireSRWLockExclusive(&mutex->lock.srw);
    }
    return 0;
}

int pthread_mutex_unlock(pthread_mutex_t *mutex)
{
    if (mutex->use_cs) {
        LeaveCriticalSection(&mutex->lock.cs);
    } else {
        ReleaseSRWLockExclusive(&mutex->lock.srw);
    }
    return 0;
}

static int cond_wait(pthread_cond_t *restrict cond,
                     pthread_mutex_t *restrict mutex,
                     DWORD ms)
{
    BOOL res;
    int hrt = mp_start_hires_timers(ms);
    if (mutex->use_cs) {
        res = SleepConditionVariableCS(cond, &mutex->lock.cs, ms);
    } else {
        res = SleepConditionVariableSRW(cond, &mutex->lock.srw, ms, 0);
    }
    mp_end_hires_timers(hrt);
    return res ? 0 : ETIMEDOUT;
}

int pthread_cond_timedwait(pthread_cond_t *restrict cond,
                           pthread_mutex_t *restrict mutex,
                           const struct timespec *restrict abstime)
{
    // mp time is not converted to realtime if internal pthread impl is used
    int64_t now = mp_time_ns();
    int64_t time_ns = abstime->tv_sec * UINT64_C(1000000000) + abstime->tv_nsec;
    int64_t timeout_ms = (time_ns - now) / INT64_C(1000000);
    return cond_wait(cond, mutex, MPCLAMP(timeout_ms, 0, INFINITE));
}

int pthread_cond_wait(pthread_cond_t *restrict cond,
                      pthread_mutex_t *restrict mutex)
{
    return cond_wait(cond, mutex, INFINITE);
}

static pthread_mutex_t pthread_table_lock = PTHREAD_MUTEX_INITIALIZER;
static struct m_thread_info *pthread_table;
size_t pthread_table_num;

struct m_thread_info {
    DWORD id;
    HANDLE handle;
    void *(*user_fn)(void *);
    void *user_arg;
    void *res;
};

static struct m_thread_info *find_thread_info(DWORD id)
{
    for (int n = 0; n < pthread_table_num; n++) {
        if (id == pthread_table[n].id)
            return &pthread_table[n];
    }
    return NULL;
}

static void remove_thread_info(struct m_thread_info *info)
{
    assert(pthread_table_num);
    assert(info >= &pthread_table[0] && info < &pthread_table[pthread_table_num]);

    pthread_table[info - pthread_table] = pthread_table[pthread_table_num - 1];
    pthread_table_num -= 1;

    // Avoid upsetting leak detectors.
    if (pthread_table_num == 0) {
        free(pthread_table);
        pthread_table = NULL;
    }
}

void pthread_exit(void *retval)
{
    pthread_mutex_lock(&pthread_table_lock);
    struct m_thread_info *info = find_thread_info(pthread_self());
    assert(info); // not started with pthread_create, or pthread_join() race
    info->res = retval;
    if (!info->handle)
        remove_thread_info(info); // detached case
    pthread_mutex_unlock(&pthread_table_lock);

    ExitThread(0);
}

int pthread_join(pthread_t thread, void **retval)
{
    pthread_mutex_lock(&pthread_table_lock);
    struct m_thread_info *info = find_thread_info(thread);
    assert(info); // not started with pthread_create, or pthread_join() race
    HANDLE h = info->handle;
    assert(h); // thread was detached
    pthread_mutex_unlock(&pthread_table_lock);

    WaitForSingleObject(h, INFINITE);

    pthread_mutex_lock(&pthread_table_lock);
    info = find_thread_info(thread);
    assert(info);
    assert(info->handle == h);
    CloseHandle(h);
    if (retval)
        *retval = info->res;
    remove_thread_info(info);
    pthread_mutex_unlock(&pthread_table_lock);

    return 0;
}

int pthread_detach(pthread_t thread)
{
    if (!pthread_equal(thread, pthread_self()))
        abort(); // restriction of this wrapper

    pthread_mutex_lock(&pthread_table_lock);
    struct m_thread_info *info = find_thread_info(thread);
    assert(info); // not started with pthread_create
    assert(info->handle); // already detached
    CloseHandle(info->handle);
    info->handle = NULL;
    pthread_mutex_unlock(&pthread_table_lock);

    return 0;
}

static DWORD WINAPI run_thread(LPVOID lpParameter)
{
    pthread_mutex_lock(&pthread_table_lock);
    struct m_thread_info *pinfo = find_thread_info(pthread_self());
    assert(pinfo);
    struct m_thread_info info = *pinfo;
    pthread_mutex_unlock(&pthread_table_lock);

    pthread_exit(info.user_fn(info.user_arg));
    abort(); // not reached
}

int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                   void *(*start_routine) (void *), void *arg)
{
    int res = 0;
    pthread_mutex_lock(&pthread_table_lock);
    void *nalloc =
        realloc(pthread_table, (pthread_table_num + 1) * sizeof(pthread_table[0]));
    if (!nalloc) {
        res = EAGAIN;
        goto done;
    }
    pthread_table = nalloc;
    pthread_table_num += 1;
    struct m_thread_info *info = &pthread_table[pthread_table_num - 1];
    *info = (struct m_thread_info) {
        .user_fn = start_routine,
        .user_arg = arg,
    };
    info->handle = CreateThread(NULL, 0, run_thread, NULL, CREATE_SUSPENDED,
                                &info->id);
    if (!info->handle) {
        remove_thread_info(info);
        res = EAGAIN;
        goto done;
    }
    *thread = info->id;
    ResumeThread(info->handle);
done:
    pthread_mutex_unlock(&pthread_table_lock);
    return res;
}

void pthread_set_name_np(pthread_t thread, const char *name)
{
#if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP) && defined(_PROCESSTHREADSAPI_H_)
    HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    if (!kernel32)
        return;
    HRESULT (WINAPI *pSetThreadDescription)(HANDLE, PCWSTR) =
        (void*)GetProcAddress(kernel32, "SetThreadDescription");
    if (!pSetThreadDescription)
        return;

    HANDLE th = OpenThread(THREAD_SET_LIMITED_INFORMATION, FALSE, thread);
    if (!th)
        return;
    wchar_t wname[80];
    int wc = MultiByteToWideChar(CP_UTF8, 0, name, -1, wname,
                                 sizeof(wname) / sizeof(wchar_t) - 1);
    if (wc > 0) {
        wname[wc] = L'\0';
        pSetThreadDescription(th, wname);
    }
    CloseHandle(th);
#endif
}

int sem_init(sem_t *sem, int pshared, unsigned int value)
{
    if (pshared)
        abort(); // unsupported
    pthread_mutex_init(&sem->lock, NULL);
    pthread_cond_init(&sem->wakeup, NULL);
    sem->value = value;
    return 0;
}

int sem_destroy(sem_t *sem)
{
    pthread_mutex_destroy(&sem->lock);
    pthread_cond_destroy(&sem->wakeup);
    return 0;
}

int sem_wait(sem_t *sem)
{
    pthread_mutex_lock(&sem->lock);
    while (!sem->value)
        pthread_cond_wait(&sem->wakeup, &sem->lock);
    sem->value -= 1;
    pthread_mutex_unlock(&sem->lock);
    return 0;
}

int sem_trywait(sem_t *sem)
{
    pthread_mutex_lock(&sem->lock);
    int r;
    if (sem->value > 0) {
        sem->value -= 1;
        r = 0;
    } else {
        errno = EAGAIN;
        r = -1;
    }
    pthread_mutex_unlock(&sem->lock);
    return r;
}

int sem_timedwait(sem_t *sem, const struct timespec *abs_timeout)
{
    pthread_mutex_lock(&sem->lock);
    while (!sem->value) {
        int err = pthread_cond_timedwait(&sem->wakeup, &sem->lock, abs_timeout);
        if (err) {
            pthread_mutex_unlock(&sem->lock);
            errno = err;
            return -1;
        }
    }
    sem->value -= 1;
    pthread_mutex_unlock(&sem->lock);
    return 0;
}

int sem_post(sem_t *sem)
{
    pthread_mutex_lock(&sem->lock);
    sem->value += 1;
    pthread_cond_broadcast(&sem->wakeup);
    pthread_mutex_unlock(&sem->lock);
    return 0;
}

// 0 is only allowed as the exact string "0" (not "000" etc)
static int env_atoi(const char *var, int low, int high, int def)
{
    const char *v = getenv(var);
    if (v && *v) {
        int i = atoi(v);
        if (i != 0 || (*v == '0' && !v[1])) {
            if (low <= i && i <= high)
                return i;
        }
    }
    return def;
}

#define CAL_DEF_MS 2    /* spin duration of one calibration measurement */
#define CAL_DEF_N 5     /* number of calibration measurements to perform */

uint64_t cns = 1;    // calibration ns
uint64_t cpcns = 1;  // cycles per cns (default: cycle==ns, i.e. measure cycles)

static void calibrate_cycle_duration_ns(void)
{
    int ms = env_atoi("MPV_CTC_MS", 0, 100, CAL_DEF_MS); // CTC: cpu time cal.
    int n = env_atoi("MPV_CTC_N", 0, 10, CAL_DEF_N);
    if (!ms || !n)
        return;

    uint64_t delta_cycles = 0;
    HANDLE h = GetCurrentThread();

    int pset = 0, p0 = GetThreadPriority(h);
    if (p0 != THREAD_PRIORITY_ERROR_RETURN && p0 < THREAD_PRIORITY_HIGHEST)
        pset = SetThreadPriority(h, THREAD_PRIORITY_HIGHEST); // failure allowed

    // assuming CycleTime ticks at a fixed rate (~Y2K+ Intel/AMD), the main
    // source of inaccuracy is counting too few cycles due to the thread going
    // idle (context switch, etc), so iterate few times and get the max value.
    for (int i = 0; i < n; i++) {
        ULONG64 c0, c1;
        if (!QueryThreadCycleTime(h, &c0))
            goto done;

        uint64_t deadline = mp_time_us() + 1000 * ms;
        while (mp_time_us() < deadline)
            /* spin */;

        if (!QueryThreadCycleTime(h, &c1))
            goto done;

        if (delta_cycles < c1 - c0)
            delta_cycles = c1 - c0;
    }

done:
    if (pset)
        SetThreadPriority(h, p0);

    if (delta_cycles) {
        cns = ms * 1e6;
        cpcns = delta_cycles;

        // assuming the type can hold any practical thread cpu-time in ns,
        // the biggest intermediate value at the win32_pthread_cpu_time_ns
        // calculation is (cpcns-1)*cns, so if it overflows - reduce
        // accuracy till it fits. this shouldn't happen in practice.
        // cpcns, cns won't be 0 (the prior step (1) breaks the loop).
        while ((cpcns - 1) > UINT64_MAX / cns) {
            cns /= 2;
            cpcns /= 2;
        }
    }
}

unsigned long long win32_pthread_cpu_time_ns(pthread_t thread)
{
    static pthread_once_t calibrate = PTHREAD_ONCE_INIT;
    pthread_once(&calibrate, calibrate_cycle_duration_ns);

    unsigned long long cycles = 0;

    HANDLE th = (thread == pthread_self())
        ? GetCurrentThread()
        : OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, thread);

    if (th) {
        ULONG64 n;
        if (QueryThreadCycleTime(th, &n))
            cycles = n;
        CloseHandle(th);
    }

    return cycles / cpcns * cns           // full units of cns
         + cycles % cpcns * cns / cpcns;  // fraction of cns
}
