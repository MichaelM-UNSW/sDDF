/*
 * Copyright 2022, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

/*
 * Very basic timer driver. Currently only permtis
 * a maximum of a single timeout per client for simplicity. 
 * 
 * Interfaces for clients:
 * seL4cp_ppcall() with 0 is a request to get the current time.
 * with a 1 is a request to set a timeout.
 */

#include <stdint.h>
#include <sel4cp.h>

#define GET_TIME 0
#define SET_TIMEOUT 1

#define CR 0
#define PR 1
#define SR 2
#define IR 3
#define OCR1 4
#define OCR2 5
#define OCR3 6
#define ICR1 7
#define ICR2 8
#define CNT 9

#define MAX_TIMEOUTS 2

uintptr_t gpt_regs;
static volatile uint32_t *gpt;

static uint32_t overflow_count;

static uint64_t timeouts[MAX_TIMEOUTS];
static sel4cp_channel active_channel = -1;
static bool timeout_active;
static uint64_t current_timeout;
static uint8_t pending_timeouts;


static uint64_t
get_ticks(void) 
{
    /* FIXME: If an overflow interrupt happens in the middle here we are in trouble */
    uint64_t overflow = overflow_count;
    uint32_t sr1 = gpt[SR];
    uint32_t cnt = gpt[CNT];
    uint32_t sr2 = gpt[SR];
    if ((sr2 & (1 << 5)) && (!(sr1 & (1 << 5)))) {
        /* rolled-over during - 64-bit time must be the overflow */
        cnt = gpt[CNT];
        overflow++;
    }
    return (overflow << 32) | cnt;
}

void
irq(sel4cp_channel ch)
{
    uint32_t sr = gpt[SR];
    gpt[SR] = sr;

    if (sr & (1 << 5)) {
        overflow_count++;
    }

    if (sr & 1) {
        gpt[IR] &= ~1;
        timeout_active = false;
        sel4cp_channel curr_channel = active_channel;
        timeouts[curr_channel] = 0;
        // notify the client.
        sel4cp_notify(curr_channel);
    }

    if (pending_timeouts && !timeout_active) {
        /* find next timeout */
        uint64_t next_timeout = UINT64_MAX;
        sel4cp_channel ch = -1;
        for (unsigned i = 0; i < MAX_TIMEOUTS; i++) {
            if (timeouts[i] != 0 && timeouts[i] < next_timeout) {
                next_timeout = timeouts[i];
                ch = i;
            }
        }
        /* FIXME: Is there a race here?  -- Probably! Fix it later. */
        if (ch != -1 && overflow_count == (next_timeout >> 32)) {
            pending_timeouts--;
            gpt[OCR1] = next_timeout;
            gpt[IR] |= 1;
            timeout_active = true;
            current_timeout = next_timeout;
            active_channel = ch;
        }
    }
}

void
notified(sel4cp_channel ch)
{
    // we have one job
    irq(ch);
    sel4cp_irq_ack_delayed(ch);
}

seL4_MessageInfo_t
protected(sel4cp_channel ch, sel4cp_msginfo msginfo)
{
    uint64_t rel_timeout, cur_ticks, abs_timeout;
    switch (sel4cp_msginfo_get_label(msginfo)) {
        case GET_TIME:
            // Just wants the time. 
            seL4_SetMR(0, get_ticks());
            return sel4cp_msginfo_new(0, 1);
        case SET_TIMEOUT:
            // Request to set a timeout. 
            rel_timeout = seL4_GetMR(0);
            cur_ticks = get_ticks();
            abs_timeout = cur_ticks + rel_timeout;

            timeouts[ch] = abs_timeout;
            if ((!timeout_active || abs_timeout < current_timeout)
                && (cur_ticks >> 32 == abs_timeout >> 32)) {
                if (timeout_active) {
                    /* there current timeout is now treated as pending */
                    pending_timeouts++;
                }
                gpt[OCR1] = abs_timeout;
                gpt[IR] |= 1;
                timeout_active = true;
                current_timeout = abs_timeout;
                active_channel = ch;
            } else {
                pending_timeouts++;
            }
            break;
        default:
            sel4cp_dbg_puts("Unknown request to timer from client\n");
            break;
    }

    return sel4cp_msginfo_new(0, 0);
}

void
init(void)
{
    gpt = (volatile uint32_t *) gpt_regs;

    uint32_t cr = (
        (1 << 9) | // Free run mode
        (1 << 6) | // Peripheral clocks
        (1) // Enable
    );

    gpt[CR] = cr;

    gpt[IR] = (
        (1 << 5) // rollover interrupt
    );
}
