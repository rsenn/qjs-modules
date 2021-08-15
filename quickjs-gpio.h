#ifndef QUICKJS_GPIO_H
#define QUICKJS_GPIO_H

struct gpio;

#include <quickjs.h>
#include <cutils.h>

struct gpio* js_gpio_data(JSContext*, JSValue value);
int js_gpio_init(JSContext*, JSModuleDef* m);
JSModuleDef* js_init_module_gpio(JSContext*, const char* module_name);

#endif /* defined(QUICKJS_GPIO_H) */
