# NCEditor Parameter Guide

NCEditor currently uses code-defined fixed parameters from `src/editorconfig.cpp`.
These values are not read from environment variables.

## 1. Search / Replace

| Parameter | Fixed Value | Notes |
|---|---:|---|
| Search highlight limit | `1000` | Max stored match positions for highlighting and match display. |
| Replace-all limit | `200` | Replace All is enabled only when match count is at or below this value. |
| Async search threshold | `256 KB` | Documents above this text size use the async search path. |
| Search debounce | `80 ms` | Input debounce for normal-size documents. |
| Large search debounce | `180 ms` | Input debounce for large documents. |
| Large-search threshold | `5 MB` | Document-size threshold for large-search debounce. |

## 2. Open / Decode / Memory

| Parameter | Fixed Value | Notes |
|---|---:|---|
| Mapped decode threshold | `8 MB` | Files above this size prefer memory-mapped decode. |
| Max open file size | `200 MB` | Hard upper bound for opening file bytes. |
| Max in-memory document size | `150 MB` | Hard upper bound for decoded text in memory. |
| Paste limit | `10 KB` | Max bytes allowed per paste operation. |

## 3. Changing Defaults

Change these values in `src/editorconfig.cpp` when the editor policy changes.
After changing them, run the large-file regression checklist and update this guide.
