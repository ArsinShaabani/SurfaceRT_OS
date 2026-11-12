/*
 * SurfaceRT_OS - preemptive round robin scheduler
 *
 * Threads are kernel threads: each one has its own stack allocated from the
 * physical allocator.  The tick handler (PPI 29) calls sched_tick(), which may
 * call schedule() directly from interrupt context - the interrupted thread's
 * IRQ frame simply stays on its stack, so switching inside the handler is safe.
 */
#include <srtos/sched.h>
#include <srtos/arch.h>
#include <srtos/irq.h>
#include <srtos/kernel.h>
#include <srtos/mm.h>
#include <srtos/clock.h>
#include <srtos/string.h>

#define QUANTUM_TICKS 10
#define DEFAULT_STACK 8192

static struct thread threads[THREAD_MAX];
static struct thread *current;
static struct thread *idle_thread;
static LIST_HEAD(runq);
static u32 next_pid = 1;
static volatile int sched_ready;
static spinlock_t sched_lock = SPINLOCK_INIT;

u32 *g_sched_sp_dummy;
void thread_exit_trampoline(void)
{
    thread_exit();
}

static void idle_entry(void *arg)
{
    (void)arg;
    for (;;) {
        wfi();
    }
}

static struct thread *alloc_thread(const char *name)
{
    int i;
    for (i = 0; i < THREAD_MAX; i++) {
        if (threads[i].state == THREAD_UNUSED) {
            memset(&threads[i], 0, sizeof(threads[i]));
            threads[i].pid = next_pid++;
            strlcpy(threads[i].name, name, THREAD_NAME_LEN);
            list_init(&threads[i].run_node);
            return &threads[i];
        }
    }
    return 0;
}

static void enqueue(struct thread *t)
{
    t->state = THREAD_READY;
    list_add_tail(&t->run_node, &runq);
}

struct thread *thread_create(const char *name, void (*entry)(void *), void *arg,
                             u32 stack_bytes)
{
    struct thread *t;
    u32 pages, *sp;
    void *stack;

    if (!stack_bytes) {
        stack_bytes = DEFAULT_STACK;
    }
    stack_bytes = ALIGN_UP(stack_bytes, PAGE_SIZE);
    pages = stack_bytes / PAGE_SIZE;

    t = alloc_thread(name);
    if (!t) {
        pr_err("sched: no free thread slot for %s\n", name);
        return 0;
    }
    stack = pmm_alloc_pages(pages);
    if (!stack) {
        t->state = THREAD_UNUSED;
        pr_err("sched: no memory for the %s stack\n", name);
        return 0;
    }
    memset(stack, 0, stack_bytes);
    t->stack_base = (u32 *)stack;
    t->stack_size = stack_bytes;

    /* Build the initial frame consumed by arm_switch_context + trampoline:
     * [r4..r11, lr=trampoline] then [r0=entry, r1=arg]. */
    sp = (u32 *)((uptr)stack + stack_bytes);
    sp = (u32 *)((uptr)sp & ~7u);
    sp -= 11;
    memset(sp, 0, 8 * sizeof(u32));
    sp[8] = (u32)(uptr)arm_thread_trampoline;
    sp[9] = (u32)(uptr)entry;
    sp[10] = (u32)(uptr)arg;
    t->sp = sp;

    enqueue(t);
    pr_debug("sched: created %s (pid %u, %u KiB stack)\n", t->name, t->pid,
             stack_bytes / 1024u);
    return t;
}

void sched_init(void)
{
    memset(threads, 0, sizeof(threads));
    list_init(&runq);
    current = 0;
    sched_ready = 1;

    idle_thread = alloc_thread("idle");
    if (idle_thread) {
        static u32 idle_stack[1024] __aligned(8);
        idle_thread->stack_base = idle_stack;
        idle_thread->stack_size = sizeof(idle_stack);
        idle_thread->state = THREAD_READY;
        idle_thread->prio = 31;
        {
            u32 *sp = (u32 *)((uptr)idle_stack + sizeof(idle_stack));
            sp = (u32 *)((uptr)sp & ~7u);
            sp -= 11;
            memset(sp, 0, 8 * sizeof(u32));
            sp[8] = (u32)(uptr)arm_thread_trampoline;
            sp[9] = (u32)(uptr)idle_entry;
            sp[10] = 0;
            idle_thread->sp = sp;
        }
        enqueue(idle_thread);
    }
    pr_info("sched: ready, %u thread slots, quantum %u ticks\n", THREAD_MAX,
            QUANTUM_TICKS);
}

void schedule(void)
{
    struct thread *prev, *next;
    u32 flags;

    if (!sched_ready) {
        return;
    }
    flags = spin_lock_irqsave(&sched_lock);
    prev = current;

    if (prev && prev->state == THREAD_RUNNING) {
        prev->state = THREAD_READY;
        list_add_tail(&prev->run_node, &runq);
    }
    if (list_empty(&runq)) {
        if (prev && prev->state != THREAD_DEAD) {
            prev->state = THREAD_RUNNING;
            current = prev;
        }
        spin_unlock_irqrestore(&sched_lock, flags);
        return;
    }
    next = list_first_entry(&runq, struct thread, run_node);
    list_del(&next->run_node);
    next->state = THREAD_RUNNING;
    next->cpu_ticks = 0;
    current = next;
    spin_unlock_irqrestore(&sched_lock, flags);

    if (prev != next) {
        if (prev) {
            arm_switch_context(&prev->sp, next->sp);
        } else {
            arm_switch_context(&g_sched_sp_dummy, next->sp);
        }
    }
}

void yield(void)
{
    schedule();
}

void sched_tick(void)
{
    struct thread *t = current;
    u64 now_ms = clock_uptime_ms();

    if (t && t->state == THREAD_RUNNING) {
        t->cpu_ticks++;
    }
    /* wake sleepers */
    {
        int i;
        for (i = 0; i < THREAD_MAX; i++) {
            if (threads[i].state == THREAD_SLEEPING &&
                (u64)threads[i].wake_at_ms <= now_ms) {
                enqueue(&threads[i]);
            }
        }
    }
    if (t && t->state == THREAD_RUNNING && t->cpu_ticks >= QUANTUM_TICKS &&
        t != idle_thread) {
        schedule();
    }
}

void msleep(u32 ms)
{
    struct thread *t = current;
    if (!t || ms == 0) {
        yield();
        return;
    }
    t->wake_at_ms = (u32)(clock_uptime_ms() + ms);
    t->state = THREAD_SLEEPING;
    schedule();
}

void thread_exit(void)
{
    struct thread *t = current;
    printk("sched: thread %s (pid %u) exited\n", t ? t->name : "?",
           t ? t->pid : 0);
    if (t) {
        t->state = THREAD_DEAD;
    }
    for (;;) {
        schedule();
        wfi();
    }
}

struct thread *thread_current(void)
{
    return current;
}

struct thread *thread_find_pid(u32 pid)
{
    int i;
    for (i = 0; i < THREAD_MAX; i++) {
        if (threads[i].state != THREAD_UNUSED && threads[i].pid == pid) {
            return &threads[i];
        }
    }
    return 0;
}

int sched_thread_count(void)
{
    int i, n = 0;
    for (i = 0; i < THREAD_MAX; i++) {
        if (threads[i].state != THREAD_UNUSED) {
            n++;
        }
    }
    return n;
}

void sched_dump(void)
{
    static const char *names[] = { "unused", "ready", "running", "sleep",
                                   "blocked", "dead" };
    int i;
    printk("  PID  STATE    TICKS  STACK   NAME\n");
    for (i = 0; i < THREAD_MAX; i++) {
        if (threads[i].state == THREAD_UNUSED) {
            continue;
        }
        printk("  %-4u %-8s %-6u %-7u %s%s\n", threads[i].pid,
               names[threads[i].state], threads[i].cpu_ticks,
               threads[i].stack_size, threads[i].name,
               (&threads[i] == current) ? " *" : "");
    }
}

void sched_start(void)
{
    printk("sched: starting %d thread(s)\n", sched_thread_count());
    irq_enable_all();
    schedule();
    /* never reached */
    for (;;) {
        wfi();
    }
}
