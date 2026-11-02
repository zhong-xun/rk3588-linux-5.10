/*
 * RKNETUPDATE-MANAGED-RECOVERY-NOTICE-V1
 * Hardware-independent completion notice for removable-media recovery updates.
 */

#ifndef RECOVERY_NOTICE_H
#define RECOVERY_NOTICE_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Blink a status LED and sound a supported input beeper while recovery waits
 * for the update medium to be removed.  The function is best-effort: missing
 * indicators never change the update result or the removal/reboot policy.  If
 * device_path is missing, the function deliberately keeps indicating forever
 * so an unverified device name can never trigger an automatic reboot.  A
 * detected removal must remain stable for one second before the function
 * returns, preventing transient device-node loss from causing a reboot.
 *
 * Optional board overrides are read from /etc/recovery-notice.conf:
 *
 *   led=auto|none|<LED class name>
 *   beeper=auto|none|<event path or exact pwm-beeper input-device name>
 *   blink_ms=250
 *   beep_count=10
 *   beep_hz=1000
 *   beep_on_ms=150
 *   beep_off_ms=150
 *
 * Overlong LED or beeper selectors are rejected and disable only that
 * indicator; they are never truncated into a different device selector.
 */
int recovery_notice_wait_for_removal(const char *media_name,
                                     const char *device_path);

#ifdef __cplusplus
}
#endif

#endif
