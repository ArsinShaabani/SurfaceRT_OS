/* SurfaceRT_OS - kernel threads */
#ifndef SRTOS_SCHED_H
#define SRTOS_SCHED_H

#include <srtos/types.h>
#include <srtos/list.h>

#define THREAD_NAME_LEN 24
#define THREAD_MAX 32

enum thread_state {
    THREAD_UNUSED = 0,
    THREAD_READY,
    THREAD_RUNNING,
    THREAD_SLEEPING,
    THREAD_BLOCKED,
    THREAD_DEAD,
};

struct thread {
    u32 *sp;
    u32 *stack_base;
    u32 stack_size;
    u32 pid;
    u32 wake_at_ms;
    u32 cpu_ticks;
    u8 state;
    u8 prio;
    char name[THREAD_NAME_LEN];
    struct list_head run_node;
};

void sched_init(void);
struct thread *thread_create(const char *name, void (*entry)(void *), void *arg,
                             u32 stack_bytes);
void sched_start(void) __noreturn;
void schedule(void);
void yield(void);
void msleep(u32 ms);
void thread_exit(void) __noreturn;
void thread_exit_trampoline(void) __noreturn;
struct thread *thread_current(void);
struct thread *thread_find_pid(u32 pid);
void sched_tick(void);
void sched_dump(void);
int sched_thread_count(void);

#endif
