#pragma once

#include "Types.h"

#include <QHash>
#include <QStringList>
#include <QVariant>
#include <QVector>

namespace kme {

// keep in sync with AnimParamKind in engine/anim_blend_tree.h
enum class ParamKind { Float, Int, Bool, Enum, Name, Clip, Bone };

struct ParamDef {
    QString name;
    ParamKind kind = ParamKind::Float;
    QVariant defaultValue;
    QStringList options;
};

struct PortDef {
    QString name;
    PortType type = PortType::Float;
    double defaultValue = 0.0;
};

enum class NodeRole { Normal, Output };

struct NodeDef {
    QString type;
    QString title;
    QString category;
    QString description;
    QVector<PortDef> inputs;
    QVector<PortDef> outputs;
    QVector<ParamDef> params;
    NodeRole role = NodeRole::Normal;

    int inputIndex(const QString& n) const
    {
        for (int i = 0; i < inputs.size(); ++i) if (inputs[i].name == n) return i;
        return -1;
    }
    int outputIndex(const QString& n) const
    {
        for (int i = 0; i < outputs.size(); ++i) if (outputs[i].name == n) return i;
        return -1;
    }
    const ParamDef* paramDef(const QString& n) const
    {
        for (const ParamDef& p : params) if (p.name == n) return &p;
        return nullptr;
    }
};

class NodeLibrary {
public:
    static const NodeLibrary& instance();

    const NodeDef* find(const QString& type) const;
    const QVector<NodeDef>& all() const { return m_defs; }

private:
    NodeLibrary();
    QVector<NodeDef> m_defs;
    QHash<QString, int> m_index;
};

} // namespace kme
