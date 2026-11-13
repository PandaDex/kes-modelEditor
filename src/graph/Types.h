#pragma once

#include <QString>

namespace kme {

enum class PortType { Pose, Float, Int, Bool };

// numeric types convert into each other, poses only connect to poses
inline bool portsCompatible(PortType from, PortType to)
{
    return (from == PortType::Pose) == (to == PortType::Pose);
}

inline QString portTypeName(PortType t)
{
    switch (t) {
    case PortType::Pose: return QStringLiteral("pose");
    case PortType::Int: return QStringLiteral("int");
    case PortType::Bool: return QStringLiteral("bool");
    default: return QStringLiteral("float");
    }
}

} // namespace kme
