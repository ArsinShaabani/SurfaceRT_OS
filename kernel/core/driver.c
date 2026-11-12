/* SurfaceRT_OS - driver registry */
#include <srtos/driver.h>
#include <srtos/kernel.h>
#include <srtos/string.h>

#define DRIVERS_MAX 32

static LIST_HEAD(driver_list);
static int nr_drivers;

static const char *class_names[DRV_MAX] = {
    "serial", "gpio", "i2c", "pmic", "block", "display",
    "input", "net", "usb", "wlan", "rtc", "thermal",
};

void driver_core_init(void)
{
    list_init(&driver_list);
    nr_drivers = 0;
}

const char *driver_class_name(enum driver_class cls)
{
    return (cls < DRV_MAX) ? class_names[cls] : "unknown";
}

int driver_register(struct driver *d)
{
    if (!d || nr_drivers >= DRIVERS_MAX) {
        return -1;
    }
    list_add_tail(&d->node, &driver_list);
    nr_drivers++;
    return 0;
}

int driver_init_all(void)
{
    struct driver *d;
    int ok = 0, fail = 0;

    list_for_each_entry(d, &driver_list, node)
    {
        if (d->initialized || !d->probe) {
            continue;
        }
        if (d->probe(d) == 0) {
            d->initialized = 1;
            ok++;
        } else {
            fail++;
            pr_warn("driver: %s (%s) probe failed\n", d->name,
                    driver_class_name(d->cls));
        }
    }
    pr_info("driver: %d initialised, %d failed\n", ok, fail);
    return ok;
}

struct driver *driver_find(enum driver_class cls, int idx)
{
    struct driver *d;
    int n = 0;
    list_for_each_entry(d, &driver_list, node)
    {
        if (d->cls == cls && d->initialized) {
            if (n++ == idx) {
                return d;
            }
        }
    }
    return 0;
}

void driver_dump(void)
{
    struct driver *d;
    printk("  CLASS     STATE  NAME\n");
    list_for_each_entry(d, &driver_list, node)
    {
        printk("  %-9s %-6s %s\n", driver_class_name(d->cls),
               d->initialized ? "ok" : "off", d->name);
    }
}
