#include "NodeLibrary.h"

#include "anim_blend_tree.h"

namespace kme {

namespace {

PortType toPortType(AnimPortType t)
{
    switch (t) {
    case AnimPortType::Pose: return PortType::Pose;
    case AnimPortType::Int: return PortType::Int;
    case AnimPortType::Bool: return PortType::Bool;
    default: return PortType::Float;
    }
}

QVariant defaultFor(const AnimNodeParamInfo& p)
{
    const QString text = QString::fromStdString(p.defaultValue);
    switch (p.kind) {
    case AnimParamKind::Float: return text.toDouble();
    case AnimParamKind::Int: return text.toInt();
    case AnimParamKind::Bool: return text == QLatin1String("true") || text == QLatin1String("1");
    default: return text;
    }
}

} // namespace

const NodeLibrary& NodeLibrary::instance()
{
    static const NodeLibrary lib;
    return lib;
}

NodeLibrary::NodeLibrary()
{
    for (const AnimNodeInfo& info : GetAnimNodeCatalog()) {
        NodeDef d;
        d.type = QString::fromStdString(info.type);
        d.title = QString::fromStdString(info.title);
        d.category = QString::fromStdString(info.category);
        d.description = QString::fromStdString(info.description);
        d.role = info.type == "Output" ? NodeRole::Output : NodeRole::Normal;
        for (const AnimNodePortInfo& p : info.inputs)
            d.inputs.append(PortDef{QString::fromStdString(p.name), toPortType(p.type), p.defaultValue});
        for (const AnimNodePortInfo& p : info.outputs)
            d.outputs.append(PortDef{QString::fromStdString(p.name), toPortType(p.type), p.defaultValue});
        for (const AnimNodeParamInfo& p : info.params) {
            ParamDef pd;
            pd.name = QString::fromStdString(p.name);
            pd.kind = static_cast<ParamKind>(static_cast<int>(p.kind));
            pd.defaultValue = defaultFor(p);
            for (const std::string& o : p.options) pd.options << QString::fromStdString(o);
            d.params.append(pd);
        }
        m_index.insert(d.type, m_defs.size());
        m_defs.append(d);
    }
}

const NodeDef* NodeLibrary::find(const QString& type) const
{
    auto it = m_index.constFind(type);
    return it == m_index.constEnd() ? nullptr : &m_defs[*it];
}

} // namespace kme
