# QR Codes Feature — Design

Date: 2026-07-27
Status: Implemented

## Goal

Let the device create, save and display QR codes ("broadcast links"), with
the same lock-screen behavior as reader pages: a displayed code survives deep
sleep (moon icon bottom-left) and wake resumes into it.

## Storage

One file per code under `/qrcodes` on the SD card:

- Filename minus `.txt` is the display name.
- File content is the raw QR payload string.
- Codes can also be created from a computer or via WebDAV by dropping
  `.txt` files into `/qrcodes`; trailing newlines are stripped on load.

No binary format, no cache versioning impact. `src/util/QrStore.{h,cpp}`
provides list (sorted), load, save, name sanitizing and unique-name suffixing.
Payloads are capped at 2953 bytes (QR version 40, ECC_LOW, byte mode —
matching `QrUtils::MAX_QR_CAPACITY`).

## UI

New home menu entry **QR Codes** (`HomeMenuItem::QR_CODES`,
`ActivityManager::goToQrCodes()`), opening `src/activities/qr/`:

- `QrMenuActivity` — two entries: Create / Saved.
- `QrCreateActivity` — type picker (URL, WiFi, Phone, Email, SMS), then
  chains `KeyboardEntryActivity` per field via `startActivityForResult`.
  WiFi order is SSID → security (WPA/WEP/open) → password (skipped for
  open networks). Payload formats: raw URL, `WIFI:T:<T>;S:<ssid>;P:<pass>;;`
  (with `\ ; , : "` escaping), `tel:`, `mailto:`, `SMSTO:<num>:<msg>`.
  Finishes by naming the code (sanitized, deduped with " 2"/" 3" suffixes),
  saving, and opening the viewer.
- `QrSavedListActivity` — sorted list of saved codes.
- `QrCodeViewActivity` — fullscreen code via `QrUtils::drawQrCode`, name
  centered below, `n / m` position indicator when more than one code exists.
  PageBack/PageForward, Left/Right and horizontal swipes cycle through the
  saved codes; Back/Confirm/tap exits.

## Sleep and resume

Mirrors the reader's quick-resume pattern:

- `Activity::qrSleepName()` (virtual, default nullptr) reports the displayed
  code's name; `ActivityManager::qrSleepName()` forwards from the current
  activity.
- `enterDeepSleep()` stores it in `CrossPointState::sleepQrName` (persisted
  in `/.crosspoint/state.json`) before saving state. Empty when sleeping
  from anywhere else, so staleness cannot occur.
- `SleepActivity::onEnter()` short-circuits to
  `renderLastScreenSleepScreen()` when `sleepQrName` is set: the code stays
  on the panel with the moon icon bottom-left, regardless of the configured
  sleep screen.
- Boot routing in `main.cpp` resumes into `QrCodeViewActivity` when
  `sleepQrName` is set (unless Back is held, same escape hatch as the
  reader). The state is cleared and saved before entering the viewer so a
  crash cannot boot-loop.

## Memory

Nothing survives an activity exit: the QR render buffer is heap-allocated
per render inside `QrUtils` (already the case), the saved-name vector is
reserved once per entry, payloads are ≤ 2953 bytes. No new tasks, no new
caches, no settings writes on interaction (a save only happens on the
explicit create flow).

## Out of scope (deliberately)

- Deleting/renaming codes on-device: manage `/qrcodes` via the file
  transfer web UI or a computer.
- vCard/MeCard and geo payloads: multi-field entry cost outweighs benefit.
- NFC: the ESP32-C3 has no NFC peripheral; QR is the broadcast mechanism.
