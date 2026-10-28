#include <fnmatch.h>
#define C(n) switch(n){case n:;}
static void f(void)
{
C(FNM_NOMATCH)
C(FNM_PATHNAME)
C(FNM_PERIOD)
C(FNM_NOESCAPE)
{int(*p)(const char*,const char*,int) = fnmatch;}
}
