# NCEditor Release Checklist and Report Template

## 1. Pre-release Checklist

### 1.1 Baseline
- [ ] Current code is based on the intended release branch.
- [ ] `docs/RELEASE_BASELINE.md` is up to date.
- [ ] Fixed parameter defaults are documented in `docs/PARAMETER_GUIDE.md`.

### 1.2 Build and Smoke Test
- [ ] `mingw32-make -j4` passes.
- [ ] The app starts and loads the main UI.
- [ ] No obsolete runtime tuning variables are required for DocumentSession.

### 1.3 Functional Regression
- [ ] Button rules: New / Open / Open More / Save / Save As / Close.
- [ ] Large files: open, scroll, and edit 30 MB and 100 MB files.
- [ ] Save protection: content-changing operations are blocked while saving.
- [ ] Find/replace limits: high-repeat content does not overflow and Replace All gating works.
- [ ] Chinese IME: preedit, commit, cursor position, and line behavior are correct.
- [ ] Close/reopen: repeated cycles show no obvious retention or crash.

### 1.4 Release Records
- [ ] Regression record is archived.
- [ ] Known limitations are updated.
- [ ] Version tag is created and pushed if this is an external release.

## 2. Release Report Template

```markdown
# NCEditor Release Report

## Version
- Branch:
- Commit:
- Tag:
- Build time:
- Build environment:

## Parameter Baseline
- DocumentSession parameters: fixed code defaults
- Paste limit: fixed code default

## Regression Result
- Total:
- Passed:
- Failed:
- Blocked:

## Key Scenarios
- 100 MB open/edit:
- Save protection:
- Find/replace limits:
- Chinese IME:
- Close/reopen stability:

## Known Limitations
- Limitation 1:
- Limitation 2:

## Release Recommendation
- Decision: release / hold
- Reason:
- Follow-up:
```

## 3. Maintenance Notes
- First deployment should run one stress regression with fixed defaults.
- If field stutter appears, use `docs/TROUBLESHOOTING_TEMPLATES_CN.md` to locate the responsible layer before changing code.
- Parameter changes now require code changes in `src/editorconfig.cpp`, followed by regression and documentation updates.
