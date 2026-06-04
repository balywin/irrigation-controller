# OTA Upload Retry on WiFi Drop — Design Spec

**Date:** 2026-06-04  
**Status:** Approved

## Problem

When WiFi drops during a firmware OTA upload, `xhr.upload.onprogress` has already fired at 100% (bytes were handed to the TCP stack), but the ESP32 never received the final chunk so `Update.end()` was never called. The update is silently discarded. The user must manually re-select the file and retry, with no guidance from the UI.

## Goal

- Detect upload network failure and auto-retry up to 4 minutes.
- Wait for WiFi reconnect between attempts (probe every 3 s).
- Re-send the full file from byte 0 each retry (restart-from-scratch strategy).
- Distinguish network errors (retryable) from server errors (fatal, user must fix).
- Give up after 10 attempts and show a manual Retry button.

## Out of Scope

- Byte-level resumable upload (Content-Range / partial re-send).
- Persistent retry state across page reloads or ESP32 reboots.
- WebSocket-based OTA.

## Files Changed

| File | Change |
|------|--------|
| `src/ElefantOTA.cpp` | Add `Update.abort()` + state reset in `/ota/start` handler |
| `include/elefant_ota_html.h` | Replace `upload()` with state-machine retry logic |

## Architecture

```
user picks file → upload(attempt=1)
  GET /ota/start?mode=X
    firmware: if Update.isRunning() → Update.abort(); reset counters; Update.begin()
  POST /ota/upload  (full file, attempt N/10 shown in status)
    → 200 OK → "Done! Rebooting…" → reload after 10 s

  on network error (onerror / status 0):
    reset bar to 0% immediately
    if attempt < 10 → WAIT_RECONNECT
    else → FATAL

  on HTTP 4xx (bad hash, no space, bad mode):
    → FATAL immediately (user must fix file/mode)

WAIT_RECONNECT:
  status = "Connection lost — attempt N/10. Reconnecting…"
  every 3 s: probe GET /update (8 s AbortController timeout)
    probe 200 → reset bar to 0%, upload(attempt+1)
    probe fail → keep waiting

FATAL:
  status = "Upload failed after 10 attempts."
  show manual Retry button → resets attempt counter → upload(1)
```

## Firmware Changes (`src/ElefantOTA.cpp`)

In the `/ota/start` GET handler, immediately before `Update.begin()`:

```cpp
if (Update.isRunning()) {
    Update.abort();
}
_current_progress_size = 0;
_update_error_str = "";
```

`Update.isRunning()` returns true when `Update.begin()` was called but neither `Update.end()` nor `Update.abort()` has run. This guarantees a clean OTA partition state before each retry attempt, including the first call.

No changes to the `/ota/upload` handler — it already processes a fresh chunk sequence correctly.

## JS State Machine (`include/elefant_ota_html.h`)

### States

**UPLOADING** (default after file selected + button clicked)
- Progress bar: 0 → 100% via `xhr.upload.onprogress`
- Status: `"Uploading… (attempt N/10)"`
- Button: disabled

**WAIT_RECONNECT**
- Progress bar: immediately reset to 0% on entry (avoids misleading 100% display)
- Status: `"Connection lost — attempt N/10. Reconnecting…"`
- Probe: `fetch('/update', {signal: AbortController 8s})` every 3 s
- On probe 200: go to UPLOADING (attempt+1)
- Button: disabled

**FATAL**
- Status: `"Upload failed after 10 attempts."` or server error message
- Manual Retry button appears (resets attempt to 1, goes to UPLOADING)
- Upload button: re-enabled (user can also pick a new file)

**SUCCESS**
- Progress bar: 100%
- Status: `"Done! Device rebooting — reconnecting in 10 s…"`
- Page reloads after 10 s

### Error Classification

| Condition | Class | Action |
|-----------|-------|--------|
| `xhr.onerror` (network drop) | Retryable | → WAIT_RECONNECT |
| `xhr.status === 0` | Retryable | → WAIT_RECONNECT |
| `fetch('/ota/start')` network error | Retryable | → WAIT_RECONNECT |
| `fetch('/ota/start')` status 4xx | Fatal | → FATAL (no retry) |
| `xhr.status` 4xx | Fatal | → FATAL (no retry) |

### Key Invariants

- `file` and `mode` are captured once in closure and reused across all retries.
- Each retry calls the full sequence: `GET /ota/start` then `POST /ota/upload`.
- Probe uses `AbortController` to avoid hanging probe requests on slow reconnect.
- Attempt counter is local to the `upload()` closure; manual Retry resets it to 1.
- Button stays disabled throughout UPLOADING and WAIT_RECONNECT; only re-enabled on FATAL or SUCCESS.

## Testing Checklist

- [ ] Happy path: upload succeeds first try → "Done, rebooting"
- [ ] WiFi drop at 50%: bar freezes, status shows "Reconnecting", bar resets to 0% on probe success, upload restarts
- [ ] WiFi drop at 99% (100% shown by onprogress): same as above — retry correctly
- [ ] 10 failed attempts: manual Retry button appears, clicking it restarts from attempt 1
- [ ] Bad MD5 hash: 400 from `/ota/start` → fatal immediately, no retry
- [ ] WiFi comes back mid-probe-interval: probe detects within 3 s
- [ ] Server error mid-upload (4xx from `/ota/upload`): fatal, no retry
