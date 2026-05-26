# NC Pre-Interpreter Architecture

The NC pre-interpreter is intentionally separate from the large-file editor.
It is a language core that can be called by the editor, a command-line tool, or
future tests without depending on `DocumentSession`, `PieceTableBuffer`, QML, or
painting code.

## Boundaries

- `src/nc/ncparser.*` owns NC tokenization, block parsing, modal-state checks,
  and diagnostics.
- `src/nc/ncdialects.*` owns built-in C++ profile factories. Add new machine
  profiles there unless a test needs a temporary local profile.
- `src/nc/ncdiagnosticmessages.*` owns diagnostic-code message lookup. The
  parser keeps stable codes and English fallback messages; UI code can request
  simplified Chinese text without coupling message localization to parsing.
- Editor code should adapt document text to the parser through
  `Parser::parseLines(lineCount, lineReader)`.
- Standalone callers can use `Parser::parseText(text)`.
- The parser returns plain data: blocks, words, comments, and diagnostics. It
  does not know how diagnostics are rendered.

## Integration Shape

The editor-side adapter should stay thin:

```cpp
nc::Parser parser;
const nc::ParseResult result = parser.parseLines(session->lineCount(), [session](int line) {
    return session->lineTextAt(line);
});
```

For large files, this keeps ownership with the editor while the parser remains a
replaceable component. A later incremental parser can keep the same public
contract and add a changed-line range API without changing the editor surface.

## Background Diagnostics

Editor diagnostics run outside the paint path. `PaintedEditorItem` collects a
value snapshot on the UI thread:

- slot index and text revision;
- visible line range plus context range;
- copied `QStringList` line text.

The background task receives only that snapshot, calls `nc::Parser`, and returns
plain diagnostic markers. It must not access `WorkspaceController`,
`DocumentSession`, QML objects, or the painted item. Returned results are applied
only when their request id, text revision, and visible range still match the
current editor state; stale results are discarded.

`PaintedEditorItem` localizes diagnostic text after parsing by calling
`nc::diagnosticMessage(diagnostic.code)`. Unknown codes fall back to the parser's
message, so new parser checks remain visible before their Chinese text is added.

## Dialect Profiles

Dialect differences live in C++ profiles rather than in the editor. The default
profile is `nc::lynucEdmProfile()` from `ncdialects.h`.

A profile owns:

- supported `G` codes and their modal groups;
- supported default `M` codes;
- default modal state;
- grouped rule switches under `DialectRuleSet`, covering units, compensation,
  transforms, tool/spindle rules, cycles, and program-level limits.
- semantic diagnostic severity overrides by diagnostic code. Lexical/parser
  errors such as invalid numbers stay strict and are not profile-relaxed.

Callers can pass a profile through `ParserOptions`:

```cpp
nc::DialectProfile profile = nc::makeLynucEdmProfile();
profile.name = QStringLiteral("My Machine");
profile.rules.units.supportsG20 = true;
profile.rules.cycles.fixedCyclesRequiringZrf.clear();
profile.rules.severityOverrides = {
    {QStringLiteral("NC_G20_UNSUPPORTED"), nc::DiagnosticSeverity::Warning}
};

nc::ParserOptions options;
options.profile = &profile;
nc::Parser parser(options);
```

Adding a new machine dialect should start from `makeLynucEdmProfile()` in
`ncdialects.cpp` and change only the supported-code tables or grouped rule
switches that differ. `makeSampleRelaxedProfile()` is a small example of this
pattern. The editor does not need to change.

## Current Rule Coverage

The first parser pass includes:

- NC address-word tokenization.
- Parenthesized comments and macro expression bracket checks.
- Unknown `G`/default `M` code diagnostics based on the Lynuc milling manual.
- Modal `G` group conflict checks.
- Default modal state tracking for motion, plane, distance, arc center, feed,
  cutter compensation, tool length compensation, and fixed cycles.
- Common semantic checks for arcs, `G04`, fixed cycles, `M98/G65` repeat count,
  duplicate addresses, and excessive `M` codes in one block.

This is a base layer. Machine-specific EDM rules should be added as dialect
profiles rather than hard-coded into the editor.

## Smoke Test Baseline

The parser has a standalone QtCore smoke test that does not link the editor,
large-file storage, QML, or painting code. Build and run it before changing NC
rules or dialect profiles:

```powershell
qmake -o tests/Makefile.ncparser_smoke tests/ncparser_smoke.pro -spec win32-g++ "CONFIG+=debug"
mingw32-make -C tests -f Makefile.ncparser_smoke
tests/bin/ncparser_smoke.exe
```

The smoke test checks default Lynuc/EDM diagnostics, modal-state carryover,
fixed cycles, subprogram repeat counts, high-speed cycle pairing, local profile
overrides, the sample relaxed dialect, diagnostic message lookup, and basic
macro-address compatibility.
