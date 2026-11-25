/* cpuid.h: gcc's header with dietlibc's __leaf macro out of the way.
 * rule: gcc names a parameter __leaf, dietlibc's cdefs.h defines a macro of that name. */
#pragma push_macro("__leaf")
#undef __leaf
#include_next <cpuid.h>
#pragma pop_macro("__leaf")
