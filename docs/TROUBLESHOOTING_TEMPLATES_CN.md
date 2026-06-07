# NCEditor Troubleshooting Templates

## A. 100MB File Stutter

### Step 1: Confirm the fixed policy values
- Check `docs/PARAMETER_GUIDE.md`.
- Search, replace, open, decode, and memory thresholds are fixed in code.
- Do not try to tune these through environment variables.

### Step 2: Locate the bottleneck
- Rendering side: inspect `PaintedEditorItem` for viewport painting, horizontal scrolling, and search highlight drawing.
- Search side: inspect `DocumentSession::rebuildSearchCache`, `startQueuedSearch`, and `computeSearch`.
- Record whether the slowdown is caused by painting, searching, opening, or editing.

### Step 3: Prefer the smallest fix
- First: reduce unnecessary repaint/search work.
- Next: adjust local caching or visible-range prefetch behavior.
- Last: change core data structures only when the first two options are insufficient.

## B. Out of Memory / Exception

### Step 1: Confirm which fixed limit is involved
- Max open file size: `200 MB`.
- Max decoded document size: `150 MB`.
- Search highlight limit: `1000`.
- Replace-all limit: `200`.
- Paste limit: `10 KB`.

### Step 2: Confirm the low-memory path
- Open path should prefer `QFile::map` above the mapped decode threshold.
- Save path should use chunked encoding plus `QSaveFile`.
- Large-file search should use the piece-table snapshot path instead of full-text concatenation.

### Step 3: Verify recovery
- After the fix, test edit -> save -> close -> reopen for multiple rounds.
- Also test search on highly repetitive large content.

## C. Lifecycle Leak After Close

### Step 1: Reproduce with a stable sequence
- Open a large file -> edit -> save -> close -> reopen, repeating several rounds.
- Watch whether closed documents still receive callbacks.

### Step 2: Check ownership and connections
- Avoid strong captures of document sessions in long-lived lambdas.
- Prefer scoped connections, weak references, or object-bound callbacks.

### Step 3: Check close cleanup
- Validate pane close, focused session reassignment, and document connection cleanup.
- Confirm closed `DocumentSession` objects are removed from the workspace list and no longer receive controller callbacks.

## General Record Template

- Symptom:
- Reproduction steps:
- Suspected layer: UI / painting / document / buffer / index
- Verified entry points:
- Smallest proposed fix:
- Regression result:
- Compatibility impact:
