#ifndef NCDIAGNOSTICMESSAGES_H
#define NCDIAGNOSTICMESSAGES_H

#include <QString>

namespace nc {

enum class DiagnosticMessageLanguage {
    English,
    SimplifiedChinese
};

QString diagnosticMessage(const QString &code,
                          DiagnosticMessageLanguage language = DiagnosticMessageLanguage::SimplifiedChinese);

} // namespace nc

#endif // NCDIAGNOSTICMESSAGES_H
