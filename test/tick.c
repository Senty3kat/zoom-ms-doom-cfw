#include <stdint.h>
#include <c6x.h>
#define CPU_HZ 300000000u
volatile uint32_t results[8];
static uint64_t cycles(void) { uint32_t lo = TSCL; return ((uint64_t)TSCH << 32) | lo; }
uint32_t DG_GetTicksMs(void) { return (uint32_t)(cycles() / (CPU_HZ / 1000)); }
void DG_SleepMs(uint32_t ms) { uint64_t end = cycles() + (uint64_t)ms * (CPU_HZ / 1000); while (cycles() < end) ; }
int main(void)
{
    int i;
    TSCL = 0;
    for (i = 0; i < 8; i++) { DG_SleepMs(1); results[i] = DG_GetTicksMs(); }
    return 0;
}
