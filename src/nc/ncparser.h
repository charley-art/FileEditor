#ifndef NCPARSER_H
#define NCPARSER_H

#include <functional>

#include <QChar>
#include <QString>
#include <QVector>

namespace nc {

enum class DiagnosticSeverity {
    Info,
    Warning,
    Error
};

struct Diagnostic {
    DiagnosticSeverity severity = DiagnosticSeverity::Info;
    int line = 0;
    int column = 0;
    int length = 0;
    QString code;
    QString message;
};

struct Word {
    QChar address;
    QString value;
    int column = 0;
    int length = 0;
    bool hasNumericValue = false;
    double numericValue = 0.0;
    bool expressionValue = false;
};

struct Block {
    int line = 0;
    QString text;
    QVector<Word> words;
    QVector<QString> comments;
    QVector<Diagnostic> diagnostics;
    bool hasMacroStatement = false;
};

struct ParseResult {
    QVector<Block> blocks;
    QVector<Diagnostic> diagnostics;
};

struct GCodeDefinition {
    QString code;
    int group = 0;
};

struct ModalDefaults {
    QString motion = QStringLiteral("G01");
    QString plane = QStringLiteral("G17");
    QString distanceMode = QStringLiteral("G90");
    QString arcCenterMode = QStringLiteral("G91.1");
    QString feedMode = QStringLiteral("G94");
    QString cutterComp = QStringLiteral("G40");
    QString toolLengthComp = QStringLiteral("G49");
    QString fixedCycle = QStringLiteral("G80");
};

struct UnitRuleSet {
    bool supportsG20 = false;
};

struct CompensationRuleSet {
    bool requireHForToolLengthComp = true;
    bool requireDForCutterComp = true;
};

struct TransformRuleSet {
    bool checkG51ScaleConflict = true;
    bool requireRForG68 = true;
};

struct ToolSpindleRuleSet {
    bool requireTForM06 = true;
    bool requireSForM03 = true;
};

struct CycleRuleSet {
    bool pairG80_1WithG81_1 = true;
    bool checkG86ZGreaterThanR = true;
    QVector<QString> fixedCyclesRequiringZrf;
};

struct ProgramRuleSet {
    bool checkNonPositiveFeed = true;
    int maxMCodeCountPerBlock = 3;
};

struct DiagnosticSeverityOverride {
    QString code;
    DiagnosticSeverity severity = DiagnosticSeverity::Warning;
};

struct DialectRuleSet {
    UnitRuleSet units;
    CompensationRuleSet compensation;
    TransformRuleSet transforms;
    ToolSpindleRuleSet toolSpindle;
    CycleRuleSet cycles;
    ProgramRuleSet programs;
    QVector<DiagnosticSeverityOverride> severityOverrides;
};

struct DialectProfile {
    QString name;
    ModalDefaults modalDefaults;
    QVector<GCodeDefinition> gCodes;
    QVector<QString> mCodes;
    QVector<int> modalMotionAllowedGroups;
    DialectRuleSet rules;
};

struct ParserOptions {
    const DialectProfile *profile = nullptr;
    bool checkUnknownGCodes = true;
    bool checkUnknownMCodes = true;
    bool warnUnsupportedG20 = true;
    int maxMCodeCountPerBlock = -1;
};

class Parser
{
public:
    using LineReader = std::function<QString(int zeroBasedLine)>;

    explicit Parser(ParserOptions options = ParserOptions());

    ParseResult parseText(const QString &text) const;
    ParseResult parseLines(int lineCount, const LineReader &lineReader) const;

private:
    ParserOptions m_options;
};

} // namespace nc

#endif // NCPARSER_H
