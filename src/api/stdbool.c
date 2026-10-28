#include <stdbool.h>
#define T(t) (t*)0;
#define C(n) switch(n){case n:;}
static void f(void)
{
T(bool)
C(true)
C(false)
C(__bool_true_false_are_defined)
}
