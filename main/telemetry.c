#include "telemetry.h"
#include "usb_serial.h"

#include <stdio.h>

/* Column layouts (first field is always message type):
 *
 * STAT,uptime_s,safety_state,usb,loop_avg_us,loop_max_us,overruns,glitches,total_panics
 *   safety_state: 0=PANIC 1=CLEAR 2=READY 3=OK
 *   usb:          0=disconnected 1=connected
 * PANIC_INFO,uptime_s,from_state,source_type,source_id,title,explanation,fix
 * PANIC_CLEARED,uptime_s,source_type,source_id,title
 * TRANS,uptime_s,from_state,to_state
 * EVENT,uptime_s,source,description
 */

static uint32_t s_panic_count = 0;

void telemetry_panic(uint32_t uptime_s, const panic_entry_t *entry,
                     const char *state_before)
{
    char buf[768];
    s_panic_count++;
    int written = snprintf(buf, sizeof(buf), "PANIC_INFO,%lu,%s,%s,%s,\"%s\",\"%s\",\"%s\"\n",
             (unsigned long)uptime_s,
             state_before,
             entry->source_type,
             entry->source_id,
             entry->title,
             entry->explanation,
             entry->fix);
    if (written >= (int)sizeof(buf)) {
        /* Truncated — patch a valid line ending so the CSV parser isn't corrupted */
        buf[sizeof(buf) - 3] = '"';
        buf[sizeof(buf) - 2] = '\n';
        buf[sizeof(buf) - 1] = '\0';
    }
    usb_serial_send(buf);
}

void telemetry_heartbeat(uint32_t uptime_s, int safety_state, bool usb,
                         uint32_t loop_avg_us, uint32_t loop_max_us,
                         uint32_t overruns, uint32_t glitches)
{
    char buf[128];
    snprintf(buf, sizeof(buf), "STAT,%lu,%d,%d,%lu,%lu,%lu,%lu,%lu\n",
             (unsigned long)uptime_s,
             safety_state,
             usb ? 1 : 0,
             (unsigned long)loop_avg_us,
             (unsigned long)loop_max_us,
             (unsigned long)overruns,
             (unsigned long)glitches,
             (unsigned long)s_panic_count);
    usb_serial_send(buf);
}

void telemetry_panic_cleared(uint32_t uptime_s, const panic_entry_t *entry)
{
    char buf[128];
    snprintf(buf, sizeof(buf), "PANIC_CLEARED,%lu,%s,%s,\"%s\"\n",
             (unsigned long)uptime_s,
             entry->source_type,
             entry->source_id,
             entry->title);
    usb_serial_send(buf);
}

void telemetry_transition(uint32_t uptime_s, const char *from, const char *to)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "TRANS,%lu,%s,%s\n",
             (unsigned long)uptime_s, from, to);
    usb_serial_send(buf);
}

void telemetry_event(uint32_t uptime_s, const char *source, const char *event)
{
    char buf[128];
    snprintf(buf, sizeof(buf), "EVENT,%lu,%s,%s\n",
             (unsigned long)uptime_s, source, event);
    usb_serial_send(buf);
}

uint32_t telemetry_panic_count(void)
{
    return s_panic_count;
}
