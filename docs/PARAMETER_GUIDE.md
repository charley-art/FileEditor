# NCEditor Parameter Guide

This guide lists runtime environment variables for large-file tuning.
If an env var is not set, NCEditor uses the default value.

## 1. Search / Replace

| Env Var | Default | Suggested Range | Notes |
|---|---:|---:|---|
| `NCEDITOR_SEARCH_HIGHLIGHT_LIMIT` | `1000` | `500 ~ 5000` | Max highlighted matches. Lower value reduces paint cost on repetitive files. |
| `NCEDITOR_REPLACE_ALL_LIMIT` | `200` | `100 ~ 2000` | Replace-all is enabled only when match count <= this value. |
| `NCEDITOR_ASYNC_SEARCH_THRESHOLD_KB` | `256` | `128 ~ 2048` | Above this size, search enters async path. |
| `NCEDITOR_SEARCH_DEBOUNCE_MS` | `80` | `60 ~ 250` | Input debounce for normal-size docs. |
| `NCEDITOR_SEARCH_DEBOUNCE_LARGE_MS` | `180` | `120 ~ 500` | Input debounce for large docs. |
| `NCEDITOR_SEARCH_DEBOUNCE_LARGE_THRESHOLD_MB` | `5` | `2 ~ 64` | File-size threshold for "large-doc debounce". |

## 2. Open / Decode / Memory

| Env Var | Default | Suggested Range | Notes |
|---|---:|---:|---|
| `NCEDITOR_MAPPED_DECODE_THRESHOLD_MB` | `8` | `4 ~ 64` | Files above this size prefer memory-mapped decode. |
| `NCEDITOR_MAX_OPEN_FILE_MB` | `200` | `100 ~ 500` | Hard upper bound for open-file bytes. |
| `NCEDITOR_MAX_DOCUMENT_MB` | `150` | `100 ~ 500` | Hard upper bound for decoded text in memory. |
| `NCEDITOR_PASTE_LIMIT_KB` | `10` | `10 ~ 1024` | Max bytes allowed per paste operation. |

## 3. Quick Profiles

### Low-memory machine (4~8 GB RAM)
- `NCEDITOR_SEARCH_HIGHLIGHT_LIMIT=600`
- `NCEDITOR_REPLACE_ALL_LIMIT=120`
- `NCEDITOR_ASYNC_SEARCH_THRESHOLD_KB=128`
- `NCEDITOR_SEARCH_DEBOUNCE_MS=120`
- `NCEDITOR_SEARCH_DEBOUNCE_LARGE_MS=260`
- `NCEDITOR_MAPPED_DECODE_THRESHOLD_MB=4`

### Standard machine (16 GB RAM)
- Keep defaults first.
- If search jitter appears on repetitive 100MB files:
  - `NCEDITOR_SEARCH_HIGHLIGHT_LIMIT=800`
  - `NCEDITOR_SEARCH_DEBOUNCE_LARGE_MS=220`

### High-memory machine (32 GB+ RAM)
- `NCEDITOR_SEARCH_HIGHLIGHT_LIMIT=1500`
- `NCEDITOR_REPLACE_ALL_LIMIT=500`
- `NCEDITOR_ASYNC_SEARCH_THRESHOLD_KB=512`

## 4. Example (Windows PowerShell)

```powershell
$env:NCEDITOR_SEARCH_HIGHLIGHT_LIMIT = "800"
$env:NCEDITOR_SEARCH_DEBOUNCE_LARGE_MS = "220"
$env:NCEDITOR_MAX_OPEN_FILE_MB = "250"
.\debug\NCEditor.exe
```

