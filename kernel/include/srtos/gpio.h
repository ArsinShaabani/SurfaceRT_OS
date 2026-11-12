/* SurfaceRT_OS - Tegra GPIO */
#ifndef SRTOS_GPIO_H
#define SRTOS_GPIO_H

#include <srtos/types.h>

int gpio_init(void);
int gpio_direction_output(int pin, int value);
int gpio_direction_input(int pin);
void gpio_set_value(int pin, int value);
int gpio_get_value(int pin);
int gpio_request(int pin, const char *owner);
void gpio_free(int pin);
void gpio_dump(void);

#define TEGRA_GPIO(port, bit) (((port) * 8) + (bit))

#endif
