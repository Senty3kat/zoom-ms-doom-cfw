#include <stdint.h>
#include <c6x.h>
volatile uint32_t results[8];
static uint64_t cycles(void) { uint32_t lo = TSCL; return ((uint64_t)TSCH << 32) | lo; }
int main(void)
{
    int i;
    TSCL = 0;
    for (i = 0; i < 4; i++) {
        uint64_t t = cycles(), e = t + 3000;
        results[2 * i] = (uint32_t)t;
        while (cycles() < e) ;
        results[2 * i + 1] = (uint32_t)cycles();
    }
    return 0;
}
