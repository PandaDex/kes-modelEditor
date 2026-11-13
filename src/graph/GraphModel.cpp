#include "GraphModel.h"

#include "anim_blend_tree.h"

#include <QJsonArray>
#include <QSet>
#include <QUndoCommand>
#include <algorithm>

namespace kme {

class SnapshotCommand : public QUndoCommand {
public:
    SnapshotCommand(GraphModel* model, QJsonObject before, QJsonObject after,
                    const QString& text, const QString& mergeKey)
        : QUndoCommand(text), m_model(model), m_before(std::move(before)), m_after(std::move(after)),
          m_mergeKey(mergeKey)
    {
    }

    int id() const override
    {
        return m_mergeKey.isEmpty() ? -1 : int(qHash(m_mergeKey) & 0x7fffffff);
    }

    bool mergeWith(const QUndoCommand* other) override
    {
        auto* o = dynamic_cast<const SnapshotCommand*>(other);
        if (!o || o->m_mergeKey != m_mergeKey || m_mergeKey.isEmpty()) return false;
        m_after = o->m_after;
        return true;
    }

    void undo() override { m_model->loadJson(m_before); }
    void redo() override
    {
        if (m_first) { m_first = false; return; }
        m_model->loadJson(m_after);
    }

private:
    GraphModel* m_model;
    QJsonObject m_before, m_after;
    QString m_mergeKey;
    bool m_first = true;
};

GraphModel::GraphModel(QObject* parent) : QObject(parent) {}

const GraphNode* GraphModel::node(int id) const
{
    auto it = m_nodes.constFind(id);
    return it == m_nodes.constEnd() ? nullptr : &it.value();
}

const NodeDef* GraphModel::def(int nodeId) const
{
    const GraphNode* n = node(nodeId);
    return n ? NodeLibrary::instance().find(n->type) : nullptr;
}

QList<int> GraphModel::nodeIds() const
{
    QList<int> ids = m_nodes.keys();
    std::sort(ids.begin(), ids.end());
    return ids;
}

const GraphConnection* GraphModel::connection(int id) const
{
    for (const GraphConnection& c : m_connections) if (c.id == id) return &c;
    return nullptr;
}

const GraphConnection* GraphModel::inputConnection(int nodeId, int inputPort) const
{
    for (const GraphConnection& c : m_connections)
        if (c.toNode == nodeId && c.toPort == inputPort) return &c;
    return nullptr;
}

QList<const GraphConnection*> GraphModel::outputConnections(int nodeId, int outputPort) const
{
    QList<const GraphConnection*> r;
    for (const GraphConnection& c : m_connections)
        if (c.fromNode == nodeId && c.fromPort == outputPort) r << &c;
    return r;
}

bool GraphModel::hasOutputNode() const { return outputNodeId() >= 0; }

int GraphModel::outputNodeId() const
{
    for (int id : nodeIds()) {
        const NodeDef* d = def(id);
        if (d && d->role == NodeRole::Output) return id;
    }
    return -1;
}

QVariant GraphModel::param(int nodeId, const QString& name) const
{
    const GraphNode* n = node(nodeId);
    if (!n) return {};
    auto it = n->params.constFind(name);
    if (it != n->params.constEnd()) return it.value();
    const NodeDef* d = def(nodeId);
    const ParamDef* p = d ? d->paramDef(name) : nullptr;
    return p ? p->defaultValue : QVariant();
}

QVariantList GraphModel::inputDefault(int nodeId, int inputPort) const
{
    const GraphNode* n = node(nodeId);
    const NodeDef* d = def(nodeId);
    if (!n || !d || inputPort < 0 || inputPort >= d->inputs.size()) return {};
    const PortDef& p = d->inputs[inputPort];
    auto it = n->inputDefaults.constFind(p.name);
    if (it != n->inputDefaults.constEnd()) return it.value().toList();
    return {p.defaultValue};
}

bool GraphModel::reaches(int fromNode, int targetNode) const
{
    QSet<int> seen;
    QList<int> stack{fromNode};
    while (!stack.isEmpty()) {
        int cur = stack.takeLast();
        if (cur == targetNode) return true;
        if (seen.contains(cur)) continue;
        seen.insert(cur);
        for (const GraphConnection& c : m_connections)
            if (c.fromNode == cur) stack << c.toNode;
    }
    return false;
}

bool GraphModel::canConnect(int fromNode, int fromPort, int toNode, int toPort, QString* why) const
{
    auto fail = [&](const char* msg) {
        if (why) *why = QString::fromLatin1(msg);
        return false;
    };
    const NodeDef* df = def(fromNode);
    const NodeDef* dt = def(toNode);
    if (!df || !dt) return fail("Unknown node");
    if (fromNode == toNode) return fail("Cannot connect a node to itself");
    if (fromPort < 0 || fromPort >= df->outputs.size()) return fail("Invalid output port");
    if (toPort < 0 || toPort >= dt->inputs.size()) return fail("Invalid input port");
    if (!portsCompatible(df->outputs[fromPort].type, dt->inputs[toPort].type))
        return fail("A pose only connects to a pose input");
    if (reaches(toNode, fromNode)) return fail("Connection would create a cycle");
    return true;
}

int GraphModel::addNode(const QString& type, const QPointF& pos, int forcedId)
{
    const NodeDef* d = NodeLibrary::instance().find(type);
    if (!d) return -1;
    if (d->role == NodeRole::Output && hasOutputNode()) return -1;

    GraphNode n;
    n.id = (forcedId > 0 && !m_nodes.contains(forcedId)) ? forcedId : m_nextNodeId;
    m_nextNodeId = std::max(m_nextNodeId, n.id + 1);
    n.type = type;
    n.pos = pos;
    m_nodes.insert(n.id, n);
    emit nodeAdded(n.id);
    emit changed();
    return n.id;
}

void GraphModel::removeNode(int id)
{
    if (!m_nodes.contains(id)) return;
    QList<int> conns;
    for (const GraphConnection& c : m_connections)
        if (c.fromNode == id || c.toNode == id) conns << c.id;
    for (int cid : conns) removeConnection(cid);
    m_nodes.remove(id);
    emit nodeRemoved(id);
    emit changed();
}

int GraphModel::addConnection(int fromNode, int fromPort, int toNode, int toPort)
{
    if (!canConnect(fromNode, fromPort, toNode, toPort)) return -1;
    if (const GraphConnection* existing = inputConnection(toNode, toPort)) removeConnection(existing->id);

    GraphConnection c;
    c.id = m_nextConnId++;
    c.fromNode = fromNode;
    c.fromPort = fromPort;
    c.toNode = toNode;
    c.toPort = toPort;
    m_connections.append(c);
    emit connectionAdded(c.id);
    emit changed();
    return c.id;
}

void GraphModel::removeConnection(int id)
{
    for (int i = 0; i < m_connections.size(); ++i) {
        if (m_connections[i].id == id) {
            m_connections.removeAt(i);
            emit connectionRemoved(id);
            emit changed();
            return;
        }
    }
}

void GraphModel::setNodePos(int id, const QPointF& pos)
{
    auto it = m_nodes.find(id);
    if (it == m_nodes.end() || it->pos == pos) return;
    it->pos = pos;
    emit nodePosChanged(id);
}

void GraphModel::setParam(int id, const QString& name, const QVariant& value)
{
    auto it = m_nodes.find(id);
    if (it == m_nodes.end()) return;
    it->params.insert(name, value);
    emit nodeChanged(id);
    emit changed();
}

void GraphModel::setInputDefault(int id, const QString& portName, const QVariantList& value)
{
    auto it = m_nodes.find(id);
    if (it == m_nodes.end()) return;
    it->inputDefaults.insert(portName, value);
    emit nodeChanged(id);
    emit changed();
}

void GraphModel::setModelPath(const QString& path)
{
    if (m_modelPath == path) return;
    m_modelPath = path;
    emit modelPathChanged();
    emit changed();
}

void GraphModel::edit(const QString& text, const std::function<void()>& fn, const QString& mergeKey)
{
    if (m_inEdit) { fn(); return; }
    const QJsonObject before = toJson();
    m_inEdit = true;
    fn();
    m_inEdit = false;
    const QJsonObject after = toJson();
    if (before == after) return;
    m_undo.push(new SnapshotCommand(this, before, after, text, mergeKey));
}

QJsonObject GraphModel::toJson(const QList<int>* subset) const
{
    QSet<int> keep;
    if (subset) for (int id : *subset) keep.insert(id);

    QJsonArray nodes;
    for (int id : nodeIds()) {
        if (subset && !keep.contains(id)) continue;
        const GraphNode& n = m_nodes[id];
        QJsonObject o;
        o["id"] = n.id;
        o["type"] = n.type;
        o["x"] = n.pos.x();
        o["y"] = n.pos.y();
        o["params"] = QJsonObject::fromVariantMap(n.params);
        o["defaults"] = QJsonObject::fromVariantMap(n.inputDefaults);
        nodes.append(o);
    }

    QJsonArray conns;
    for (const GraphConnection& c : m_connections) {
        if (subset && (!keep.contains(c.fromNode) || !keep.contains(c.toNode))) continue;
        const NodeDef* df = def(c.fromNode);
        const NodeDef* dt = def(c.toNode);
        if (!df || !dt) continue;
        QJsonObject o;
        o["id"] = c.id;
        o["from"] = c.fromNode;
        o["fromPort"] = df->outputs.value(c.fromPort).name;
        o["to"] = c.toNode;
        o["toPort"] = dt->inputs.value(c.toPort).name;
        conns.append(o);
    }

    QJsonObject root;
    root["format"] = "kesAnimGraph";
    root["version"] = 1;
    root["nodes"] = nodes;
    root["connections"] = conns;
    if (!subset) root["model"] = m_modelPath;
    return root;
}

bool GraphModel::parse(const QJsonObject& obj, bool remap, const QPointF& offset, QList<int>* newIds,
                       QStringList* warnings)
{
    if (obj.value("format").toString() != "kesAnimGraph") {
        if (warnings) *warnings << "Not an animation graph.";
        return false;
    }
    if (!remap) m_modelPath = obj.value("model").toString();

    QHash<int, int> idMap;
    const QJsonArray nodes = obj.value("nodes").toArray();
    for (const QJsonValue& v : nodes) {
        const QJsonObject o = v.toObject();
        const QString type = o.value("type").toString();
        const NodeDef* d = NodeLibrary::instance().find(type);
        const int fileId = o.value("id").toInt(-1);
        if (!d) {
            if (warnings) *warnings << QString("Skipped unknown node type '%1'.").arg(type);
            continue;
        }
        const QPointF pos(o.value("x").toDouble() + offset.x(), o.value("y").toDouble() + offset.y());
        const int id = addNode(type, pos, remap ? -1 : fileId);
        if (id < 0) {
            if (warnings) *warnings << QString("Skipped node '%1' (e.g. duplicate output node).").arg(type);
            continue;
        }
        idMap.insert(fileId, id);
        if (newIds) newIds->append(id);

        const QVariantMap params = o.value("params").toObject().toVariantMap();
        for (auto it = params.constBegin(); it != params.constEnd(); ++it)
            if (d->paramDef(it.key())) setParam(id, it.key(), it.value());
        const QVariantMap defs = o.value("defaults").toObject().toVariantMap();
        for (auto it = defs.constBegin(); it != defs.constEnd(); ++it)
            if (d->inputIndex(it.key()) >= 0) setInputDefault(id, it.key(), it.value().toList());
    }

    for (const QJsonValue& v : obj.value("connections").toArray()) {
        const QJsonObject o = v.toObject();
        if (!idMap.contains(o.value("from").toInt(-1)) || !idMap.contains(o.value("to").toInt(-1))) continue;
        const int from = idMap[o.value("from").toInt()];
        const int to = idMap[o.value("to").toInt()];
        const NodeDef* df = def(from);
        const NodeDef* dt = def(to);
        const int fp = df ? df->outputIndex(o.value("fromPort").toString()) : -1;
        const int tp = dt ? dt->inputIndex(o.value("toPort").toString()) : -1;
        if (fp < 0 || tp < 0 || addConnection(from, fp, to, tp) < 0) {
            if (warnings) *warnings << "Skipped an invalid connection.";
        }
    }
    return true;
}

void GraphModel::clear()
{
    m_nodes.clear();
    m_connections.clear();
    m_modelPath.clear();
    m_nextNodeId = 1;
    m_nextConnId = 1;
}

bool GraphModel::loadJson(const QJsonObject& obj, QStringList* warnings)
{
    blockSignals(true);
    clear();
    const bool ok = parse(obj, false, QPointF(), nullptr, warnings);
    blockSignals(false);
    emit graphReset();
    emit modelPathChanged();
    emit changed();
    return ok;
}

namespace {

QString valueText(const QVariant& v)
{
    if (v.typeId() == QMetaType::Bool) return v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    if (v.typeId() == QMetaType::Double || v.typeId() == QMetaType::Float) return QString::number(v.toDouble(), 'g', 9);
    return v.toString();
}

} // namespace

AnimBlendTreeData GraphModel::toBlendTree() const
{
    AnimBlendTreeData tree;
    tree.modelPath = m_modelPath.toStdString();
    for (int id : nodeIds()) {
        const GraphNode& n = m_nodes[id];
        const NodeDef* d = def(id);
        if (!d) continue;
        AnimBlendTreeNode out;
        out.id = id;
        out.type = n.type.toStdString();
        out.position[0] = float(n.pos.x());
        out.position[1] = float(n.pos.y());
        for (const ParamDef& p : d->params) out.Set(p.name.toStdString(), valueText(param(id, p.name)).toStdString());
        for (int i = 0; i < d->inputs.size(); ++i) {
            const PortDef& port = d->inputs[i];
            if (port.type == PortType::Pose || inputConnection(id, i)) continue;
            const QVariantList v = inputDefault(id, i);
            const double value = v.isEmpty() ? port.defaultValue : v[0].toDouble();
            if (port.type == PortType::Bool)
                out.Set(port.name.toStdString(), value != 0.0 ? "true" : "false");
            else
                out.Set(port.name.toStdString(), QString::number(value, 'g', 9).toStdString());
        }
        tree.nodes.push_back(std::move(out));
    }
    for (const GraphConnection& c : m_connections) {
        const NodeDef* df = def(c.fromNode);
        const NodeDef* dt = def(c.toNode);
        if (!df || !dt) continue;
        tree.links.push_back({c.fromNode, df->outputs.value(c.fromPort).name.toStdString(), c.toNode,
                              dt->inputs.value(c.toPort).name.toStdString()});
    }
    return tree;
}

void GraphModel::loadBlendTree(const AnimBlendTreeData& tree, QStringList* warnings)
{
    QJsonArray nodes;
    for (const AnimBlendTreeNode& n : tree.nodes) {
        const QString type = QString::fromStdString(n.type);
        const NodeDef* d = NodeLibrary::instance().find(type);
        QJsonObject o;
        o["id"] = n.id;
        o["type"] = type;
        o["x"] = n.position[0];
        o["y"] = n.position[1];
        QJsonObject params, defaults;
        for (const auto& [key, raw] : n.values) {
            const QString k = QString::fromStdString(key);
            const QString text = QString::fromStdString(raw);
            if (!d) continue;
            if (const ParamDef* p = d->paramDef(k)) {
                switch (p->kind) {
                case ParamKind::Float: params[k] = text.toDouble(); break;
                case ParamKind::Int: params[k] = text.toInt(); break;
                case ParamKind::Bool: params[k] = text == QLatin1String("true") || text == QLatin1String("1"); break;
                default: params[k] = text; break;
                }
            } else if (d->inputIndex(k) >= 0) {
                const bool isBool = d->inputs[d->inputIndex(k)].type == PortType::Bool;
                const double value = isBool ? ((text == QLatin1String("true") || text == QLatin1String("1")) ? 1.0 : 0.0)
                                            : text.toDouble();
                defaults[k] = QJsonArray{value};
            }
        }
        o["params"] = params;
        o["defaults"] = defaults;
        nodes.append(o);
    }
    QJsonArray conns;
    for (const AnimBlendTreeLink& l : tree.links) {
        QJsonObject o;
        o["from"] = l.fromNode;
        o["fromPort"] = QString::fromStdString(l.fromPort);
        o["to"] = l.toNode;
        o["toPort"] = QString::fromStdString(l.toPort);
        conns.append(o);
    }
    QJsonObject root;
    root["format"] = "kesAnimGraph";
    root["model"] = QString::fromStdString(tree.modelPath);
    root["nodes"] = nodes;
    root["connections"] = conns;
    loadJson(root, warnings);
}

QList<int> GraphModel::insertJson(const QJsonObject& obj, const QPointF& offset, QStringList* warnings)
{
    QList<int> ids;
    parse(obj, true, offset, &ids, warnings);
    return ids;
}

} // namespace kme
