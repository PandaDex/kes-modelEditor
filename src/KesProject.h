#pragma once

#include <QString>

namespace kme {

QString contentRootFromProject(const QString& kprojectPath);

// nearest .kproject, falls back to a "content" folder
QString findContentRoot(const QString& startPath);

QString contentRelativePath(const QString& contentRoot, const QString& absolutePath);

} // namespace kme
