# NCEditor Release Baseline

Date: 2026-05-05  
Target: Qt 5.15 + qmake + QML + C++

## 1. Baseline Scope

- Large-file viewport uses `QQuickPaintedItem` windowed rendering.
- 2x2 pane layout, max 4 panes, no duplicate open path.
- Save path uses `QSaveFile` (temp-write + commit).
- Save/open/edit/search paths include low-memory protection and exception fallback.
- Close/open lifecycle fixed to avoid `DocumentSession` retention after pane close.

## 2. Default Runtime Parameters

- `NCEDITOR_PASTE_LIMIT_KB=10`
- `NCEDITOR_SEARCH_HIGHLIGHT_LIMIT=1000`
- `NCEDITOR_REPLACE_ALL_LIMIT=200`
- `NCEDITOR_ASYNC_SEARCH_THRESHOLD_KB=256`
- `NCEDITOR_SEARCH_DEBOUNCE_MS=80`
- `NCEDITOR_SEARCH_DEBOUNCE_LARGE_MS=180`
- `NCEDITOR_SEARCH_DEBOUNCE_LARGE_THRESHOLD_MB=5`
- `NCEDITOR_MAPPED_DECODE_THRESHOLD_MB=8`
- `NCEDITOR_MAX_OPEN_FILE_MB=200`
- `NCEDITOR_MAX_DOCUMENT_MB=150`

## 3. Build-Time Default

- `src/main.cpp`: `NCEDITOR_PERF_OVERLAY_ENABLED=0` (default off).

## 4. Current Limits

- Max panes: 4
- Replace-all disabled when match count exceeds configured limit.
- Paste blocked when single operation exceeds configured KB limit.

## 5. Release Validation Status

- Build: pass (`mingw32-make -j4`)
- User long-run feedback: open/edit/save/close/reopen flow is stable, no current crash repro.
- Remaining manual gate: execute full checklist in `docs/REGRESSION_CHECKLIST.md` before external release.

