/* SurfaceRT_OS - driver registration */
#ifndef SRTOS_DRIVER_H
#define SRTOS_DRIVER_H

#include <srtos/types.h>
#include <srtos/list.h>

enum driver_class {
    DRV_SERIAL = 0,
    DRV_GPIO,
    DRV_I2C,
    DRV_PMIC,
    DRV_BLOCK,
    DRV_DISPLAY,
    DRV_INPUT,
    DRV_NET,
    DRV_USB,
    DRV_WLAN,
    DRV_RTC,
    DRV_THERMAL,
    DRV_MAX,
};

struct driver {
    const char *name;
    enum driver_class cls;
    int (*probe)(struct driver *self);
    int (*remove)(struct driver *self);
    void *priv;
    u32 irq;
    int initialized;
    struct list_head node;
};

void driver_core_init(void);
int driver_register(struct driver *d);
int driver_init_all(void);
struct driver *driver_find(enum driver_class cls, int idx);
void driver_dump(void);
const char *driver_class_name(enum driver_class cls);

#endif
