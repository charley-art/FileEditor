# NCEditor Release Baseline

Date: 2026-05-05  
Target: Qt 5.15 + qmake + QML + C++

## 1. Baseline Scope

- Large-file viewport uses `QQuickPaintedItem` windowed rendering.
- 2x2 pane layout, max 4 panes, no duplicate open path.
- Save path uses `QSaveFile` (temp-write + commit).
- Save/open/edit/search paths include low-memory protection and exception fallback.
- Close/open lifecycle fixed to avoid `DocumentSession` retention after pane close.

## 2. Default Parameters

- Paste limit: `10 KB`
- Search highlight limit: `1000`
- Replace-all limit: `200`
- Async search threshold: `256 KB`
- Search debounce: `80 ms`
- Large search debounce: `180 ms`
- Large-search threshold: `5 MB`
- Mapped decode threshold: `8 MB`
- Max open file size: `200 MB`
- Max in-memory document size: `150 MB`

## 3. Current Limits

- Max panes: 4
- Replace-all disabled when match count exceeds the fixed limit.
- Paste blocked when single operation exceeds the configured KB limit.

## 4. Release Validation Status

- Build: pass (`mingw32-make -j4`)
- User long-run feedback: open/edit/save/close/reopen flow is stable, no current crash repro.
- Remaining manual gate: execute full checklist in `docs/REGRESSION_CHECKLIST.md` before external release.
