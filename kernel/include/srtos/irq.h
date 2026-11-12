/* SurfaceRT_OS - interrupt controller and interrupt routing */
#ifndef SRTOS_IRQ_H
#define SRTOS_IRQ_H

#include <srtos/types.h>

typedef void (*irq_handler_fn)(int irq, void *arg);

#define IRQ_NR 160

/* spin lock with IRQ masking */
typedef struct {
    volatile int locked;
} spinlock_t;

#define SPINLOCK_INIT { 0 }

static inline void spin_lock_init(spinlock_t *l)
{
    l->locked = 0;
}

void spin_lock(spinlock_t *l);
void spin_unlock(spinlock_t *l);
u32 spin_lock_irqsave(spinlock_t *l);
void spin_unlock_irqrestore(spinlock_t *l, u32 flags);

int gic_init(void);
void gic_enable_irq(int irq);
void gic_disable_irq(int irq);
void gic_set_priority(int irq, u8 prio);
void gic_set_type(int irq, int edge);
int gic_pending(void);
void gic_eoi(int irq);
void gic_dump(void);

int irq_register(int irq, irq_handler_fn fn, void *arg);
void irq_unregister(int irq);
void irq_dispatch(int irq);
void irq_init(void);

u32 irq_save(void);
void irq_restore(u32 flags);
void irq_enable_all(void);
void irq_disable_all(void);

#endif /* SRTOS_IRQ_H */
