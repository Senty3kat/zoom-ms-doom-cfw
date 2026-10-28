#include <stdint.h>
volatile uint32_t results[8];
volatile uint32_t u[4] = { 5437u * 35u, 9257u * 35u, 1000u, 35000u };
volatile int s[4] = { 190, 323, -7, 1 };
volatile int d = 1;
int main(void)
{
    int i;
    for (i = 0; i < 4; i++) {
        results[i] = u[i] / 1000u;
        results[4 + i] = s[i] / d;
    }
    return 0;
}
