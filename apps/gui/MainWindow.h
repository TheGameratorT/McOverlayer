#pragma once

#include "core/MappingConfig.h"
#include "core/Types.h"
#include <QMainWindow>
#include <QList>

class ConfigPanel;
class AssignmentCard;
class ThumbnailLoader;
class SeedSearchDialog;
class QScrollArea;
class QFlowWidget;
class QLineEdit;
class QComboBox;
class QSpinBox;
class QPushButton;
class QLabel;
class QWidget;
class QHBoxLayout;
class QTimer;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void onRandomize();
    void onCopySeed();
    void onFilterChanged();
    void onConfigChanged(const Core::MappingConfig &config);
    void onOverlayLookup();
    void onSeedSearch();
    void onPresets();
    void onApply();
    void onAbout();
    void onThumbnailLoaded(int index, const QImage &image);

private:
    void build();
    void rebuild();
    void applyFilter();
    void saveConfig();
    void updateStatus();

    Core::MappingConfig m_config;
    Core::BuildResult   m_buildResult;

    // Filtered/visible subset
    QList<int> m_visibleIndices;

    ConfigPanel      *m_configPanel      = nullptr;
    QWidget          *m_cardsContainer  = nullptr;
    QScrollArea      *m_scrollArea      = nullptr;
    ThumbnailLoader  *m_loader          = nullptr;
    SeedSearchDialog *m_seedSearchDlg   = nullptr;

    QList<AssignmentCard *> m_cards;

    // Toolbar widgets
    QLineEdit  *m_seedEdit    = nullptr;
    QLineEdit  *m_filterEdit  = nullptr;
    QComboBox  *m_typeCombo   = nullptr;
    QSpinBox   *m_maxSpin     = nullptr;
    QLabel     *m_statusLabel = nullptr;
    QTimer     *m_filterTimer = nullptr;
};
