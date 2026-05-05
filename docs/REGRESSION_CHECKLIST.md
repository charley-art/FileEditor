# NCEditor Regression Checklist

## 1. Window and Button Rules
- Start app: no editor window should be visible.
- Click `New` when no window exists: create exactly one empty editor.
- Click `New` when a window is focused: clear current document and set to unnamed state.
- Click `Open` with one focused window: replace only focused window content.
- Click `Open More`: add a new window only when current count `< 4`.
- Click `Close`: close only focused window.
- Verify maximum visible windows is 4.

## 1.1 Build/Debug Switches
- Verify performance overlay switch is controlled by macro:
  - `src/main.cpp` -> `NCEDITOR_PERF_OVERLAY_ENABLED`.
  - Set `0` and confirm overlay hidden.
  - Set `1` and confirm overlay visible.

## 2. Save Safety Rules
- Open a file and edit it.
- Click `Save`: while saving is running, verify content-modifying operations are blocked.
- During saving, verify `New/Open/Open More/Close` are blocked.
- Verify sidebar state text switches to `Saving`.
- Save with power-loss safety path:
  - confirm saved file is complete and not corrupted after multiple save cycles.

## 2.1 Open Safety Rules
- Open a large file and immediately try `New/Open/Open More/Save/Save As/Close`.
- Verify these actions stay disabled while file loading is in progress.
- Verify sidebar state text switches to `Opening`.

## 3. Large File Open
- Open a ~30MB text file: app should remain responsive (no fake hang/crash).
- Open a ~100MB text file:
  - editor should load successfully;
  - vertical scrolling should be smooth;
  - no abnormal line spacing.
- Open files with different encodings:
  - UTF-8 (with/without BOM), GB18030, UTF-16LE/BE (with BOM), UTF-32LE/BE (with BOM)
  - verify text is not mojibake.
- Open UTF-16LE/BE files without BOM (if available):
  - verify heuristic decoding still shows readable text.

## 4. Long Line Rendering
- Open file containing a very long single line (100k+ chars).
- Scroll horizontally to far right:
  - visible text should continue rendering;
  - no obvious stutter/freeze;
  - cursor position should remain correct when clicking/typing.

## 5. Selection and Floating Menu
- Without enabling multi-select, mouse drag should scroll viewport, not create selection.
- Enable multi-select from long-press menu:
  - drag selection should work;
  - drag to top/bottom edge should auto-scroll;
  - menu should stay until pressing `Close`.

## 6. Clipboard Limits
- Copy normal selection and paste: should work.
- Paste content >10KB:
  - operation should be blocked;
  - toast should explain reason;
  - app must not crash.
- (Optional) Set environment variable `NCEDITOR_PASTE_LIMIT_KB` (for example `20`) before app launch:
  - paste-limit behavior and toast text should follow the configured threshold.

## 7. Find/Replace Stability
- Open repeated-content large file (30MB+).
- Open find dialog, type query continuously:
  - no crash;
  - old search tasks should not overwrite latest result;
  - UI shows `Searching...` during async search.
- Verify match cap behavior:
  - match count display shows `1000+` when overflow.
- Verify `Replace All`:
  - enabled only when match count is within threshold;
  - disabled for overflow/high-count cases.

## 8. Chinese IME
- Switch to Chinese IME in editor.
- Type pinyin and choose candidates:
  - preedit text should display;
  - committed Chinese text should insert at cursor correctly;
  - no jump to wrong line, no duplicated ASCII insertion.

## 9. Multi-document Focus
- With 2~4 windows open, switch focus by click.
- Verify operations (find/save/close/edit) always apply to focused window only.

## 10. Exit and Reopen
- Edit file without save, close window/app:
  - discard/save prompts should behave correctly.
- Reopen app and files:
  - no crash on startup;
  - layout and basic operations remain stable.
