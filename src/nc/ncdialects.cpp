#include "ncdialects.h"

#include <QString>

namespace nc {

DialectProfile makeLynucEdmProfile()
{
    DialectProfile p;
    p.name = QStringLiteral("Lynuc/EDM");
    p.modalDefaults.motion = QStringLiteral("G01");
    p.modalDefaults.plane = QStringLiteral("G17");
    p.modalDefaults.distanceMode = QStringLiteral("G90");
    p.modalDefaults.arcCenterMode = QStringLiteral("G91.1");
    p.modalDefaults.feedMode = QStringLiteral("G94");
    p.modalDefaults.cutterComp = QStringLiteral("G40");
    p.modalDefaults.toolLengthComp = QStringLiteral("G49");
    p.modalDefaults.fixedCycle = QStringLiteral("G80");

    p.gCodes = {
        {QStringLiteral("G00"), 1}, {QStringLiteral("G01"), 1}, {QStringLiteral("G02"), 1},
        {QStringLiteral("G03"), 1}, {QStringLiteral("G02.4"), 1}, {QStringLiteral("G03.4"), 1},
        {QStringLiteral("G04"), 0}, {QStringLiteral("G05"), 0}, {QStringLiteral("G05.1"), 0},
        {QStringLiteral("G07.1"), 0}, {QStringLiteral("G09"), 0}, {QStringLiteral("G10"), 20},
        {QStringLiteral("G11"), 20}, {QStringLiteral("G15"), 22}, {QStringLiteral("G16"), 22},
        {QStringLiteral("G17"), 2}, {QStringLiteral("G18"), 2}, {QStringLiteral("G19"), 2},
        {QStringLiteral("G20"), 6}, {QStringLiteral("G21"), 6}, {QStringLiteral("G27"), 20},
        {QStringLiteral("G28"), 20}, {QStringLiteral("G29"), 20}, {QStringLiteral("G30"), 20},
        {QStringLiteral("G31"), 0}, {QStringLiteral("G31.2"), 0}, {QStringLiteral("G32"), 0},
        {QStringLiteral("G40"), 7}, {QStringLiteral("G41"), 7}, {QStringLiteral("G42"), 7},
        {QStringLiteral("G40.1"), 23}, {QStringLiteral("G41.1"), 23}, {QStringLiteral("G42.1"), 23},
        {QStringLiteral("G43"), 8}, {QStringLiteral("G44"), 8}, {QStringLiteral("G43.1"), 8},
        {QStringLiteral("G43.4"), 8}, {QStringLiteral("G49"), 8}, {QStringLiteral("G50"), 11},
        {QStringLiteral("G51"), 11}, {QStringLiteral("G50.1"), 18}, {QStringLiteral("G51.1"), 18},
        {QStringLiteral("G52"), 0}, {QStringLiteral("G53"), 0}, {QStringLiteral("G53.1"), 0},
        {QStringLiteral("G54"), 14}, {QStringLiteral("G55"), 14}, {QStringLiteral("G56"), 14},
        {QStringLiteral("G57"), 14}, {QStringLiteral("G58"), 14}, {QStringLiteral("G59"), 14},
        {QStringLiteral("G54.1"), 14}, {QStringLiteral("G61"), 15}, {QStringLiteral("G64"), 15},
        {QStringLiteral("G65"), 0}, {QStringLiteral("G66"), 12}, {QStringLiteral("G67"), 12},
        {QStringLiteral("G68"), 16}, {QStringLiteral("G69"), 16}, {QStringLiteral("G68.2"), 19},
        {QStringLiteral("G68.3"), 19}, {QStringLiteral("G69.2"), 19}, {QStringLiteral("G70"), 30},
        {QStringLiteral("G71"), 30}, {QStringLiteral("G72"), 30}, {QStringLiteral("G73"), 9},
        {QStringLiteral("G74"), 9}, {QStringLiteral("G76"), 9}, {QStringLiteral("G80"), 9},
        {QStringLiteral("G81"), 9}, {QStringLiteral("G82"), 9}, {QStringLiteral("G83"), 9},
        {QStringLiteral("G84"), 9}, {QStringLiteral("G85"), 9}, {QStringLiteral("G86"), 9},
        {QStringLiteral("G87"), 9}, {QStringLiteral("G80.1"), 9}, {QStringLiteral("G81.1"), 9},
        {QStringLiteral("G73.4"), 9}, {QStringLiteral("G74.4"), 9}, {QStringLiteral("G81.4"), 9},
        {QStringLiteral("G82.4"), 9}, {QStringLiteral("G83.4"), 9}, {QStringLiteral("G84.4"), 9},
        {QStringLiteral("G85.4"), 9}, {QStringLiteral("G86.4"), 9}, {QStringLiteral("G90"), 3},
        {QStringLiteral("G91"), 3}, {QStringLiteral("G90.1"), 4}, {QStringLiteral("G91.1"), 4},
        {QStringLiteral("G92"), 0}, {QStringLiteral("G92.1"), 0}, {QStringLiteral("G94"), 5},
        {QStringLiteral("G95"), 5}, {QStringLiteral("G98"), 10}, {QStringLiteral("G99"), 10},
        {QStringLiteral("G110"), 20}, {QStringLiteral("G150"), 20}, {QStringLiteral("G150.1"), 25},
        {QStringLiteral("G151.1"), 25}
    };
    for (int base = 154; base <= 954; base += 100) {
        for (int offset = 0; offset <= 5; ++offset) {
            p.gCodes.push_back({QStringLiteral("G%1").arg(base + offset), 14});
        }
    }
    const QVector<QString> modelCodes = {
        QStringLiteral("G160.1"), QStringLiteral("G160.2"), QStringLiteral("G160.3"),
        QStringLiteral("G161.1"), QStringLiteral("G161.2"), QStringLiteral("G162.1"),
        QStringLiteral("G162.2"), QStringLiteral("G162.3"), QStringLiteral("G162.4"),
        QStringLiteral("G162.5"), QStringLiteral("G162.6"), QStringLiteral("G162.7"),
        QStringLiteral("G163.1"), QStringLiteral("G163.2"), QStringLiteral("G163.3"),
        QStringLiteral("G164.1"), QStringLiteral("G164.2"), QStringLiteral("G164.3")
    };
    for (const QString &code : modelCodes) {
        p.gCodes.push_back({code, 20});
    }

    p.mCodes = {
        QStringLiteral("M00"), QStringLiteral("M01"), QStringLiteral("M02"),
        QStringLiteral("M03"), QStringLiteral("M05"), QStringLiteral("M06"),
        QStringLiteral("M07"), QStringLiteral("M08"), QStringLiteral("M09"),
        QStringLiteral("M18"), QStringLiteral("M19"), QStringLiteral("M28"),
        QStringLiteral("M29"), QStringLiteral("M30"), QStringLiteral("M98"),
        QStringLiteral("M99")
    };
    p.rules.cycles.fixedCyclesRequiringZrf = {
        QStringLiteral("G73"), QStringLiteral("G74"), QStringLiteral("G76"),
        QStringLiteral("G81"), QStringLiteral("G82"), QStringLiteral("G83"),
        QStringLiteral("G84"), QStringLiteral("G85"), QStringLiteral("G86"),
        QStringLiteral("G87"), QStringLiteral("G81.1")
    };
    p.modalMotionAllowedGroups = {2, 3, 4, 5, 7, 8, 10, 14, 15, 22, 23, 25};
    p.rules.units.supportsG20 = false;
    p.rules.programs.maxMCodeCountPerBlock = 3;
    return p;
}

const DialectProfile &lynucEdmProfile()
{
    static const DialectProfile profile = makeLynucEdmProfile();
    return profile;
}

DialectProfile makeSampleRelaxedProfile()
{
    DialectProfile p = makeLynucEdmProfile();
    p.name = QStringLiteral("Sample Relaxed");
    p.gCodes.push_back({QStringLiteral("G999"), 0});
    p.mCodes.push_back(QStringLiteral("M100"));
    p.rules.units.supportsG20 = true;
    p.rules.compensation.requireHForToolLengthComp = false;
    p.rules.compensation.requireDForCutterComp = false;
    p.rules.cycles.fixedCyclesRequiringZrf.clear();
    p.rules.cycles.pairG80_1WithG81_1 = false;
    p.rules.cycles.checkG86ZGreaterThanR = false;
    p.rules.programs.maxMCodeCountPerBlock = 4;
    p.rules.severityOverrides = {
        {QStringLiteral("NC_G51_SCALE_CONFLICT"), DiagnosticSeverity::Warning},
        {QStringLiteral("NC_UNKNOWN_M_CODE"), DiagnosticSeverity::Info}
    };
    return p;
}

const DialectProfile &sampleRelaxedProfile()
{
    static const DialectProfile profile = makeSampleRelaxedProfile();
    return profile;
}

} // namespace nc
