#include "MaterialPreview.h"

#include "text_block_file.h"

#include <QDataStream>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

namespace kme {

namespace {

QString unquote(const std::string& raw)
{
    QString v = QString::fromStdString(raw).trimmed();
    if (v.size() >= 2 && v.startsWith('"') && v.endsWith('"')) v = v.mid(1, v.size() - 2);
    return v;
}

// same layout as engine/texture_file.cpp; only the top mip is read
QImage loadKTex(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    QDataStream in(&file);
    in.setByteOrder(QDataStream::LittleEndian);
    quint32 magic = 0, version = 0, sourceLength = 0;
    in >> magic >> version >> sourceLength;
    if (magic != 0x5845544Bu || version != 1u) return {};
    if (in.skipRawData(int(sourceLength)) != int(sourceLength)) return {};
    quint32 width = 0, height = 0, format = 0, flags = 0, mips = 0, size = 0;
    in >> width >> height >> format >> flags >> mips >> size;
    if (in.status() != QDataStream::Ok || format != 1 || width == 0 || height == 0 || width > 16384 ||
        height > 16384 || quint64(size) < quint64(width) * height * 4)
        return {};
    QImage image(int(width), int(height), QImage::Format_RGBA8888);
    for (quint32 y = 0; y < height; ++y)
        if (in.readRawData(reinterpret_cast<char*>(image.scanLine(int(y))), int(width * 4)) != int(width * 4)) return {};
    return image;
}

} // namespace

QImage loadTextureImage(const QString& absolutePath)
{
    if (QFileInfo(absolutePath).suffix().compare("ktex", Qt::CaseInsensitive) == 0) return loadKTex(absolutePath);
    QImage image(absolutePath);
    return image.isNull() ? image : image.convertToFormat(QImage::Format_RGBA8888);
}

bool loadPreviewMaterial(const QString& absolutePath, const std::function<QString(const QString&)>& resolve,
                         PreviewMaterial& out)
{
    out = {};
    std::vector<TextBlock> blocks;
    if (!LoadTextBlockFile(absolutePath.toUtf8().constData(), blocks)) return false;

    QString texture, graphTexture;
    for (const TextBlock& block : blocks) {
        if (block.type == "Material") {
            if (const std::string* v = FindTextBlockProperty(block, "BaseTexture"))
                texture = unquote(*v);
            else if (const std::string* legacy = FindTextBlockProperty(block, "Texture"))
                texture = unquote(*legacy);
            if (const std::string* v = FindTextBlockProperty(block, "BaseColor")) {
                const QStringList parts = unquote(*v).split(QRegularExpression("[,\\s]+"), Qt::SkipEmptyParts);
                for (int i = 0; i < 4 && i < parts.size(); ++i) {
                    bool ok = false;
                    const float f = parts[i].toFloat(&ok);
                    if (ok) out.baseColor[i] = f;
                }
            }
            if (const std::string* v = FindTextBlockProperty(block, "DoubleSided")) {
                const QString s = unquote(*v).toLower();
                out.doubleSided = s == "true" || s == "1";
            }
        } else if (block.type == "Node" && graphTexture.isEmpty()) {
            const std::string* type = FindTextBlockProperty(block, "Type");
            const std::string* asset = FindTextBlockProperty(block, "Asset");
            if (type && asset && unquote(*type) == "Texture") graphTexture = unquote(*asset);
        }
    }
    if (texture.isEmpty()) texture = graphTexture;
    if (!texture.isEmpty()) {
        out.texturePath = resolve(texture);
        out.texture = loadTextureImage(out.texturePath);
    }
    return true;
}

} // namespace kme
