#include "nc/ncparser.h"
#include "nc/ncdialects.h"
#include "nc/ncdiagnosticmessages.h"

#include <QCoreApplication>
#include <QSet>
#include <QStringList>
#include <QTextStream>

namespace {

struct TestContext {
    int failed = 0;
    QTextStream out{stdout};
    QTextStream err{stderr};
};

QSet<QString> diagnosticCodes(const nc::ParseResult &result)
{
    QSet<QString> codes;
    for (const nc::Diagnostic &diagnostic : result.diagnostics) {
        codes.insert(diagnostic.code);
    }
    return codes;
}

QString severityName(nc::DiagnosticSeverity severity)
{
    switch (severity) {
    case nc::DiagnosticSeverity::Info:
        return QStringLiteral("Info");
    case nc::DiagnosticSeverity::Warning:
        return QStringLiteral("Warning");
    case nc::DiagnosticSeverity::Error:
        return QStringLiteral("Error");
    }
    return QStringLiteral("Unknown");
}

nc::ParseResult parseText(const QString &text, const nc::ParserOptions &options = nc::ParserOptions())
{
    nc::Parser parser(options);
    return parser.parseText(text);
}

void expectContains(TestContext *ctx,
                    const QString &name,
                    const nc::ParseResult &result,
                    const QStringList &expectedCodes)
{
    const QSet<QString> codes = diagnosticCodes(result);
    QStringList missing;
    for (const QString &code : expectedCodes) {
        if (!codes.contains(code)) {
            missing.push_back(code);
        }
    }

    if (missing.isEmpty()) {
        ctx->out << "PASS " << name << Qt::endl;
        return;
    }

    ++ctx->failed;
    ctx->err << "FAIL " << name << ": missing " << missing.join(QStringLiteral(", "))
             << "; actual " << QStringList(codes.begin(), codes.end()).join(QStringLiteral(", "))
             << Qt::endl;
}

void expectAbsent(TestContext *ctx,
                  const QString &name,
                  const nc::ParseResult &result,
                  const QStringList &forbiddenCodes)
{
    const QSet<QString> codes = diagnosticCodes(result);
    QStringList present;
    for (const QString &code : forbiddenCodes) {
        if (codes.contains(code)) {
            present.push_back(code);
        }
    }

    if (present.isEmpty()) {
        ctx->out << "PASS " << name << Qt::endl;
        return;
    }

    ++ctx->failed;
    ctx->err << "FAIL " << name << ": unexpected " << present.join(QStringLiteral(", "))
             << "; actual " << QStringList(codes.begin(), codes.end()).join(QStringLiteral(", "))
             << Qt::endl;
}

void expectSeverity(TestContext *ctx,
                    const QString &name,
                    const nc::ParseResult &result,
                    const QString &code,
                    nc::DiagnosticSeverity expectedSeverity)
{
    for (const nc::Diagnostic &diagnostic : result.diagnostics) {
        if (diagnostic.code != code) {
            continue;
        }
        if (diagnostic.severity == expectedSeverity) {
            ctx->out << "PASS " << name << Qt::endl;
            return;
        }

        ++ctx->failed;
        ctx->err << "FAIL " << name << ": " << code << " severity was "
                 << severityName(diagnostic.severity) << ", expected "
                 << severityName(expectedSeverity) << Qt::endl;
        return;
    }

    ++ctx->failed;
    ctx->err << "FAIL " << name << ": missing " << code << Qt::endl;
}

void runDefaultProfileCases(TestContext *ctx)
{
    expectContains(ctx,
                   QStringLiteral("modal G conflict"),
                   parseText(QStringLiteral("G17 G18")),
                   {QStringLiteral("NC_MODAL_G_CONFLICT")});

    expectContains(ctx,
                   QStringLiteral("G20 unsupported"),
                   parseText(QStringLiteral("G20")),
                   {QStringLiteral("NC_G20_UNSUPPORTED")});

    expectContains(ctx,
                   QStringLiteral("G04 negative time"),
                   parseText(QStringLiteral("G04 X-1")),
                   {QStringLiteral("NC_G04_NEGATIVE_TIME")});

    expectContains(ctx,
                   QStringLiteral("G04 X/P conflict"),
                   parseText(QStringLiteral("G04 X1 P2")),
                   {QStringLiteral("NC_G04_X_P_CONFLICT")});

    expectContains(ctx,
                   QStringLiteral("arc missing center"),
                   parseText(QStringLiteral("G02 X10")),
                   {QStringLiteral("NC_ARC_MISSING_CENTER")});

    expectContains(ctx,
                   QStringLiteral("arc R precedence"),
                   parseText(QStringLiteral("G02 X10 R5 I1")),
                   {QStringLiteral("NC_ARC_R_PRECEDENCE")});

    expectContains(ctx,
                   QStringLiteral("M03 missing S"),
                   parseText(QStringLiteral("M03")),
                   {QStringLiteral("NC_M03_MISSING_S")});

    expectContains(ctx,
                   QStringLiteral("M06 missing T"),
                   parseText(QStringLiteral("M06")),
                   {QStringLiteral("NC_M06_MISSING_T")});

    expectContains(ctx,
                   QStringLiteral("G43 missing H"),
                   parseText(QStringLiteral("G43")),
                   {QStringLiteral("NC_TOOL_LENGTH_MISSING_H")});

    expectContains(ctx,
                   QStringLiteral("G41 missing D"),
                   parseText(QStringLiteral("G41")),
                   {QStringLiteral("NC_CUTTER_COMP_MISSING_D")});
}

void runStateCarryCases(TestContext *ctx)
{
    expectAbsent(ctx,
                 QStringLiteral("previous S satisfies M03"),
                 parseText(QStringLiteral("S1200\nM03")),
                 {QStringLiteral("NC_M03_MISSING_S")});

    expectAbsent(ctx,
                 QStringLiteral("previous T satisfies M06"),
                 parseText(QStringLiteral("T01\nM06")),
                 {QStringLiteral("NC_M06_MISSING_T")});
}

void runCycleAndProgramCases(TestContext *ctx)
{
    expectContains(ctx,
                   QStringLiteral("fixed cycle missing R"),
                   parseText(QStringLiteral("G81 X0 Z-5 F100")),
                   {QStringLiteral("NC_FIXED_CYCLE_MISSING_ARGUMENT")});

    expectAbsent(ctx,
                 QStringLiteral("fixed cycle complete arguments"),
                 parseText(QStringLiteral("G81 X0 Z-5 R2 F100")),
                 {QStringLiteral("NC_FIXED_CYCLE_MISSING_ARGUMENT")});

    expectContains(ctx,
                   QStringLiteral("M98 repeat count range"),
                   parseText(QStringLiteral("M98 P100 L0")),
                   {QStringLiteral("NC_REPEAT_OUT_OF_RANGE")});

    expectContains(ctx,
                   QStringLiteral("G65 repeat count range"),
                   parseText(QStringLiteral("G65 P9000 L1001")),
                   {QStringLiteral("NC_REPEAT_OUT_OF_RANGE")});

    expectContains(ctx,
                   QStringLiteral("G80.1 without G81.1"),
                   parseText(QStringLiteral("G80.1")),
                   {QStringLiteral("NC_G80_1_WITHOUT_G81_1")});

    expectAbsent(ctx,
                 QStringLiteral("G80.1 after G81.1 pair"),
                 parseText(QStringLiteral("G81.1 Z-5 R2 F100\nG80.1")),
                 {QStringLiteral("NC_G80_1_WITHOUT_G81_1")});

    expectContains(ctx,
                   QStringLiteral("G86 Z greater than R"),
                   parseText(QStringLiteral("G86 Z2 R1 F100")),
                   {QStringLiteral("NC_G86_Z_GREATER_THAN_R")});
}

void runDialectRuleCases(TestContext *ctx)
{
    expectContains(ctx,
                   QStringLiteral("G51 scale conflict"),
                   parseText(QStringLiteral("G51 P2 I1")),
                   {QStringLiteral("NC_G51_SCALE_CONFLICT")});

    expectContains(ctx,
                   QStringLiteral("G68 missing R"),
                   parseText(QStringLiteral("G68 X0 Y0")),
                   {QStringLiteral("NC_G68_MISSING_R")});

    expectContains(ctx,
                   QStringLiteral("too many M codes"),
                   parseText(QStringLiteral("S1000 M00 M01 M02 M03")),
                   {QStringLiteral("NC_TOO_MANY_M_CODES")});

    expectContains(ctx,
                   QStringLiteral("non-positive feed"),
                   parseText(QStringLiteral("G01 X1 F0")),
                   {QStringLiteral("NC_NON_POSITIVE_FEED")});
}

void runProfileOverrideCases(TestContext *ctx)
{
    nc::DialectProfile g20Profile = nc::makeLynucEdmProfile();
    g20Profile.rules.units.supportsG20 = true;

    nc::ParserOptions g20Options;
    g20Options.profile = &g20Profile;
    expectAbsent(ctx,
                 QStringLiteral("profile can enable G20"),
                 parseText(QStringLiteral("G20"), g20Options),
                 {QStringLiteral("NC_G20_UNSUPPORTED")});

    nc::DialectProfile groupProfile = nc::makeLynucEdmProfile();
    for (nc::GCodeDefinition &definition : groupProfile.gCodes) {
        if (definition.code == QStringLiteral("G18")) {
            definition.group = 99;
        }
    }

    nc::ParserOptions groupOptions;
    groupOptions.profile = &groupProfile;
    expectAbsent(ctx,
                 QStringLiteral("profile controls G groups"),
                 parseText(QStringLiteral("G17 G18"), groupOptions),
                 {QStringLiteral("NC_MODAL_G_CONFLICT")});

    nc::DialectProfile relaxedCompProfile = nc::makeLynucEdmProfile();
    relaxedCompProfile.rules.compensation.requireHForToolLengthComp = false;
    relaxedCompProfile.rules.compensation.requireDForCutterComp = false;

    nc::ParserOptions relaxedCompOptions;
    relaxedCompOptions.profile = &relaxedCompProfile;
    expectAbsent(ctx,
                 QStringLiteral("profile can relax compensation words"),
                 parseText(QStringLiteral("G43\nG41"), relaxedCompOptions),
                 {
                     QStringLiteral("NC_TOOL_LENGTH_MISSING_H"),
                     QStringLiteral("NC_CUTTER_COMP_MISSING_D")
                 });

    nc::DialectProfile fixedCycleProfile = nc::makeLynucEdmProfile();
    fixedCycleProfile.rules.cycles.fixedCyclesRequiringZrf.clear();

    nc::ParserOptions fixedCycleOptions;
    fixedCycleOptions.profile = &fixedCycleProfile;
    expectAbsent(ctx,
                 QStringLiteral("profile controls fixed cycle arguments"),
                 parseText(QStringLiteral("G81 X0"), fixedCycleOptions),
                 {QStringLiteral("NC_FIXED_CYCLE_MISSING_ARGUMENT")});

    nc::DialectProfile maxMProfile = nc::makeLynucEdmProfile();
    maxMProfile.rules.programs.maxMCodeCountPerBlock = 4;

    nc::ParserOptions maxMOptions;
    maxMOptions.profile = &maxMProfile;
    expectAbsent(ctx,
                 QStringLiteral("profile controls max M codes"),
                 parseText(QStringLiteral("S1000 M00 M01 M02 M03"), maxMOptions),
                 {QStringLiteral("NC_TOO_MANY_M_CODES")});

    nc::DialectProfile highSpeedProfile = nc::makeLynucEdmProfile();
    highSpeedProfile.rules.cycles.pairG80_1WithG81_1 = false;

    nc::ParserOptions highSpeedOptions;
    highSpeedOptions.profile = &highSpeedProfile;
    expectAbsent(ctx,
                 QStringLiteral("profile controls G80.1 pairing"),
                 parseText(QStringLiteral("G80.1"), highSpeedOptions),
                 {QStringLiteral("NC_G80_1_WITHOUT_G81_1")});

    nc::DialectProfile g86Profile = nc::makeLynucEdmProfile();
    g86Profile.rules.cycles.checkG86ZGreaterThanR = false;

    nc::ParserOptions g86Options;
    g86Options.profile = &g86Profile;
    expectAbsent(ctx,
                 QStringLiteral("profile controls G86 Z/R check"),
                 parseText(QStringLiteral("G86 Z2 R1 F100"), g86Options),
                 {QStringLiteral("NC_G86_Z_GREATER_THAN_R")});
}

void runSampleDialectCases(TestContext *ctx)
{
    nc::ParserOptions options;
    options.profile = &nc::sampleRelaxedProfile();

    const nc::ParseResult result = parseText(
        QStringLiteral("G20\nG43\nG41\nG81 X0\nG80.1\nG86 Z2 R1 F100\nG999\nM100"),
        options);
    expectAbsent(ctx,
                 QStringLiteral("sample relaxed dialect overrides"),
                 result,
                 {
                     QStringLiteral("NC_G20_UNSUPPORTED"),
                     QStringLiteral("NC_TOOL_LENGTH_MISSING_H"),
                     QStringLiteral("NC_CUTTER_COMP_MISSING_D"),
                     QStringLiteral("NC_FIXED_CYCLE_MISSING_ARGUMENT"),
                     QStringLiteral("NC_G80_1_WITHOUT_G81_1"),
                     QStringLiteral("NC_G86_Z_GREATER_THAN_R"),
                     QStringLiteral("NC_UNKNOWN_G_CODE"),
                     QStringLiteral("NC_UNKNOWN_M_CODE")
                 });

    const nc::ParseResult severityResult = parseText(QStringLiteral("G51 P2 I1\nM777"), options);
    expectSeverity(ctx,
                   QStringLiteral("sample dialect relaxes G51 severity"),
                   severityResult,
                   QStringLiteral("NC_G51_SCALE_CONFLICT"),
                   nc::DiagnosticSeverity::Warning);
    expectSeverity(ctx,
                   QStringLiteral("sample dialect lowers unknown M severity"),
                   severityResult,
                   QStringLiteral("NC_UNKNOWN_M_CODE"),
                   nc::DiagnosticSeverity::Info);
}

void runSeverityOverrideCases(TestContext *ctx)
{
    nc::DialectProfile profile = nc::makeLynucEdmProfile();
    profile.rules.severityOverrides = {
        {QStringLiteral("NC_G20_UNSUPPORTED"), nc::DiagnosticSeverity::Warning},
        {QStringLiteral("NC_FIXED_CYCLE_MISSING_ARGUMENT"), nc::DiagnosticSeverity::Error},
        {QStringLiteral("NC_ARC_R_PRECEDENCE"), nc::DiagnosticSeverity::Warning},
        {QStringLiteral("NC_INVALID_NUMBER"), nc::DiagnosticSeverity::Info}
    };

    nc::ParserOptions options;
    options.profile = &profile;
    const nc::ParseResult semanticResult = parseText(QStringLiteral("G20\nG81 X0\nG02 X10 R5 I1"), options);
    expectSeverity(ctx,
                   QStringLiteral("profile lowers G20 severity"),
                   semanticResult,
                   QStringLiteral("NC_G20_UNSUPPORTED"),
                   nc::DiagnosticSeverity::Warning);
    expectSeverity(ctx,
                   QStringLiteral("profile raises fixed cycle severity"),
                   semanticResult,
                   QStringLiteral("NC_FIXED_CYCLE_MISSING_ARGUMENT"),
                   nc::DiagnosticSeverity::Error);
    expectSeverity(ctx,
                   QStringLiteral("profile raises arc precedence severity"),
                   semanticResult,
                   QStringLiteral("NC_ARC_R_PRECEDENCE"),
                   nc::DiagnosticSeverity::Warning);

    const nc::ParseResult lexicalResult = parseText(QStringLiteral("XABC"), options);
    expectSeverity(ctx,
                   QStringLiteral("lexical diagnostics ignore severity overrides"),
                   lexicalResult,
                   QStringLiteral("NC_INVALID_NUMBER"),
                   nc::DiagnosticSeverity::Error);
}

void runMacroCompatibilityCases(TestContext *ctx)
{
    const nc::ParseResult result = parseText(QStringLiteral("#1=100\nN1 IF[#1 GT 0] GOTO 10\nG01 X#1"));
    expectAbsent(ctx,
                 QStringLiteral("macro values are accepted"),
                 result,
                 {
                     QStringLiteral("NC_INVALID_NUMBER"),
                     QStringLiteral("NC_UNEXPECTED_CHAR"),
                     QStringLiteral("NC_UNCLOSED_EXPRESSION")
                 });
}

void runDiagnosticMessageCases(TestContext *ctx)
{
    const QString chinese = nc::diagnosticMessage(QStringLiteral("NC_G20_UNSUPPORTED"));
    const QString english = nc::diagnosticMessage(QStringLiteral("NC_G20_UNSUPPORTED"),
                                                  nc::DiagnosticMessageLanguage::English);
    const QString unknown = nc::diagnosticMessage(QStringLiteral("NC_NOT_A_REAL_CODE"));

    if (!chinese.isEmpty()
        && chinese != english
        && english.contains(QStringLiteral("G20"))
        && unknown == QStringLiteral("NC_NOT_A_REAL_CODE")) {
        ctx->out << "PASS diagnostic message localization" << Qt::endl;
        return;
    }

    ++ctx->failed;
    ctx->err << "FAIL diagnostic message localization" << Qt::endl;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app)

    TestContext ctx;
    runDefaultProfileCases(&ctx);
    runStateCarryCases(&ctx);
    runCycleAndProgramCases(&ctx);
    runDialectRuleCases(&ctx);
    runProfileOverrideCases(&ctx);
    runSampleDialectCases(&ctx);
    runSeverityOverrideCases(&ctx);
    runMacroCompatibilityCases(&ctx);
    runDiagnosticMessageCases(&ctx);

    if (ctx.failed == 0) {
        ctx.out << "NC parser smoke tests passed." << Qt::endl;
        return 0;
    }

    ctx.err << ctx.failed << " NC parser smoke test(s) failed." << Qt::endl;
    return 1;
}
