#include "../recovery_notice.c"

#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    setenv("RK_RECOVERY_NOTICE_CONFIG", "/nonexistent/recovery-notice.conf", 1);
    setenv("RK_RECOVERY_NOTICE_LED_ROOT", "/nonexistent/leds", 1);
    setenv("RK_RECOVERY_NOTICE_INPUT_ROOT", "/nonexistent/input", 1);

    /* Missing optional indicators must never make recovery fail or crash. */
    recovery_notice_busy_start("test medium");
    recovery_notice_busy_tick();
    recovery_notice_busy_stop();
    puts("recovery notice optional-device test passed");
    return 0;
}
