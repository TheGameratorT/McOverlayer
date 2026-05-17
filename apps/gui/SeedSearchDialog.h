#pragma once

#include "core/MappingConfig.h"
#include <QDialog>
#include <QThread>
#include <QVector>
#include <QAtomicInt>
#include <QRandomGenerator>

class QCloseEvent;
class QListWidget;
class QSpinBox;
class QPushButton;
class QLabel;
class QWidget;
class QVBoxLayout;
class QLineEdit;

// Single texture→overlay constraint row.
class ConstraintRow : public QWidget {
    Q_OBJECT
public:
    explicit ConstraintRow(QWidget *parent = nullptr);

    QString textureFilter() const;
    QString overlayFilter() const;

signals:
    void removeRequested(ConstraintRow *self);

private:
    QLineEdit *m_texEdit = nullptr;
    QLineEdit *m_ovEdit  = nullptr;
};

// Background worker: brute-force random seeds against a list of constraints.
// Uses one std::thread per logical CPU core for parallel seed checking.
class SeedSearchWorker : public QThread {
    Q_OBJECT
public:
    SeedSearchWorker(
        const Core::MappingConfig &config,
        const QList<QPair<QString,QString>> &constraints,
        const QStringList &targets,
        const QStringList &overlays,
        int maxTries,
        QObject *parent = nullptr);

    void stop();

signals:
    void progress(int tried);
    void found(qint64 seed);
    void finished();

protected:
    void run() override;

private:
    bool check(qint64 seed) const;
    void workerThread(QRandomGenerator rng);

    int        m_maxTries;
    QAtomicInt m_stop{0};
    QAtomicInt m_triedCount{0};

    // Pre-computed per-overlay data for fast ring construction
    struct OverlayData {
        QString hashSuffix; // ":overlay:" + relPath — appended to seed string for ring hash
        QString relLower;   // relPath.toLower() — used for overlay filter matching
    };
    QVector<OverlayData> m_ovData;

    // Pre-computed per-constraint data
    struct ResolvedConstraint {
        QVector<QString> targetSuffixes; // ":" + relTargetPath for each matching target
        QString          ovFilter;       // lowercased overlay filter substring
    };
    QVector<ResolvedConstraint> m_resolved;
};

// Dialog: search for a seed satisfying texture→overlay constraints.
class SeedSearchDialog : public QDialog {
    Q_OBJECT
public:
    SeedSearchDialog(
        const Core::MappingConfig &config,
        const QStringList &targets,
        const QStringList &overlays,
        QWidget *parent = nullptr);

    // Update the target/overlay lists used by the next search run.
    void updatePaths(const QStringList &targets, const QStringList &overlays);

signals:
    void seedSelected(qint64 seed);

private slots:
    void addRow();
    void removeRow(ConstraintRow *row);
    void onStart();
    void onStop();
    void onFound(qint64 seed);
    void onFinished();
    void onApply();

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void build();
    QList<QPair<QString,QString>> constraints() const;

    Core::MappingConfig m_config;
    QStringList m_targets;
    QStringList m_overlays;
    SeedSearchWorker *m_worker = nullptr;

    QList<ConstraintRow *> m_rows;
    QWidget    *m_rowsWidget  = nullptr;
    QVBoxLayout*m_rowsLayout  = nullptr;
    QListWidget*m_resultsList = nullptr;
    QSpinBox   *m_maxSpin     = nullptr;
    QPushButton*m_searchBtn   = nullptr;
    QPushButton*m_stopBtn     = nullptr;
    QPushButton*m_applyBtn    = nullptr;
    QLabel     *m_progressLbl = nullptr;
};
