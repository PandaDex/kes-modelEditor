// test model: "lower" at the origin, "upper" 1 unit above it, clip bends "upper" 90 deg around Z over 1s
#include "GraphModel.h"
#include "ModelDocument.h"
#include "anim_blend_tree.h"
#include "animation_pose.h"

#include <QCoreApplication>
#include <QDir>
#include <QTemporaryDir>
#include <cmath>
#include <cstdio>

namespace {

int g_failures = 0;

#define EXPECT(cond)                                                       \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::printf("FAILED line %d: %s\n", __LINE__, #cond);          \
            ++g_failures;                                                  \
        }                                                                  \
    } while (0)

KModelData makeModel()
{
    KModelData m;
    KModelMesh mesh;
    mesh.name = "bar";
    mesh.vertices.resize(3);
    mesh.vertices[1].px = 0.1f;
    mesh.vertices[2].py = 2.0f;
    mesh.vertices[2].boneIndices[0] = 1;
    mesh.indices = {0, 1, 2};
    mesh.primitives.push_back({0, 3, -1});
    m.meshes.push_back(mesh);

    KModelNode meshNode;
    meshNode.name = "bar";
    meshNode.meshIndex = 0;
    KModelNode lower;
    lower.name = "lower";
    lower.boneIndex = 0;
    KModelNode upperNode;
    upperNode.name = "upper";
    upperNode.parentIndex = 1;
    upperNode.boneIndex = 1;
    upperNode.translation[1] = 1.0f;
    m.nodes = {meshNode, lower, upperNode};

    KModelBone b0;
    b0.name = "lower";
    b0.nodeIndex = 1;
    KModelBone b1;
    b1.name = "upper";
    b1.nodeIndex = 2;
    b1.parentBoneIndex = 0;
    b1.inverseBindMatrix[13] = -1.0f;
    m.bones = {b0, b1};
    return m;
}

AnimationClipData makeBend()
{
    AnimationClipData clip;
    clip.name = "Bend";
    clip.duration = 1.0f;
    AnimationNodeTrack track;
    track.nodeName = "upper";
    track.nodeIndex = 2;
    for (float t : {0.0f, 0.5f, 1.0f}) {
        const float a = 3.14159265f / 2 * t;
        track.rotationKeys.push_back({t, {0.0f, 0.0f, std::sin(a / 2), std::cos(a / 2)}});
    }
    clip.tracks.push_back(track);
    return clip;
}

int findBone(const KModelData& m, const char* name)
{
    for (size_t i = 0; i < m.bones.size(); ++i)
        if (m.bones[i].name == name) return int(i);
    return -1;
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir tmp;
    const QString contentRoot = tmp.path();
    const QString clipPath = QStringLiteral("anims/bar_Bend.kanim");
    const QString modelPath = tmp.filePath("model.kmodel");
    EXPECT(SaveKModelFile(modelPath.toUtf8().constData(), makeModel()));
    EXPECT(QDir(contentRoot).mkpath("anims"));
    EXPECT(SaveAnimationClipFile(QDir(contentRoot).filePath(clipPath).toUtf8().constData(), makeBend()));

    kme::ModelDocument doc;
    QString error;
    EXPECT(doc.load(modelPath, &error));
    const int upper = findBone(doc.model(), "upper");
    EXPECT(upper >= 0);

    KModelSocket socket;
    socket.name = "tip";
    socket.boneIndex = upper;
    socket.translation[1] = 1.0f;
    doc.edit("Add socket", [&](KModelData& m) { m.sockets.push_back(socket); });
    KModelCollider collider;
    collider.shape = KModelColliderShape::Capsule;
    collider.boneIndex = upper;
    doc.edit("Add collider", [&](KModelData& m) { m.colliders.push_back(collider); });
    for (int i = 0; i < 3; ++i)
        doc.edit("Move", [&](KModelData& m) { m.sockets[0].translation[0] += 0.1f; }, "socket:0:pos");
    EXPECT(doc.undoStack()->count() == 3);   // the three moves merge
    doc.undoStack()->undo();
    EXPECT(std::fabs(doc.model().sockets[0].translation[0]) < 1e-6f);
    doc.undoStack()->redo();
    EXPECT(std::fabs(doc.model().sockets[0].translation[0] - 0.3f) < 1e-5f);

    KModelChild child;
    child.name = "gun";
    child.modelPath = "models/gun.kmodel";
    child.animationPath = "anims/gun_idle.kanim";
    child.boneIndex = upper;
    doc.edit("Add child model", [&](KModelData& m) { m.children.push_back(child); });
    doc.edit("Set default material", [&](KModelData& m) { m.defaultMaterial = "materials/skin.mat"; });
    doc.setSelection({kme::Selection::Child, 0});
    doc.undoStack()->undo();
    EXPECT(doc.model().defaultMaterial.empty());
    doc.undoStack()->undo();
    EXPECT(doc.model().children.empty());
    EXPECT(doc.selection().kind == kme::Selection::None);
    doc.undoStack()->redo();
    doc.undoStack()->redo();
    EXPECT(doc.model().children.size() == 1 && doc.model().defaultMaterial == "materials/skin.mat");
    EXPECT(doc.isModified());
    EXPECT(doc.save(&error));
    EXPECT(!doc.isModified());

    KModelData saved;
    EXPECT(LoadKModelFile(modelPath.toUtf8().constData(), saved));
    EXPECT(saved.sockets.size() == 1 && saved.sockets[0].name == "tip");
    EXPECT(saved.colliders.size() == 1 && saved.colliders[0].shape == KModelColliderShape::Capsule);
    EXPECT(saved.meshes.size() == doc.model().meshes.size());
    EXPECT(saved.children.size() == 1 && saved.children[0].modelPath == "models/gun.kmodel" && saved.children[0].boneIndex == upper &&
           saved.children[0].animationPath == "anims/gun_idle.kanim");
    EXPECT(saved.defaultMaterial == "materials/skin.mat");

    kme::GraphModel graph;
    const int clip = graph.addNode("Clip", {0, 0});
    const int output = graph.addNode("Output", {300, 0});
    const int speed = graph.addNode("FloatParam", {-300, 0});
    EXPECT(graph.addNode("Output", {0, 0}) < 0);
    graph.setParam(clip, "Clip", clipPath);
    graph.setParam(speed, "Name", "Speed");
    graph.setParam(speed, "Default", 1.0);
    EXPECT(!graph.canConnect(speed, 0, output, 0));
    EXPECT(graph.addConnection(clip, 0, output, 0) >= 0);
    EXPECT(graph.addConnection(speed, 0, clip, 0) >= 0);
    graph.setModelPath("meshes/bar.kmodel");

    const QString graphPath = tmp.filePath("bar.kanimgraph");
    std::string err;
    EXPECT(SaveAnimBlendTreeFile(graphPath.toUtf8().constData(), graph.toBlendTree(), &err));
    AnimBlendTreeData loaded;
    EXPECT(LoadAnimBlendTreeFile(graphPath.toUtf8().constData(), loaded, &err));
    kme::GraphModel reloaded;
    reloaded.loadBlendTree(loaded);
    EXPECT(reloaded.nodeCount() == 3);
    EXPECT(reloaded.connections().size() == 2);
    EXPECT(reloaded.modelPath() == "meshes/bar.kmodel");
    EXPECT(reloaded.param(clip, "Clip").toString() == clipPath);

    AnimationClipData clipData;
    EXPECT(LoadAnimationClipFile(QDir(contentRoot).filePath(clipPath).toUtf8().constData(), clipData));
    AnimBlendTreeInstance instance;
    EXPECT(instance.Build(reloaded.toBlendTree(), &err));
    const AnimBlendTreeInstance::ClipResolver clips = [&](const std::string& p) {
        return p == clipPath.toStdString() ? &clipData : nullptr;
    };
    std::vector<AnimationTransform> pose;
    instance.SetFloat("Speed", 0.5f);
    EXPECT(instance.Evaluate(saved, 1.0f, clips, pose));   // 0.5 s into a 1 s clip: 45 degrees

    std::vector<float> world;
    BuildAnimationWorldMatrices(saved, pose, world);
    float tip[16];
    const KModelSocket& s = saved.sockets[0];
    BuildBoneAttachmentMatrix(saved, world, s.boneIndex, s.translation, s.rotation, s.scale, tip);
    // Upper bone sits at y = 1 and bends about Z: (0.3, 1, 0) turned 45 degrees, plus (0, 1, 0).
    const float c = std::cos(3.14159265f / 4), sn = std::sin(3.14159265f / 4);
    std::printf("socket at %.3f %.3f %.3f\n", tip[12], tip[13], tip[14]);
    EXPECT(std::fabs(tip[12] - (0.3f * c - 1.0f * sn)) < 1e-3f);
    EXPECT(std::fabs(tip[13] - (1.0f + 0.3f * sn + 1.0f * c)) < 1e-3f);
    EXPECT(instance.GetNodeWeight(clip) > 0.99f);

    std::printf(g_failures ? "%d check(s) failed\n" : "all checks passed\n", g_failures);
    return g_failures ? 1 : 0;
}
