#ifndef GPIO_H
#define GPIO_H

#include <stdbool.h>
#include <stdint.h>

struct gpio {
  int fd;
  uint32_t* map;
  bool debug;
};

bool gpio_open(struct gpio*);
void gpio_close(struct gpio*);
void gpio_init_pin(struct gpio*, const uint8_t pin, const bool output);
void gpio_set_pin(struct gpio*, const uint8_t pin, const bool value);
bool gpio_get_pin(struct gpio*, const uint8_t pin);

#endif // GPIO_H
