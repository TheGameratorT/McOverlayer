#pragma once

#include "core/MappingConfig.h"
#include <QDialog>
#include <QThread>
#include <QCloseEvent>
#include <QElapsedTimer>
#include <QTimer>

class QTextEdit;
class QProgressBar;
class QPushButton;
class QCheckBox;
class QLineEdit;
class QLabel;

// Background worker that runs the overlay pipeline.
// If config.outputDir is set, reads from config.textureDir and writes to outputDir,
// then copies non-image files. Otherwise modifies textures in-place.
class ApplyWorker : public QThread {
    Q_OBJECT
public:
    explicit ApplyWorker(const Core::MappingConfig &config, QObject *parent = nullptr);
    void requestStop();

signals:
    void logMessage(QString msg);
    void progressChanged(int done, int total);
    void finished(bool success);

protected:
    void run() override;

private:
    Core::MappingConfig m_config;
    QAtomicInt m_stop{0};
};

// Dialog that runs the overlay pipeline with a live progress bar and log.
// Supports in-place mode (no output dir) and reference mode (output dir set).
class ApplyDialog : public QDialog {
    Q_OBJECT
public:
    explicit ApplyDialog(const Core::MappingConfig &config, QWidget *parent = nullptr);

private slots:
    void onRun();
    void onStop();
    void onProgress(int done, int total);
    void onFinished(bool success);
    void browseOutputDir();

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void build();

    Core::MappingConfig m_config;
    ApplyWorker        *m_worker = nullptr;

    QLineEdit    *m_outEdit   = nullptr;
    QPushButton  *m_outBtn    = nullptr;
    QTextEdit    *m_log       = nullptr;
    QProgressBar *m_progress  = nullptr;
    QLabel       *m_elapsed   = nullptr;
    QPushButton  *m_runBtn    = nullptr;
    QPushButton  *m_stopBtn   = nullptr;

    QElapsedTimer m_elapsedTimer;
    QTimer        m_tickTimer;
};
