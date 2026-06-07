# NCEditor Maintenance Notes

## 1. Core Architecture
- UI stack: `QML` + `QQuickPaintedItem` (custom viewport renderer).
- Editor viewport: `src/paintededitoritem.{h,cpp}`.
- Workspace orchestration and 4-pane behavior: `src/workspacecontroller.{h,cpp}`.
- Document model: `src/documentsession.{h,cpp}`.
- Text buffer: `src/piecetablebuffer.{h,cpp}`.
- Line mapping index: `src/lineindex.{h,cpp}` (treap-based dynamic line index).
- Floating long-press menu: single shared instance in `qml/main.qml` (`FloatingMenu`), owner pane switches on demand.

## 2. Performance Model (Large Files)
- Rendering is windowed: only visible lines (+small overscan) are painted.
- Search highlight is capped and throttled to avoid frame spikes.
- Large-file async search runs on piece-table snapshot streaming path (no full-text concat).
- Horizontal metrics are refreshed lazily during continuous scrolling or editing.
- Piece table keeps edits localized; line index updates avoid full rebuild.
- Large-file decode prefers memory-mapped read (`QFile::map`) to reduce peak memory.

## 3. Safety Rules in Code
- Save path uses temp-file semantics (`QSaveFile`) to reduce data-loss risk.
- Save encoding writes in adaptive small chunks to lower `bad_alloc` probability.
- Edit core path has exception recovery: on memory/unknown edit failure, it rebuilds line index/cache best-effort and keeps app alive.
- During saving, content-changing operations are blocked for the saving document session.
- During async opening, conflicting actions are blocked.
- Paste operation is guarded by size limit.
- Search highlight/match count is capped to avoid runaway memory or paint load.
- `canUndo/canRedo/canModify` state comes from each `DocumentSession` (Q_PROPERTY), not from controller duplication.
- `WorkspaceController` owns the `DocumentSession` list and focused session id;
  pane identity comes from `DocumentSession::sessionId`.
- Multi-select state belongs to `DocumentSession`; QML and painted items read it
  from the bound document session.

## 4. Configuration
- Editor configuration is provided by `src/editorconfig.{h,cpp}`.
- Document search, replace, open, decode, memory thresholds, pane limits, and
  paste limits are fixed code-level defaults.

## 5. Important Limits (Current Defaults)
- Max panes: `4`.
- Paste single-op limit: `10KB`.
- Search highlight cap: `1000`.
- Replace-all default cap: `200`.

## 6. Files to Touch for Common Changes
- Change button behavior or pane policy:
  - `src/workspacecontroller.cpp`
- Change selection, cursor movement, painting, scrolling:
  - `src/paintededitoritem.cpp`
  - `qml/components/EditorViewport.qml`
  - `qml/components/EditorPane.qml`
  - `qml/main.qml` (shared floating menu dispatch / owner switching)
- Change find/replace behavior:
  - `src/documentsession.cpp`
  - `qml/components/FindReplaceDialog.qml`
- Change persistence or encoding policy:
  - `src/documentsession.cpp`

## 7. Release Checklist
- Run build: `mingw32-make -j4`.
- Run regression list in `docs/REGRESSION_CHECKLIST.md`.
- Validate at least one large file (`>= 30MB`) and one stress file (`~100MB`).
- For fixed parameter values, use `docs/PARAMETER_GUIDE.md`.
- Release snapshot and defaults: `docs/RELEASE_BASELINE.md`.

## 8. Training Pack (CN)
- Maintenance learning path: `docs/MAINTENANCE_TRAINING_CN.md`.
- High-frequency troubleshooting templates: `docs/TROUBLESHOOTING_TEMPLATES_CN.md`.
- Release checklist/report templates: `docs/RELEASE_WORKFLOW_TEMPLATE_CN.md`.
