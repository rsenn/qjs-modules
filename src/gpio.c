#ifdef HAVE_SYS_MMAN_H
#include <sys/mman.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "gpio.h"

#ifndef O_CLOEXEC
#define O_CLOEXEC 02000000
#endif

/**
 * \addtogroup gpio
 * @{
 */
#define PERIPHERALS_BASE_ADDR (0x20200000)
#define MAP_SIZE (0xA0)

static const int fsel_shift[] = {
    0, 3, 6, 9, 12, 15, 18, 21, 24, 27, 0, 3, 6, 9, 12, 15, 18, 21, 24, 27, 0, 3, 6, 9, 12, 15, 18, 21, 24, 27,
    0, 3, 6, 9, 12, 15, 18, 21, 24, 27, 0, 3, 6, 9, 12, 15, 18, 21, 24, 27, 0, 3, 6, 9, 12, 15, 18, 21, 24, 27,
};

static const int fsel[] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
};

static const int set[] = {
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    7, 7, 7, 7, 7, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
};

static const int set_shift[] = {
    0,  1,  2,  3,  4,  5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26,
    27, 28, 29, 30, 31, 0, 1, 2, 3, 4, 5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21,
};

static const int clr[] = {
    10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,
    10, 10, 10, 10, 10, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11,
};

static const int* clr_shift = set_shift;

static const int lvl[] = {
    13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13,
    13, 13, 13, 13, 13, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14,
};

static const int* lvl_shift = set_shift;

bool
gpio_open(struct gpio* gpio) {

  const char* dbg = getenv("DEBUG");

  if(dbg && strstr(dbg, "gpio"))
    gpio->debug = true;

  if((gpio->fd = open("/dev/gpiomem", O_RDWR | O_SYNC | O_CLOEXEC)) < 0) {
    if(gpio->debug)
      fprintf(stderr, "Could not open /dev/gpiomem: %s\n", strerror(errno));
    return false;
  }

  if((gpio->map = mmap(NULL, MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, gpio->fd, PERIPHERALS_BASE_ADDR)) ==
     MAP_FAILED) {
    if(gpio->debug)
      fprintf(stderr, "Could not map gpio memory: %s\n", strerror(errno));
    return false;
  }

  if(gpio->debug)
    fprintf(stdout, "GPIO initialized (%d, %p)\n", gpio->fd, gpio->map);

  gpio->ref_count = 1;

  return true;
}

void
gpio_close(struct gpio* gpio) {
  if(--gpio->ref_count == 0) {
    munmap(gpio->map, MAP_SIZE);
    gpio->map = 0;
    close(gpio->fd);
    gpio->fd = -1;
  }
}

void
gpio_init_pin(struct gpio* gpio, const uint8_t pin, const bool output) {
  *(gpio->map + fsel[pin]) &= ~(1 << fsel_shift[pin]);
  *(gpio->map + fsel[pin]) |= (output << fsel_shift[pin]);

  if(gpio->debug)
    fprintf(stdout, "Pin %d set to %s\n", pin, output ? "output" : "input");
}

void
gpio_set_pin(struct gpio* gpio, const uint8_t pin, const bool value) {
  if(value) {
    *(gpio->map + set[pin]) &= ~(1 << set_shift[pin]);
    *(gpio->map + set[pin]) |= (1 << set_shift[pin]);
  } else {
    *(gpio->map + clr[pin]) &= ~(1 << clr_shift[pin]);
    *(gpio->map + clr[pin]) |= (1 << clr_shift[pin]);
  }

  if(gpio->debug)
    fprintf(stdout, "Set pin %d to value %d\n", pin, value);
}

bool
gpio_get_pin(struct gpio* gpio, const uint8_t pin) {
  const bool value = *(gpio->map + lvl[pin]) & (1 << lvl_shift[pin]);

  if(gpio->debug)
    fprintf(stdout, "Get pin %d at value %d\n", pin, value);

  return value;
}

/**
 * @}
 */
#endif /* defined(HAVE_SYS_MMAN_H) */
