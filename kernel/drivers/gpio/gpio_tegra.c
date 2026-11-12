/* SurfaceRT_OS - Tegra 3 GPIO controller */
#include <srtos/gpio.h>
#include <srtos/kernel.h>
#include <srtos/string.h>
#include "tegra30.h"

#define GPIO_PORTS 8
#define PINS_PER_PORT 8

static u8 g_owner[GPIO_PORTS * PINS_PER_PORT];

static inline uptr port_base(int pin)
{
    return T30_GPIO_BASE + (uptr)(pin / PINS_PER_PORT) * 0x100;
}

static inline u32 pin_bit(int pin)
{
    return BIT(pin % PINS_PER_PORT);
}

int gpio_init(void)
{
    memset(g_owner, 0, sizeof(g_owner));
    pr_info("gpio: %d ports at 0x%08x\n", GPIO_PORTS, T30_GPIO_BASE);
    return 0;
}

int gpio_request(int pin, const char *owner)
{
    if (pin < 0 || pin >= GPIO_PORTS * PINS_PER_PORT) {
        return -1;
    }
    if (g_owner[pin] && owner) {
        return -1;
    }
    if (owner) {
        g_owner[pin] = 1;
    }
    return 0;
}

void gpio_free(int pin)
{
    if (pin >= 0 && pin < GPIO_PORTS * PINS_PER_PORT) {
        g_owner[pin] = 0;
    }
}

int gpio_direction_output(int pin, int value)
{
    if (pin < 0 || pin >= GPIO_PORTS * PINS_PER_PORT) {
        return -1;
    }
    gpio_set_value(pin, value);
    writel(readl(port_base(pin) + GPIO_OE) | pin_bit(pin), port_base(pin) + GPIO_OE);
    return 0;
}

int gpio_direction_input(int pin)
{
    if (pin < 0 || pin >= GPIO_PORTS * PINS_PER_PORT) {
        return -1;
    }
    writel(readl(port_base(pin) + GPIO_OE) & ~pin_bit(pin),
           port_base(pin) + GPIO_OE);
    return 0;
}

void gpio_set_value(int pin, int value)
{
    uptr base;
    if (pin < 0 || pin >= GPIO_PORTS * PINS_PER_PORT) {
        return;
    }
    base = port_base(pin);
    if (value) {
        writel(readl(base + GPIO_OUT) | pin_bit(pin), base + GPIO_OUT);
    } else {
        writel(readl(base + GPIO_OUT) & ~pin_bit(pin), base + GPIO_OUT);
    }
    /* Tegra GPIOs also need OUT_EN and the output value in the CNF register */
    writel(readl(base + GPIO_CNF) | (BIT(0) << (pin % PINS_PER_PORT)) |
               (BIT(1) << (pin % PINS_PER_PORT)),
           base + GPIO_CNF);
}

int gpio_get_value(int pin)
{
    if (pin < 0 || pin >= GPIO_PORTS * PINS_PER_PORT) {
        return -1;
    }
    return (readl(port_base(pin) + GPIO_IN) & pin_bit(pin)) ? 1 : 0;
}

void gpio_dump(void)
{
    int p;
    for (p = 0; p < GPIO_PORTS; p++) {
        uptr base = T30_GPIO_BASE + (uptr)p * 0x100;
        printk("gpio%d: oe=0x%02x out=0x%02x in=0x%02x\n", p,
               readl(base + GPIO_OE) & 0xFF, readl(base + GPIO_OUT) & 0xFF,
               readl(base + GPIO_IN) & 0xFF);
    }
}
