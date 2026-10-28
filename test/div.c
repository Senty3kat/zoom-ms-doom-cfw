#include <stdint.h>
volatile uint32_t results[8];
volatile uint64_t in[4] = { 300000ull, 3000000ull, 123456789012ull, 299999ull };
int main(void)
{
    int i;
    for (i = 0; i < 4; i++) {
        uint64_t q = in[i] / 300000u;
        results[2 * i] = (uint32_t)q;
        results[2 * i + 1] = (uint32_t)(q >> 32);
    }
    return 0;
}
