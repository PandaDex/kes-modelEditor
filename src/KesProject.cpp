#include "KesProject.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

namespace kme {

QString contentRootFromProject(const QString& kprojectPath)
{
    QFile f(kprojectPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    static const QRegularExpression re(QStringLiteral(R"re(AssetRoot\s*=\s*"?([^"\r\n]+?)"?\s*$)re"),
                                       QRegularExpression::MultilineOption);
    const QRegularExpressionMatch m = re.match(QString::fromUtf8(f.readAll()));
    if (!m.hasMatch()) return {};
    return QDir::cleanPath(QFileInfo(kprojectPath).absoluteDir().absoluteFilePath(m.captured(1)));
}

QString findContentRoot(const QString& startPath)
{
    const QFileInfo start(startPath);
    QDir dir(start.isDir() ? start.absoluteFilePath() : start.absolutePath());
    do {
        const QStringList projects = dir.entryList({"*.kproject"}, QDir::Files);
        if (!projects.isEmpty()) {
            const QString root = contentRootFromProject(dir.absoluteFilePath(projects.first()));
            if (!root.isEmpty()) return root;
        }
        if (dir.dirName() == QLatin1String("content")) return dir.absolutePath();
    } while (dir.cdUp());
    return {};
}

QString contentRelativePath(const QString& contentRoot, const QString& absolutePath)
{
    if (contentRoot.isEmpty()) return {};
    const QString rel = QDir(contentRoot).relativeFilePath(absolutePath);
    if (rel.startsWith(QLatin1String("..")) || QDir::isAbsolutePath(rel)) return {};
    return rel;
}

} // namespace kme
