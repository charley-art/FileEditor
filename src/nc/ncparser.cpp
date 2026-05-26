#include "ncparser.h"

#include "ncdialects.h"

#include <algorithm>
#include <cmath>

#include <QHash>
#include <QStringList>

namespace nc {
namespace {

struct ModalState {
    int motionGroup = 1;
    QString motion;
    QString plane;
    QString distanceMode;
    QString arcCenterMode;
    QString feedMode;
    QString cutterComp;
    QString toolLengthComp;
    QString fixedCycle;
    bool hasSelectedTool = false;
    bool hasSpindleSpeed = false;
    bool highSpeedCycleActive = false;
};

const DialectProfile *activeProfile(const ParserOptions &options)
{
    return options.profile ? options.profile : &lynucEdmProfile();
}

int gCodeGroup(const DialectProfile &profile, const QString &code)
{
    for (const GCodeDefinition &definition : profile.gCodes) {
        if (definition.code == code) {
            return definition.group;
        }
    }
    return -1;
}

bool containsCode(const QVector<QString> &codes, const QString &code)
{
    return std::find(codes.cbegin(), codes.cend(), code) != codes.cend();
}

int resolvedMaxMCodeCount(const ParserOptions &options, const DialectProfile &profile)
{
    return options.maxMCodeCountPerBlock >= 0
        ? options.maxMCodeCountPerBlock
        : profile.rules.programs.maxMCodeCountPerBlock;
}

ModalState makeInitialModalState(const DialectProfile &profile)
{
    ModalState state;
    state.motion = profile.modalDefaults.motion;
    state.plane = profile.modalDefaults.plane;
    state.distanceMode = profile.modalDefaults.distanceMode;
    state.arcCenterMode = profile.modalDefaults.arcCenterMode;
    state.feedMode = profile.modalDefaults.feedMode;
    state.cutterComp = profile.modalDefaults.cutterComp;
    state.toolLengthComp = profile.modalDefaults.toolLengthComp;
    state.fixedCycle = profile.modalDefaults.fixedCycle;
    return state;
}

Diagnostic makeDiagnostic(DiagnosticSeverity severity,
                          int line,
                          int column,
                          int length,
                          const QString &code,
                          const QString &message)
{
    Diagnostic diagnostic;
    diagnostic.severity = severity;
    diagnostic.line = line;
    diagnostic.column = column;
    diagnostic.length = length;
    diagnostic.code = code;
    diagnostic.message = message;
    return diagnostic;
}

DiagnosticSeverity resolvedSeverity(const DialectProfile &profile,
                                    const QString &code,
                                    DiagnosticSeverity defaultSeverity)
{
    for (const DiagnosticSeverityOverride &severityOverride : profile.rules.severityOverrides) {
        if (severityOverride.code == code) {
            return severityOverride.severity;
        }
    }
    return defaultSeverity;
}

QString normalizeCode(const Word &word)
{
    if (!word.hasNumericValue) {
        return word.address.toUpper() + word.value.trimmed().toUpper();
    }

    const double value = word.numericValue;
    const double rounded = std::round(value);
    if (std::abs(value - rounded) < 0.000001) {
        return QStringLiteral("%1%2")
            .arg(word.address.toUpper())
            .arg(static_cast<int>(rounded), 2, 10, QLatin1Char('0'));
    }

    QString number = QString::number(value, 'f', 4);
    while (number.contains(QLatin1Char('.')) && number.endsWith(QLatin1Char('0'))) {
        number.chop(1);
    }
    if (number.endsWith(QLatin1Char('.'))) {
        number.chop(1);
    }
    return word.address.toUpper() + number;
}

bool isAddress(QChar ch)
{
    const ushort u = ch.toUpper().unicode();
    return u >= 'A' && u <= 'Z';
}

bool isValueTerminator(QChar ch)
{
    return ch.isSpace() || ch == QLatin1Char(';') || ch == QLatin1Char('(') || ch == QLatin1Char(')');
}

bool parseNumber(const QString &value, double *number)
{
    bool ok = false;
    const double parsed = value.toDouble(&ok);
    if (ok && number) {
        *number = parsed;
    }
    return ok;
}

bool startsWithKeyword(const QString &text, const QString &keyword)
{
    if (!text.startsWith(keyword)) {
        return false;
    }
    if (text.length() == keyword.length()) {
        return true;
    }
    const QChar next = text.at(keyword.length());
    return next.isSpace()
        || next == QLatin1Char('[')
        || next == QLatin1Char(';')
        || next.isDigit();
}

bool isMacroControlBlock(const QString &line)
{
    QString trimmed = line.trimmed().toUpper();
    if (trimmed.startsWith(QLatin1Char('#'))) {
        return true;
    }

    if (trimmed.startsWith(QLatin1Char('N'))) {
        int i = 1;
        while (i < trimmed.length() && trimmed.at(i).isSpace()) {
            ++i;
        }
        while (i < trimmed.length() && trimmed.at(i).isDigit()) {
            ++i;
        }
        while (i < trimmed.length() && trimmed.at(i).isSpace()) {
            ++i;
        }
        trimmed = trimmed.mid(i);
    }

    return startsWithKeyword(trimmed, QStringLiteral("IF"))
        || startsWithKeyword(trimmed, QStringLiteral("WHILE"))
        || startsWithKeyword(trimmed, QStringLiteral("GOTO"))
        || startsWithKeyword(trimmed, QStringLiteral("DO"))
        || startsWithKeyword(trimmed, QStringLiteral("END"));
}

bool hasAddress(const Block &block, QChar address)
{
    const QChar normalized = address.toUpper();
    for (const Word &word : block.words) {
        if (word.address.toUpper() == normalized) {
            return true;
        }
    }
    return false;
}

const Word *firstWord(const Block &block, QChar address)
{
    const QChar normalized = address.toUpper();
    for (const Word &word : block.words) {
        if (word.address.toUpper() == normalized) {
            return &word;
        }
    }
    return nullptr;
}

bool allowsModalMotion(const DialectProfile &profile, const QVector<QString> &gCodes)
{
    for (const QString &code : gCodes) {
        const int group = gCodeGroup(profile, code);
        if (group < 0) {
            continue;
        }

        if (!profile.modalMotionAllowedGroups.contains(group)) {
            return false;
        }
    }
    return true;
}

void addDiagnostic(Block *block,
                   DiagnosticSeverity severity,
                   int column,
                   int length,
                   const QString &code,
                   const QString &message)
{
    block->diagnostics.push_back(makeDiagnostic(severity, block->line, column, length, code, message));
}

void addProfileDiagnostic(Block *block,
                          const DialectProfile &profile,
                          DiagnosticSeverity defaultSeverity,
                          int column,
                          int length,
                          const QString &code,
                          const QString &message)
{
    addDiagnostic(block, resolvedSeverity(profile, code, defaultSeverity), column, length, code, message);
}

Block parseBlock(int lineNumber, const QString &line)
{
    Block block;
    block.line = lineNumber;
    block.text = line;
    if (isMacroControlBlock(line)) {
        block.hasMacroStatement = true;
        return block;
    }

    int i = 0;
    int bracketDepth = 0;
    while (i < line.length()) {
        const QChar ch = line.at(i);
        if (ch == QLatin1Char(';')) {
            break;
        }
        if (ch.isSpace()) {
            ++i;
            continue;
        }
        if (ch == QLatin1Char('(')) {
            const int start = i;
            ++i;
            QString comment;
            bool closed = false;
            while (i < line.length()) {
                if (line.at(i) == QLatin1Char(')')) {
                    closed = true;
                    ++i;
                    break;
                }
                comment.append(line.at(i));
                ++i;
            }
            block.comments.push_back(comment);
            if (!closed) {
                addDiagnostic(&block,
                              DiagnosticSeverity::Error,
                              start + 1,
                              line.length() - start,
                              QStringLiteral("NC_UNCLOSED_COMMENT"),
                              QStringLiteral("Comment parenthesis is not closed."));
            }
            continue;
        }
        if (ch == QLatin1Char('#')) {
            block.hasMacroStatement = true;
            ++i;
            continue;
        }
        if (!isAddress(ch)) {
            addDiagnostic(&block,
                          DiagnosticSeverity::Error,
                          i + 1,
                          1,
                          QStringLiteral("NC_UNEXPECTED_CHAR"),
                          QStringLiteral("Unexpected character in NC block."));
            ++i;
            continue;
        }

        const int start = i;
        Word word;
        word.address = ch.toUpper();
        word.column = start + 1;
        ++i;

        if (i >= line.length() || isValueTerminator(line.at(i))) {
            addDiagnostic(&block,
                          DiagnosticSeverity::Error,
                          word.column,
                          1,
                          QStringLiteral("NC_WORD_MISSING_VALUE"),
                          QStringLiteral("Address word is missing a value or expression."));
            word.length = 1;
            block.words.push_back(word);
            continue;
        }

        int valueStart = i;
        if (line.at(i) == QLatin1Char('[') || line.at(i) == QLatin1Char('#')) {
            word.expressionValue = true;
            if (line.at(i) == QLatin1Char('[')) {
                bracketDepth = 1;
                ++i;
                while (i < line.length() && bracketDepth > 0) {
                    if (line.at(i) == QLatin1Char('[')) {
                        ++bracketDepth;
                    } else if (line.at(i) == QLatin1Char(']')) {
                        --bracketDepth;
                    }
                    ++i;
                }
                if (bracketDepth != 0) {
                    addDiagnostic(&block,
                                  DiagnosticSeverity::Error,
                                  valueStart + 1,
                                  line.length() - valueStart,
                                  QStringLiteral("NC_UNCLOSED_EXPRESSION"),
                                  QStringLiteral("Macro expression bracket is not closed."));
                }
            } else {
                while (i < line.length() && !isValueTerminator(line.at(i))) {
                    ++i;
                }
            }
        } else {
            while (i < line.length() && !isValueTerminator(line.at(i))) {
                if (line.at(i) == QLatin1Char('[')) {
                    word.expressionValue = true;
                    bracketDepth = 1;
                    ++i;
                    while (i < line.length() && bracketDepth > 0) {
                        if (line.at(i) == QLatin1Char('[')) {
                            ++bracketDepth;
                        } else if (line.at(i) == QLatin1Char(']')) {
                            --bracketDepth;
                        }
                        ++i;
                    }
                    if (bracketDepth != 0) {
                        addDiagnostic(&block,
                                      DiagnosticSeverity::Error,
                                      valueStart + 1,
                                      line.length() - valueStart,
                                      QStringLiteral("NC_UNCLOSED_EXPRESSION"),
                                      QStringLiteral("Macro expression bracket is not closed."));
                    }
                    break;
                }
                ++i;
            }
        }

        word.value = line.mid(valueStart, i - valueStart).trimmed();
        word.length = i - start;
        if (!word.expressionValue) {
            double numeric = 0.0;
            if (parseNumber(word.value, &numeric)) {
                word.hasNumericValue = true;
                word.numericValue = numeric;
            } else {
                addDiagnostic(&block,
                              DiagnosticSeverity::Error,
                              word.column,
                              word.length,
                              QStringLiteral("NC_INVALID_NUMBER"),
                              QStringLiteral("Address word has an invalid numeric value."));
            }
        }
        block.words.push_back(word);
    }

    const QString upperLine = line.toUpper();
    if (upperLine.contains(QStringLiteral("IF"))
        || upperLine.contains(QStringLiteral("GOTO"))
        || upperLine.contains(QStringLiteral("WHILE"))
        || upperLine.contains(QStringLiteral("DO"))
        || upperLine.contains(QStringLiteral("END"))) {
        block.hasMacroStatement = true;
    }

    return block;
}

void updateModalState(const DialectProfile &profile, const QVector<QString> &gCodes, ModalState *state)
{
    for (const QString &code : gCodes) {
        if (code == QStringLiteral("G00")
            || code == QStringLiteral("G01")
            || code == QStringLiteral("G02")
            || code == QStringLiteral("G03")
            || code == QStringLiteral("G02.4")
            || code == QStringLiteral("G03.4")) {
            state->motion = code;
        } else if (code == QStringLiteral("G17")
                   || code == QStringLiteral("G18")
                   || code == QStringLiteral("G19")) {
            state->plane = code;
        } else if (code == QStringLiteral("G90") || code == QStringLiteral("G91")) {
            state->distanceMode = code;
        } else if (code == QStringLiteral("G90.1") || code == QStringLiteral("G91.1")) {
            state->arcCenterMode = code;
        } else if (code == QStringLiteral("G94") || code == QStringLiteral("G95")) {
            state->feedMode = code;
        } else if (code == QStringLiteral("G40") || code == QStringLiteral("G41") || code == QStringLiteral("G42")) {
            state->cutterComp = code;
        } else if (code == QStringLiteral("G43")
                   || code == QStringLiteral("G44")
                   || code == QStringLiteral("G49")) {
            state->toolLengthComp = code;
        } else if (gCodeGroup(profile, code) == 9) {
            state->fixedCycle = code;
        }
    }
}

void analyzeBlock(Block *block, const ParserOptions &options, ModalState *state)
{
    const DialectProfile &profile = *activeProfile(options);
    QHash<QChar, int> addressCounts;
    QVector<QString> gCodes;
    QVector<QString> mCodes;
    QHash<int, Word> modalGroupWords;

    for (const Word &word : block->words) {
        addressCounts[word.address.toUpper()] += 1;
        if (word.address == QLatin1Char('G')) {
            const QString code = normalizeCode(word);
            gCodes.push_back(code);
            const int group = gCodeGroup(profile, code);
            if (group < 0) {
                if (options.checkUnknownGCodes && !word.expressionValue) {
                    addProfileDiagnostic(block, profile,
                                  DiagnosticSeverity::Error,
                                  word.column,
                                  word.length,
                                  QStringLiteral("NC_UNKNOWN_G_CODE"),
                                  QStringLiteral("G code is not defined by the active profile."));
                }
            } else if (group != 0) {
                if (modalGroupWords.contains(group)) {
                    addProfileDiagnostic(block, profile,
                                  DiagnosticSeverity::Error,
                                  word.column,
                                  word.length,
                                  QStringLiteral("NC_MODAL_G_CONFLICT"),
                                  QStringLiteral("Conflicting modal G codes appear in the same block."));
                } else {
                    modalGroupWords.insert(group, word);
                }
            }
            if (options.warnUnsupportedG20 && code == QStringLiteral("G20") && !profile.rules.units.supportsG20) {
                addProfileDiagnostic(block, profile,
                              DiagnosticSeverity::Error,
                              word.column,
                              word.length,
                              QStringLiteral("NC_G20_UNSUPPORTED"),
                              QStringLiteral("The active profile does not support G20 inch units."));
            }
        } else if (word.address == QLatin1Char('M')) {
            const QString code = normalizeCode(word);
            mCodes.push_back(code);
            if (options.checkUnknownMCodes && !containsCode(profile.mCodes, code) && !word.expressionValue) {
                addProfileDiagnostic(block, profile,
                              DiagnosticSeverity::Warning,
                              word.column,
                              word.length,
                              QStringLiteral("NC_UNKNOWN_M_CODE"),
                              QStringLiteral("M code is not defined by the active profile."));
            }
        }
    }

    for (auto it = addressCounts.cbegin(); it != addressCounts.cend(); ++it) {
        const QChar address = it.key();
        if (it.value() <= 1 || address == QLatin1Char('G') || address == QLatin1Char('M')) {
            continue;
        }
        const Word *word = firstWord(*block, address);
        addProfileDiagnostic(block, profile,
                      DiagnosticSeverity::Warning,
                      word ? word->column : 1,
                      word ? word->length : 1,
                      QStringLiteral("NC_DUPLICATE_ADDRESS"),
                      QStringLiteral("Duplicate address word appears in the same block."));
    }

    const int maxMCodeCount = resolvedMaxMCodeCount(options, profile);
    if (maxMCodeCount > 0 && mCodes.size() > maxMCodeCount) {
        const Word *word = firstWord(*block, QLatin1Char('M'));
        addProfileDiagnostic(block, profile,
                      DiagnosticSeverity::Warning,
                      word ? word->column : 1,
                      word ? word->length : 1,
                      QStringLiteral("NC_TOO_MANY_M_CODES"),
                      QStringLiteral("Too many M codes appear in one block."));
    }

    const Word *feed = firstWord(*block, QLatin1Char('F'));
    if (profile.rules.programs.checkNonPositiveFeed && feed && feed->hasNumericValue && feed->numericValue <= 0.0) {
        addProfileDiagnostic(block, profile,
                      DiagnosticSeverity::Warning,
                      feed->column,
                      feed->length,
                      QStringLiteral("NC_NON_POSITIVE_FEED"),
                      QStringLiteral("Feed rate should normally be positive."));
    }

    if (profile.rules.compensation.requireHForToolLengthComp
        && (gCodes.contains(QStringLiteral("G43")) || gCodes.contains(QStringLiteral("G44")))
        && !hasAddress(*block, QLatin1Char('H'))) {
        const Word *word = firstWord(*block, QLatin1Char('G'));
        addProfileDiagnostic(block, profile,
                      DiagnosticSeverity::Warning,
                      word ? word->column : 1,
                      word ? word->length : 1,
                      QStringLiteral("NC_TOOL_LENGTH_MISSING_H"),
                      QStringLiteral("G43/G44 usually requires an H compensation number."));
    }

    if (profile.rules.compensation.requireDForCutterComp
        && (gCodes.contains(QStringLiteral("G41")) || gCodes.contains(QStringLiteral("G42")))
        && !hasAddress(*block, QLatin1Char('D'))) {
        const Word *word = firstWord(*block, QLatin1Char('G'));
        addProfileDiagnostic(block, profile,
                      DiagnosticSeverity::Warning,
                      word ? word->column : 1,
                      word ? word->length : 1,
                      QStringLiteral("NC_CUTTER_COMP_MISSING_D"),
                      QStringLiteral("G41/G42 usually requires a D compensation number."));
    }

    if (profile.rules.transforms.checkG51ScaleConflict
        && gCodes.contains(QStringLiteral("G51"))
        && hasAddress(*block, QLatin1Char('P'))
        && (hasAddress(*block, QLatin1Char('I'))
            || hasAddress(*block, QLatin1Char('J'))
            || hasAddress(*block, QLatin1Char('K')))) {
        const Word *word = firstWord(*block, QLatin1Char('P'));
        addProfileDiagnostic(block, profile,
                      DiagnosticSeverity::Error,
                      word ? word->column : 1,
                      word ? word->length : 1,
                      QStringLiteral("NC_G51_SCALE_CONFLICT"),
                      QStringLiteral("G51 cannot combine P scale with I/J/K scale values."));
    }

    if (profile.rules.transforms.requireRForG68
        && gCodes.contains(QStringLiteral("G68"))
        && !hasAddress(*block, QLatin1Char('R'))) {
        const Word *word = firstWord(*block, QLatin1Char('G'));
        addProfileDiagnostic(block, profile,
                      DiagnosticSeverity::Error,
                      word ? word->column : 1,
                      word ? word->length : 1,
                      QStringLiteral("NC_G68_MISSING_R"),
                      QStringLiteral("G68 coordinate rotation requires an R angle."));
    }

    if (profile.rules.toolSpindle.requireTForM06
        && mCodes.contains(QStringLiteral("M06"))
        && !hasAddress(*block, QLatin1Char('T'))
        && !state->hasSelectedTool) {
        const Word *word = firstWord(*block, QLatin1Char('M'));
        addProfileDiagnostic(block, profile,
                      DiagnosticSeverity::Warning,
                      word ? word->column : 1,
                      word ? word->length : 1,
                      QStringLiteral("NC_M06_MISSING_T"),
                      QStringLiteral("M06 should have a T tool number in this or a previous block."));
    }

    if (profile.rules.toolSpindle.requireSForM03
        && mCodes.contains(QStringLiteral("M03"))
        && !hasAddress(*block, QLatin1Char('S'))
        && !state->hasSpindleSpeed) {
        const Word *word = firstWord(*block, QLatin1Char('M'));
        addProfileDiagnostic(block, profile,
                      DiagnosticSeverity::Warning,
                      word ? word->column : 1,
                      word ? word->length : 1,
                      QStringLiteral("NC_M03_MISSING_S"),
                      QStringLiteral("M03 should normally have an S spindle speed."));
    }

    const bool explicitArc = gCodes.contains(QStringLiteral("G02")) || gCodes.contains(QStringLiteral("G03"));
    const bool modalArcMove = !explicitArc
        && allowsModalMotion(profile, gCodes)
        && (state->motion == QStringLiteral("G02") || state->motion == QStringLiteral("G03"))
        && (hasAddress(*block, QLatin1Char('X'))
            || hasAddress(*block, QLatin1Char('Y'))
            || hasAddress(*block, QLatin1Char('Z'))
            || hasAddress(*block, QLatin1Char('I'))
            || hasAddress(*block, QLatin1Char('J'))
            || hasAddress(*block, QLatin1Char('K'))
            || hasAddress(*block, QLatin1Char('R')));
    if (explicitArc || modalArcMove) {
        const bool hasR = hasAddress(*block, QLatin1Char('R'));
        const bool hasI = hasAddress(*block, QLatin1Char('I'));
        const bool hasJ = hasAddress(*block, QLatin1Char('J'));
        const bool hasK = hasAddress(*block, QLatin1Char('K'));
        if (!hasR && !hasI && !hasJ && !hasK) {
            addProfileDiagnostic(block, profile,
                          DiagnosticSeverity::Error,
                          1,
                          qMax(1, block->text.length()),
                          QStringLiteral("NC_ARC_MISSING_CENTER"),
                          QStringLiteral("Arc interpolation requires R or I/J/K center parameters."));
        }
        if (hasR && (hasI || hasJ || hasK)) {
            const Word *word = firstWord(*block, QLatin1Char('R'));
            addProfileDiagnostic(block, profile,
                          DiagnosticSeverity::Info,
                          word ? word->column : 1,
                          word ? word->length : 1,
                          QStringLiteral("NC_ARC_R_PRECEDENCE"),
                          QStringLiteral("When R and I/J/K are both set, the profile uses R."));
        }

        bool hasPlaneEnd = false;
        QString plane = state->plane;
        if (gCodes.contains(QStringLiteral("G17"))) {
            plane = QStringLiteral("G17");
        } else if (gCodes.contains(QStringLiteral("G18"))) {
            plane = QStringLiteral("G18");
        } else if (gCodes.contains(QStringLiteral("G19"))) {
            plane = QStringLiteral("G19");
        }
        if (plane == QStringLiteral("G17")) {
            hasPlaneEnd = hasAddress(*block, QLatin1Char('X')) || hasAddress(*block, QLatin1Char('Y'));
        } else if (plane == QStringLiteral("G18")) {
            hasPlaneEnd = hasAddress(*block, QLatin1Char('X')) || hasAddress(*block, QLatin1Char('Z'));
        } else if (plane == QStringLiteral("G19")) {
            hasPlaneEnd = hasAddress(*block, QLatin1Char('Y')) || hasAddress(*block, QLatin1Char('Z'));
        }
        if (hasR && !hasPlaneEnd) {
            const Word *word = firstWord(*block, QLatin1Char('R'));
            addProfileDiagnostic(block, profile,
                          DiagnosticSeverity::Error,
                          word ? word->column : 1,
                          word ? word->length : 1,
                          QStringLiteral("NC_FULL_CIRCLE_WITH_R"),
                          QStringLiteral("Full-circle machining should use I/J/K instead of R."));
        }
    }

    if (gCodes.contains(QStringLiteral("G04"))) {
        if (hasAddress(*block, QLatin1Char('X')) && hasAddress(*block, QLatin1Char('P'))) {
            addProfileDiagnostic(block, profile,
                          DiagnosticSeverity::Error,
                          1,
                          qMax(1, block->text.length()),
                          QStringLiteral("NC_G04_X_P_CONFLICT"),
                          QStringLiteral("G04 dwell cannot use X and P at the same time."));
        }
        const Word *x = firstWord(*block, QLatin1Char('X'));
        const Word *p = firstWord(*block, QLatin1Char('P'));
        if ((x && x->hasNumericValue && x->numericValue < 0.0) || (p && p->hasNumericValue && p->numericValue < 0.0)) {
            addProfileDiagnostic(block, profile,
                          DiagnosticSeverity::Error,
                          x ? x->column : p->column,
                          x ? x->length : p->length,
                          QStringLiteral("NC_G04_NEGATIVE_TIME"),
                          QStringLiteral("G04 dwell time cannot be negative."));
        }
    }

    for (const QString &code : gCodes) {
        if (!containsCode(profile.rules.cycles.fixedCyclesRequiringZrf, code)) {
            continue;
        }
        if (!hasAddress(*block, QLatin1Char('Z'))
            || !hasAddress(*block, QLatin1Char('R'))
            || !hasAddress(*block, QLatin1Char('F'))) {
            addProfileDiagnostic(block, profile,
                          DiagnosticSeverity::Warning,
                          1,
                          qMax(1, block->text.length()),
                          QStringLiteral("NC_FIXED_CYCLE_MISSING_ARGUMENT"),
                          QStringLiteral("Fixed cycle is missing one of Z, R, or F."));
        }
    }

    const Word *l = firstWord(*block, QLatin1Char('L'));
    if ((mCodes.contains(QStringLiteral("M98")) || gCodes.contains(QStringLiteral("G65"))) && l && l->hasNumericValue) {
        if (l->numericValue < 1.0 || l->numericValue > 1000.0) {
            addProfileDiagnostic(block, profile,
                          DiagnosticSeverity::Error,
                          l->column,
                          l->length,
                          QStringLiteral("NC_REPEAT_OUT_OF_RANGE"),
                          QStringLiteral("Subprogram repeat count L must be in range 1 to 1000."));
        }
    }

    if (profile.rules.cycles.pairG80_1WithG81_1
        && gCodes.contains(QStringLiteral("G80.1"))
        && !state->highSpeedCycleActive) {
        const Word *word = firstWord(*block, QLatin1Char('G'));
        addProfileDiagnostic(block, profile,
                      DiagnosticSeverity::Warning,
                      word ? word->column : 1,
                      word ? word->length : 1,
                      QStringLiteral("NC_G80_1_WITHOUT_G81_1"),
                      QStringLiteral("G80.1 should pair with an active G81.1 high-speed cycle."));
    }

    const Word *z = firstWord(*block, QLatin1Char('Z'));
    const Word *r = firstWord(*block, QLatin1Char('R'));
    if (profile.rules.cycles.checkG86ZGreaterThanR
        && gCodes.contains(QStringLiteral("G86"))
        && z && r
        && z->hasNumericValue
        && r->hasNumericValue
        && z->numericValue > r->numericValue) {
        addProfileDiagnostic(block, profile,
                      DiagnosticSeverity::Error,
                      z->column,
                      z->length,
                      QStringLiteral("NC_G86_Z_GREATER_THAN_R"),
                      QStringLiteral("G86 Z value must not be greater than R value."));
    }

    if (hasAddress(*block, QLatin1Char('S'))) {
        state->hasSpindleSpeed = true;
    }
    const bool hasToolInBlock = hasAddress(*block, QLatin1Char('T'));
    if (mCodes.contains(QStringLiteral("M06"))) {
        state->hasSelectedTool = false;
    } else if (hasToolInBlock) {
        state->hasSelectedTool = true;
    }
    if (gCodes.contains(QStringLiteral("G81.1"))) {
        state->highSpeedCycleActive = true;
    }
    if (gCodes.contains(QStringLiteral("G80.1"))) {
        state->highSpeedCycleActive = false;
    }

    updateModalState(profile, gCodes, state);
}

void appendBlock(ParseResult *result, Block block)
{
    for (const Diagnostic &diagnostic : block.diagnostics) {
        result->diagnostics.push_back(diagnostic);
    }
    result->blocks.push_back(block);
}

} // namespace

Parser::Parser(ParserOptions options)
    : m_options(options)
{
}

ParseResult Parser::parseText(const QString &text) const
{
    QStringList lines = text.split(QLatin1Char('\n'));
    if (!lines.isEmpty() && lines.last().isEmpty() && text.endsWith(QLatin1Char('\n'))) {
        lines.removeLast();
    }

    return parseLines(lines.size(), [&lines](int zeroBasedLine) {
        QString line = lines.at(zeroBasedLine);
        if (line.endsWith(QLatin1Char('\r'))) {
            line.chop(1);
        }
        return line;
    });
}

ParseResult Parser::parseLines(int lineCount, const LineReader &lineReader) const
{
    ParseResult result;
    const DialectProfile &profile = *activeProfile(m_options);
    ModalState state = makeInitialModalState(profile);
    result.blocks.reserve(qMax(0, lineCount));

    for (int i = 0; i < lineCount; ++i) {
        Block block = parseBlock(i + 1, lineReader ? lineReader(i) : QString());
        analyzeBlock(&block, m_options, &state);
        appendBlock(&result, block);
    }

    return result;
}

} // namespace nc
