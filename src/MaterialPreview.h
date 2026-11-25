#pragma once

#include <QImage>
#include <QString>
#include <QVector4D>
#include <functional>

namespace kme {

// The bits of a .mat the viewport can show: base color and base texture.
struct PreviewMaterial {
    QVector4D baseColor{1, 1, 1, 1};
    QString texturePath; // resolved absolute path, empty = none
    QImage texture;
    bool doubleSided = false;
};

QImage loadTextureImage(const QString& absolutePath);

// resolve maps a content-relative path to an absolute one
bool loadPreviewMaterial(const QString& absolutePath, const std::function<QString(const QString&)>& resolve,
                         PreviewMaterial& out);

} // namespace kme
