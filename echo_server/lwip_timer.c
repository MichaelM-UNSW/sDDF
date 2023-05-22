#include "timer.h"

#define TIMER_CH 1 
#define LWIP_TICK_MS 10
#define NS_IN_MS 1000000ULL

#define GET_TIME 0
#define SET_TIMEOUT 1

void
set_timeout(void)
{
    uint64_t timeout = (LWIP_TICK_MS * NS_IN_MS);
    sel4cp_mr_set(0, timeout);
    sel4cp_ppcall(TIMER_CH, sel4cp_msginfo_new(SET_TIMEOUT, 1));
}

u32_t 
sys_now(void)
{
    sel4cp_ppcall(TIMER_CH, sel4cp_msginfo_new(GET_TIME, 0));
    uint64_t time_now = seL4_GetMR(0);
    return time_now / NS_IN_MS;
}

