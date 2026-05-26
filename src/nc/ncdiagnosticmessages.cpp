#include "ncdiagnosticmessages.h"

namespace nc {
namespace {

QString englishMessage(const QString &code)
{
    if (code == QStringLiteral("NC_UNCLOSED_COMMENT")) {
        return QStringLiteral("Comment parenthesis is not closed.");
    }
    if (code == QStringLiteral("NC_UNEXPECTED_CHAR")) {
        return QStringLiteral("Unexpected character in NC block.");
    }
    if (code == QStringLiteral("NC_WORD_MISSING_VALUE")) {
        return QStringLiteral("Address word is missing a value or expression.");
    }
    if (code == QStringLiteral("NC_UNCLOSED_EXPRESSION")) {
        return QStringLiteral("Macro expression bracket is not closed.");
    }
    if (code == QStringLiteral("NC_INVALID_NUMBER")) {
        return QStringLiteral("Address word has an invalid numeric value.");
    }
    if (code == QStringLiteral("NC_UNKNOWN_G_CODE")) {
        return QStringLiteral("G code is not defined by the active profile.");
    }
    if (code == QStringLiteral("NC_MODAL_G_CONFLICT")) {
        return QStringLiteral("Conflicting modal G codes appear in the same block.");
    }
    if (code == QStringLiteral("NC_G20_UNSUPPORTED")) {
        return QStringLiteral("The active profile does not support G20 inch units.");
    }
    if (code == QStringLiteral("NC_UNKNOWN_M_CODE")) {
        return QStringLiteral("M code is not defined by the active profile.");
    }
    if (code == QStringLiteral("NC_DUPLICATE_ADDRESS")) {
        return QStringLiteral("Duplicate address word appears in the same block.");
    }
    if (code == QStringLiteral("NC_TOO_MANY_M_CODES")) {
        return QStringLiteral("Too many M codes appear in one block.");
    }
    if (code == QStringLiteral("NC_NON_POSITIVE_FEED")) {
        return QStringLiteral("Feed rate should normally be positive.");
    }
    if (code == QStringLiteral("NC_TOOL_LENGTH_MISSING_H")) {
        return QStringLiteral("G43/G44 usually requires an H compensation number.");
    }
    if (code == QStringLiteral("NC_CUTTER_COMP_MISSING_D")) {
        return QStringLiteral("G41/G42 usually requires a D compensation number.");
    }
    if (code == QStringLiteral("NC_G51_SCALE_CONFLICT")) {
        return QStringLiteral("G51 cannot combine P scale with I/J/K scale values.");
    }
    if (code == QStringLiteral("NC_G68_MISSING_R")) {
        return QStringLiteral("G68 coordinate rotation requires an R angle.");
    }
    if (code == QStringLiteral("NC_M06_MISSING_T")) {
        return QStringLiteral("M06 should have a T tool number in this or a previous block.");
    }
    if (code == QStringLiteral("NC_M03_MISSING_S")) {
        return QStringLiteral("M03 should normally have an S spindle speed.");
    }
    if (code == QStringLiteral("NC_ARC_MISSING_CENTER")) {
        return QStringLiteral("Arc interpolation requires R or I/J/K center parameters.");
    }
    if (code == QStringLiteral("NC_ARC_R_PRECEDENCE")) {
        return QStringLiteral("When R and I/J/K are both set, the profile uses R.");
    }
    if (code == QStringLiteral("NC_FULL_CIRCLE_WITH_R")) {
        return QStringLiteral("Full-circle machining should use I/J/K instead of R.");
    }
    if (code == QStringLiteral("NC_G04_X_P_CONFLICT")) {
        return QStringLiteral("G04 dwell cannot use X and P at the same time.");
    }
    if (code == QStringLiteral("NC_G04_NEGATIVE_TIME")) {
        return QStringLiteral("G04 dwell time cannot be negative.");
    }
    if (code == QStringLiteral("NC_FIXED_CYCLE_MISSING_ARGUMENT")) {
        return QStringLiteral("Fixed cycle is missing one of Z, R, or F.");
    }
    if (code == QStringLiteral("NC_REPEAT_OUT_OF_RANGE")) {
        return QStringLiteral("Subprogram repeat count L must be in range 1 to 1000.");
    }
    if (code == QStringLiteral("NC_G80_1_WITHOUT_G81_1")) {
        return QStringLiteral("G80.1 should pair with an active G81.1 high-speed cycle.");
    }
    if (code == QStringLiteral("NC_G86_Z_GREATER_THAN_R")) {
        return QStringLiteral("G86 Z value must not be greater than R value.");
    }
    return QString();
}

QString chineseMessage(const QString &code)
{
    if (code == QStringLiteral("NC_UNCLOSED_COMMENT")) {
        return QStringLiteral("\u6ce8\u91ca\u62ec\u53f7\u672a\u95ed\u5408\u3002");
    }
    if (code == QStringLiteral("NC_UNEXPECTED_CHAR")) {
        return QStringLiteral("\u7a0b\u5e8f\u6bb5\u4e2d\u6709\u65e0\u6cd5\u8bc6\u522b\u7684\u5b57\u7b26\u3002");
    }
    if (code == QStringLiteral("NC_WORD_MISSING_VALUE")) {
        return QStringLiteral("\u5730\u5740\u5b57\u7f3a\u5c11\u540e\u7eed\u6570\u503c\u6216\u8868\u8fbe\u5f0f\u3002");
    }
    if (code == QStringLiteral("NC_UNCLOSED_EXPRESSION")) {
        return QStringLiteral("\u5b8f\u8868\u8fbe\u5f0f\u65b9\u62ec\u53f7\u672a\u95ed\u5408\u3002");
    }
    if (code == QStringLiteral("NC_INVALID_NUMBER")) {
        return QStringLiteral("\u5730\u5740\u5b57\u7684\u6570\u503c\u683c\u5f0f\u4e0d\u6b63\u786e\u3002");
    }
    if (code == QStringLiteral("NC_UNKNOWN_G_CODE")) {
        return QStringLiteral("\u5f53\u524d\u65b9\u8a00\u672a\u5b9a\u4e49\u8be5 G \u6307\u4ee4\u3002");
    }
    if (code == QStringLiteral("NC_MODAL_G_CONFLICT")) {
        return QStringLiteral("\u540c\u4e00\u7a0b\u5e8f\u6bb5\u4e2d\u51fa\u73b0\u4e92\u65a5\u7684\u540c\u7ec4 G \u6307\u4ee4\u3002");
    }
    if (code == QStringLiteral("NC_G20_UNSUPPORTED")) {
        return QStringLiteral("\u5f53\u524d\u65b9\u8a00\u4e0d\u652f\u6301 G20 \u82f1\u5bf8\u5355\u4f4d\u3002");
    }
    if (code == QStringLiteral("NC_UNKNOWN_M_CODE")) {
        return QStringLiteral("\u5f53\u524d\u65b9\u8a00\u672a\u5b9a\u4e49\u8be5 M \u6307\u4ee4\u3002");
    }
    if (code == QStringLiteral("NC_DUPLICATE_ADDRESS")) {
        return QStringLiteral("\u540c\u4e00\u7a0b\u5e8f\u6bb5\u4e2d\u91cd\u590d\u6307\u5b9a\u4e86\u5730\u5740\u5b57\u3002");
    }
    if (code == QStringLiteral("NC_TOO_MANY_M_CODES")) {
        return QStringLiteral("\u4e00\u4e2a\u7a0b\u5e8f\u6bb5\u4e2d\u7684 M \u4ee3\u7801\u6570\u91cf\u8fc7\u591a\u3002");
    }
    if (code == QStringLiteral("NC_NON_POSITIVE_FEED")) {
        return QStringLiteral("F \u8fdb\u7ed9\u901f\u5ea6\u901a\u5e38\u5e94\u4e3a\u6b63\u503c\u3002");
    }
    if (code == QStringLiteral("NC_TOOL_LENGTH_MISSING_H")) {
        return QStringLiteral("G43/G44 \u901a\u5e38\u9700\u8981 H \u957f\u5ea6\u8865\u507f\u53f7\u3002");
    }
    if (code == QStringLiteral("NC_CUTTER_COMP_MISSING_D")) {
        return QStringLiteral("G41/G42 \u901a\u5e38\u9700\u8981 D \u534a\u5f84\u8865\u507f\u53f7\u3002");
    }
    if (code == QStringLiteral("NC_G51_SCALE_CONFLICT")) {
        return QStringLiteral("G51 \u4e0d\u80fd\u540c\u65f6\u6307\u5b9a P \u4e0e I/J/K \u7f29\u653e\u503c\u3002");
    }
    if (code == QStringLiteral("NC_G68_MISSING_R")) {
        return QStringLiteral("G68 \u5750\u6807\u65cb\u8f6c\u9700\u8981\u6307\u5b9a R \u89d2\u5ea6\u3002");
    }
    if (code == QStringLiteral("NC_M06_MISSING_T")) {
        return QStringLiteral("M06 \u6362\u5200\u5e94\u5728\u672c\u6bb5\u6216\u524d\u9762\u7a0b\u5e8f\u6bb5\u6307\u5b9a T \u5200\u5177\u53f7\u3002");
    }
    if (code == QStringLiteral("NC_M03_MISSING_S")) {
        return QStringLiteral("M03 \u4e3b\u8f74\u6b63\u8f6c\u901a\u5e38\u9700\u8981 S \u8f6c\u901f\u3002");
    }
    if (code == QStringLiteral("NC_ARC_MISSING_CENTER")) {
        return QStringLiteral("\u5706\u5f27\u63d2\u8865\u9700\u8981 R \u6216 I/J/K \u5706\u5fc3\u53c2\u6570\u3002");
    }
    if (code == QStringLiteral("NC_ARC_R_PRECEDENCE")) {
        return QStringLiteral("\u540c\u65f6\u6307\u5b9a R \u548c I/J/K \u65f6\uff0c\u7cfb\u7edf\u4f18\u5148\u4f7f\u7528 R\u3002");
    }
    if (code == QStringLiteral("NC_FULL_CIRCLE_WITH_R")) {
        return QStringLiteral("\u6574\u5706\u52a0\u5de5\u5e94\u4f7f\u7528 I/J/K\uff0c\u4e0d\u5e94\u4f7f\u7528 R\u3002");
    }
    if (code == QStringLiteral("NC_G04_X_P_CONFLICT")) {
        return QStringLiteral("G04 \u6682\u505c\u4e0d\u80fd\u540c\u65f6\u4f7f\u7528 X \u548c P\u3002");
    }
    if (code == QStringLiteral("NC_G04_NEGATIVE_TIME")) {
        return QStringLiteral("G04 \u6682\u505c\u65f6\u95f4\u4e0d\u80fd\u4e3a\u8d1f\u503c\u3002");
    }
    if (code == QStringLiteral("NC_FIXED_CYCLE_MISSING_ARGUMENT")) {
        return QStringLiteral("\u56fa\u5b9a\u5faa\u73af\u7f3a\u5c11 Z\u3001R \u6216 F \u53c2\u6570\u3002");
    }
    if (code == QStringLiteral("NC_REPEAT_OUT_OF_RANGE")) {
        return QStringLiteral("\u5b50\u7a0b\u5e8f\u91cd\u590d\u6b21\u6570 L \u5e94\u5728 1 \u5230 1000 \u4e4b\u95f4\u3002");
    }
    if (code == QStringLiteral("NC_G80_1_WITHOUT_G81_1")) {
        return QStringLiteral("G80.1 \u5e94\u4e0e\u5df2\u542f\u7528\u7684 G81.1 \u9ad8\u901f\u5faa\u73af\u914d\u5bf9\u4f7f\u7528\u3002");
    }
    if (code == QStringLiteral("NC_G86_Z_GREATER_THAN_R")) {
        return QStringLiteral("G86 \u4e2d Z \u503c\u4e0d\u80fd\u5927\u4e8e R \u503c\u3002");
    }
    return QString();
}

} // namespace

QString diagnosticMessage(const QString &code, DiagnosticMessageLanguage language)
{
    const QString localized = language == DiagnosticMessageLanguage::SimplifiedChinese
        ? chineseMessage(code)
        : englishMessage(code);
    if (!localized.isEmpty()) {
        return localized;
    }
    const QString english = englishMessage(code);
    return english.isEmpty() ? code : english;
}

} // namespace nc
