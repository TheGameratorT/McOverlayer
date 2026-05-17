#pragma once

#include "core/Types.h"
#include <QMainWindow>
#include <QHash>

class TextureCanvas;
class QListWidget;
class QComboBox;
class QLineEdit;
class QSpinBox;
class QLabel;
class QPushButton;
class QAction;

class RegionEditorWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit RegionEditorWindow(QWidget *parent = nullptr);

private slots:
    void onBrowseDir();
    void onBrowseTexBase();
    void onEntitySelected(int index);
    void onTextureSelected(int index);
    void onRegionAdded(const Core::EntityRegion &region);
    void onRegionSelected(int index);
    void onRemoveRegion();
    void onFlipChanged(int index);
    void onRotateChanged(int index);
    void onNameChanged(const QString &text);
    void onTexWidthChanged(int value);
    void onTexHeightChanged(int value);
    void onAddTexture();
    void onRemoveTexture();
    void onSave();

private:
    void build();
    void loadDir(const QString &dirPath);
    void refreshEntityList();
    void refreshTextureList();
    void refreshRegionList();
    void selectRegionInList(int index);
    bool saveEntityJson(const QString &entityId);
    QString resolveTexPath(const QString &relPath) const;

    QString     m_dirPath;
    QString     m_texBasePath;

    QHash<QString, Core::EntityData> m_entities;
    QStringList m_entityIds;

    QString m_currentEntityId;
    int     m_currentRegionIndex = -1;

    TextureCanvas *m_canvas        = nullptr;
    QListWidget   *m_entityList    = nullptr;
    QListWidget   *m_textureList   = nullptr;
    QListWidget   *m_regionList    = nullptr;

    QLabel     *m_texBaseLabel = nullptr;
    QSpinBox   *m_texWSpin     = nullptr;
    QSpinBox   *m_texHSpin     = nullptr;
    QLineEdit  *m_nameEdit     = nullptr;
    QComboBox  *m_flipCombo    = nullptr;
    QComboBox  *m_rotCombo     = nullptr;
    QPushButton*m_removeBtn    = nullptr;
    QPushButton*m_addTexBtn    = nullptr;
    QPushButton*m_removeTexBtn = nullptr;
    QAction    *m_saveAct      = nullptr;
    QLabel     *m_statusLabel  = nullptr;
};
