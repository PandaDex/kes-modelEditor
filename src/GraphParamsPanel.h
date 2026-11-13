#pragma once

#include <QWidget>

class QVBoxLayout;

namespace kme {

class PreviewState;

// preview only, nothing here gets saved
class GraphParamsPanel : public QWidget {
    Q_OBJECT
public:
    explicit GraphParamsPanel(PreviewState* preview, QWidget* parent = nullptr);

private:
    void rebuild();

    PreviewState* m_preview;
    QVBoxLayout* m_root;
    QWidget* m_content = nullptr;
};

} // namespace kme
