#include <stddef.h>
#define T(t) (t*)0;
#define C(n) switch(n){case n:;}
struct stddef_test_struct { int i; };
static void f(void)
{
{void *p=NULL;}
C(offsetof(struct stddef_test_struct, i))
T(ptrdiff_t)
T(wchar_t)
T(size_t)
}
