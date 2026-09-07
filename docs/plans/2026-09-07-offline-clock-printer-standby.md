# Offline clock and printer standby

## Findings

Memento Mori explicitly fell back to the dark CrossPoint screen when the birth
date was missing, the RTC read failed, or the clock date preceded the birth date.
Wi-Fi auto-sync and the setup prompt used a persisted "synced before" flag. That
flag did not establish whether the RTC was still valid. The clock status bar could
also reuse an old time indefinitely after a read failure.

Printer mode blocked auto-sleep and skipped the normal loop delay indefinitely.
It did not disable Wi-Fi modem sleep as the web server does. It also sampled
buttons again after the main loop, potentially clearing a new press event. These
are confirmed code findings. Whether radio sleep, scheduling, or another device
condition caused the reported idle print failure is not yet established.

## Behavior

- Memento Mori uses the RTC offline with no age limit on its last internet sync.
  One failed read gets one retry. Invalid calendar values are rejected. If a
  reliable date remains unavailable, the screen explains the problem and the
  recovery action, then the device sleeps normally. No Wi-Fi wakeups are added.
  The grid is calculated on entering sleep and remains static until the next
  sleep; it does not wake the device for daily redraws.
- An existing Wi-Fi connection triggers a bounded NTP attempt when the clock has
  never synced or is currently invalid. The NTP client stops on success or timeout.
  Valid clock readings do not cause repeated automatic syncs or settings writes.
- Printer mode uses the configured sleep timeout, starting when the service is
  ready. The countdown shows remaining minutes, rounded up, and refreshes once a
  minute. The mapped Confirm button is labeled "Reset timer". Button presses and
  completed pages renew the timer. Discovery requests do not.
- Timer expiry shows "Printer asleep", explains that Wi-Fi is off, and tells the
  user to wake with Power and reopen Printer. This screen takes priority over the
  selected sleep artwork for this timeout. The normal deep-sleep path tears down
  the services and radio. There is no background listener while asleep.
- If the global sleep timeout is "Never", printer mode uses ten minutes. Reading
  can remain awake indefinitely without making printer standby unbounded.
- While ready, printer mode disables radio sleep and uses the normal 10 ms loop
  delay. Wi-Fi sleep settings are restored on exit. Incoming page decoding and
  countdown rendering share the render lock. A transfer can finish before the
  idle timeout is evaluated again.
- Each printer HTTP response advertises `Connection: close` so persistent
  discovery traffic cannot hold the main loop and freeze the countdown. A full
  print request finishes before the connection closes. The host harness retains
  its default support for persistent connections.
- Network loss replaces the ready screen with a reconnecting message. On
  reconnection, the listener, address and discovery advertisement are refreshed.
  Discovery failure leaves the direct printer address visible. Startup failure
  turns the radio off and stays visible rather than silently returning home.

## Verification

Run `sh test/power_clock/run.sh` for the firmware clock code with controlled RTC
and NTP responses, plus the printer timer. It covers offline dates, transient and
persistent read failure, stale-cache rejection, invalid calendars, leap days,
UTC-offset rollover, missing hardware, stale NTP completion, sync readback
failure, the five-second timeout, timer reset, discovery polling, and timer wrap.
The protocol test sends real IPP discovery requests through the firmware's HTTP
and IPP code, verifying that a completed response releases the timed session.
It also verifies that a fragmented print request produces all expected pixels
before the connection closes, and that long attribute names do not misalign the
next IPP attribute.
The CI unit-test job runs this suite.

Build the X3/X4 firmware with `pio run -e default`.

Verified on 2026-09-07: the X3/X4 build, clock/timer/protocol regressions, full
repository formatting, whitespace, and strict static-analysis checks pass.
Printer buffers are explicitly initialized, and the parser consumes excess
attribute-name bytes through the same zero-length-safe path as shorter names.
The firmware has not been flashed. Battery current and the original idle-print
failure have not been verified on hardware.

Device checks remain necessary. There was no X3 connected during development:

1. Set Time to sleep to two minutes. In both Join Network and Create Hotspot,
   wait at least a minute without touching the reader, then print. The first
   page must arrive without a button press and renew the countdown.
2. Let the countdown decrease and press the button labeled Reset timer. It must
   return to two minutes. Repeat while viewing a stored page.
3. Leave the computer's print dialog open so discovery continues. Let the timer
   expire. Check the Printer asleep screen, radio shutdown, deep sleep, and wake
   instructions. Repeat with Quick Resume selected to check screen precedence.
4. Start a large print just before expiry. Let it finish and confirm that
   completed pages renew the timer. Abort a transfer with Left and verify that
   the printer remains responsive.
5. Disconnect and restore the joined network. Confirm the visible status and
   successful printing after reconnection, including a changed DHCP address.
6. Sync the clock, disconnect Wi-Fi, then read and sleep offline. Memento Mori
   should remain usable. After genuine clock loss, its message must explain the
   missing date; joining Wi-Fi with internet should recover it.
7. Compare battery current during ready, receiving, and timed-out deep sleep.
   Radio sleep is intentionally disabled while ready, so a lower standby current
   is not claimed. The timeout bounds that cost; firmware builds and host tests
   cannot measure it.
