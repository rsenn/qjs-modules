#ifndef GPIO_H
#define GPIO_H

#include <stdbool.h>
#include <stdint.h>

#define GPIO_MAPSIZE 0xA0

/**
 * \defgroup gpio gpio: Raspberry Pi GPIO
 * @{
 */
struct gpio {
  int fd;
  uint32_t* map;
  bool debug;
  int ref_count;
};

bool gpio_open(struct gpio*);
void gpio_close(struct gpio*);
void gpio_init_pin(struct gpio*, const uint8_t pin, const bool output);
void gpio_set_pin(struct gpio*, const uint8_t pin, const bool value);
bool gpio_get_pin(struct gpio*, const uint8_t pin);

static inline struct gpio*
gpio_dup(struct gpio* gp) {
  ++gp->ref_count;
  return gp;
}

/**
 * @}
 */
#endif // GPIO_H
