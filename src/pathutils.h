#ifndef PATHUTILS_H
#define PATHUTILS_H

#include <QDir>
#include <QFileInfo>
#include <QString>

namespace PathUtils {

inline QString normalizePath(const QString &path)
{
    QFileInfo info(path);
    QString normalized = info.canonicalFilePath();
    if (normalized.isEmpty()) {
        normalized = QDir::cleanPath(info.absoluteFilePath());
    }
#ifdef Q_OS_WIN
    normalized = normalized.toLower();
#endif
    return normalized;
}

} // namespace PathUtils

#endif // PATHUTILS_H
