#include "SeedSearchDialog.h"
#include "core/Hash.h"
#include "core/FileUtils.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QPushButton>
#include <QListWidget>
#include <QListWidgetItem>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QRandomGenerator>

#include <algorithm>
#include <thread>
#include <vector>

// ---------------------------------------------------------------------------
// ConstraintRow
// ---------------------------------------------------------------------------

ConstraintRow::ConstraintRow(QWidget *parent) : QWidget(parent)
{
    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(4);
    lay->addWidget(new QLabel(QStringLiteral("Texture:"), this));
    m_texEdit = new QLineEdit(this);
    m_texEdit->setPlaceholderText(QStringLiteral("rel. path, e.g. block/grass_block_top.png"));
    lay->addWidget(m_texEdit, 1);
    lay->addWidget(new QLabel(QStringLiteral("→  Overlay:"), this));
    m_ovEdit = new QLineEdit(this);
    m_ovEdit->setPlaceholderText(QStringLiteral("partial name, e.g. sakura.png"));
    lay->addWidget(m_ovEdit, 1);
    auto *rmBtn = new QPushButton(QStringLiteral("✕"), this);
    rmBtn->setFixedWidth(24);
    connect(rmBtn, &QPushButton::clicked, this, [this]{ emit removeRequested(this); });
    lay->addWidget(rmBtn);
}

QString ConstraintRow::textureFilter() const { return m_texEdit->text().trimmed(); }
QString ConstraintRow::overlayFilter() const { return m_ovEdit->text().trimmed(); }

// ---------------------------------------------------------------------------
// SeedSearchWorker
// ---------------------------------------------------------------------------

SeedSearchWorker::SeedSearchWorker(
    const Core::MappingConfig &config,
    const QList<QPair<QString,QString>> &constraints,
    const QStringList &targets,
    const QStringList &overlays,
    int maxTries,
    QObject *parent)
    : QThread(parent)
    , m_maxTries(maxTries)
{
    // Pre-compute per-overlay hash suffixes and lowercase rel paths.
    // Separating these from the seed prefix lets check() skip re-hashing
    // the seed string for every overlay on every call.
    m_ovData.reserve(overlays.size());
    for (const QString &ov : overlays) {
        const QString rel = Core::relPath(config.overlayDir, ov).replace(QLatin1Char('\\'), QLatin1Char('/'));
        m_ovData.append({QStringLiteral(":overlay:") + rel, rel.toLower()});
    }

    // Pre-compute constraint target hash suffixes
    for (const auto &[texFilter, ovFilter] : constraints) {
        const QString tf = QString(texFilter).replace(QLatin1Char('\\'), QLatin1Char('/')).toLower();

        ResolvedConstraint c;
        c.ovFilter = QString(ovFilter).replace(QLatin1Char('\\'), QLatin1Char('/')).toLower();

        for (const QString &t : targets) {
            const QString rel = Core::relPath(config.textureDir, t).replace(QLatin1Char('\\'), QLatin1Char('/'));
            if (tf.isEmpty() || rel.toLower().contains(tf))
                c.targetSuffixes.append(QLatin1Char(':') + rel);
        }
        m_resolved.append(std::move(c));
    }
}

void SeedSearchWorker::stop() { m_stop.storeRelaxed(1); }

void SeedSearchWorker::run()
{
    m_triedCount.storeRelaxed(0);
    const int N = qMax(1, QThread::idealThreadCount());

    std::vector<std::thread> workers;
    workers.reserve(static_cast<size_t>(N));
    for (int i = 0; i < N; ++i)
        workers.emplace_back(&SeedSearchWorker::workerThread, this,
                             QRandomGenerator::securelySeeded());

    for (auto &w : workers)
        w.join();

    emit progress(qMin(m_triedCount.loadRelaxed(), m_maxTries));
    emit finished();
}

void SeedSearchWorker::workerThread(QRandomGenerator rng)
{
    QElapsedTimer timer;
    timer.start();

    while (!m_stop.loadRelaxed()) {
        const int idx = m_triedCount.fetchAndAddRelaxed(1);
        if (idx >= m_maxTries) break;

        const qint64 seed = static_cast<qint64>(rng.bounded(static_cast<quint32>(0xFFFFFFFFu)));
        if (check(seed))
            emit found(seed);

        // Emit progress roughly every 100 ms regardless of thread count
        if (timer.elapsed() >= 100) {
            emit progress(qMin(m_triedCount.loadRelaxed(), m_maxTries));
            timer.restart();
        }
    }
}

bool SeedSearchWorker::check(qint64 seed) const
{
    if (m_ovData.isEmpty()) return false;

    // Hash the seed prefix once; each overlay/target suffix is accumulated on top.
    // This avoids re-hashing the seed string (up to 10 chars) per overlay per check.
    const QString seedStr = QString::number(seed);
    const uint32_t seedState = Core::ringHashAccum(seedStr);

    // Build ring: (hash, overlay-index). Use std::vector to avoid Qt COW overhead.
    const int N = m_ovData.size();
    std::vector<std::pair<uint32_t, int>> ring(static_cast<size_t>(N));
    for (int i = 0; i < N; ++i)
        ring[static_cast<size_t>(i)] = {
            Core::ringHashFinal(Core::ringHashAccum(m_ovData[i].hashSuffix, seedState)), i};

    std::sort(ring.begin(), ring.end(),
              [](const auto &a, const auto &b){ return a.first < b.first; });

    // Check each constraint: at least one matching target must land on a
    // qualifying overlay.
    for (const auto &c : m_resolved) {
        if (c.targetSuffixes.isEmpty()) return false;

        bool anyMatch = false;
        for (const QString &suf : c.targetSuffixes) {
            const uint32_t pos = Core::ringHashFinal(Core::ringHashAccum(suf, seedState));

            // Binary search for first ring entry with hash >= pos (ring wrap-around)
            auto it = std::lower_bound(ring.begin(), ring.end(),
                                       std::make_pair(pos, 0),
                                       [](const auto &a, const auto &b){ return a.first < b.first; });
            const int idx = (it != ring.end()) ? it->second : ring.front().second;

            if (m_ovData[idx].relLower.contains(c.ovFilter)) { anyMatch = true; break; }
        }
        if (!anyMatch) return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// SeedSearchDialog
// ---------------------------------------------------------------------------

SeedSearchDialog::SeedSearchDialog(
    const Core::MappingConfig &config,
    const QStringList &targets,
    const QStringList &overlays,
    QWidget *parent)
    : QDialog(parent)
    , m_config(config)
    , m_targets(targets)
    , m_overlays(overlays)
{
    setWindowTitle(QStringLiteral("Seed Search"));
    setMinimumSize(660, 500);
    build();
}

void SeedSearchDialog::build()
{
    auto *layout = new QVBoxLayout(this);

    // Constraints group
    auto *cGrp = new QGroupBox(QStringLiteral("Texture → Overlay Constraints"), this);
    auto *cLay = new QVBoxLayout(cGrp);

    m_rowsWidget = new QWidget(this);
    m_rowsLayout = new QVBoxLayout(m_rowsWidget);
    m_rowsLayout->setContentsMargins(0, 0, 0, 0);
    m_rowsLayout->setSpacing(2);
    cLay->addWidget(m_rowsWidget);

    auto *addBtn = new QPushButton(QStringLiteral("+ Add Constraint"), this);
    addBtn->setMaximumWidth(130);
    connect(addBtn, &QPushButton::clicked, this, &SeedSearchDialog::addRow);
    cLay->addWidget(addBtn, 0, Qt::AlignLeft);
    layout->addWidget(cGrp);

    // Search controls
    auto *ctrl = new QHBoxLayout();
    ctrl->addWidget(new QLabel(QStringLiteral("Max seeds:"), this));
    m_maxSpin = new QSpinBox(this);
    m_maxSpin->setRange(100, 10'000'000);
    m_maxSpin->setValue(100'000);
    m_maxSpin->setSingleStep(10'000);
    m_maxSpin->setFixedWidth(90);
    ctrl->addWidget(m_maxSpin);
    ctrl->addStretch();
    m_searchBtn = new QPushButton(QStringLiteral("▶ Search"), this);
    connect(m_searchBtn, &QPushButton::clicked, this, &SeedSearchDialog::onStart);
    ctrl->addWidget(m_searchBtn);
    m_stopBtn = new QPushButton(QStringLiteral("■ Stop"), this);
    m_stopBtn->setEnabled(false);
    connect(m_stopBtn, &QPushButton::clicked, this, &SeedSearchDialog::onStop);
    ctrl->addWidget(m_stopBtn);
    layout->addLayout(ctrl);

    m_progressLbl = new QLabel(QStringLiteral("Seeds tried: 0"), this);
    layout->addWidget(m_progressLbl);

    auto *rGrp = new QGroupBox(QStringLiteral("Found Seeds"), this);
    auto *rLay = new QVBoxLayout(rGrp);
    m_resultsList = new QListWidget(this);
    connect(m_resultsList, &QListWidget::itemSelectionChanged,
            this, [this]{ m_applyBtn->setEnabled(!m_resultsList->selectedItems().isEmpty()); });
    rLay->addWidget(m_resultsList);
    layout->addWidget(rGrp, 1);

    auto *btns = new QDialogButtonBox(this);
    m_applyBtn = btns->addButton(QStringLiteral("Apply Selected Seed"), QDialogButtonBox::AcceptRole);
    m_applyBtn->setEnabled(false);
    connect(m_applyBtn, &QPushButton::clicked, this, &SeedSearchDialog::onApply);
    auto *closeBtn = btns->addButton(QStringLiteral("Close"), QDialogButtonBox::RejectRole);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);
    layout->addWidget(btns);

    addRow();
}

void SeedSearchDialog::addRow()
{
    auto *row = new ConstraintRow(m_rowsWidget);
    connect(row, &ConstraintRow::removeRequested, this, &SeedSearchDialog::removeRow);
    m_rows.append(row);
    m_rowsLayout->addWidget(row);
}

void SeedSearchDialog::removeRow(ConstraintRow *row)
{
    if (m_rows.size() <= 1) return;
    m_rows.removeOne(row);
    m_rowsLayout->removeWidget(row);
    row->deleteLater();
}

QList<QPair<QString,QString>> SeedSearchDialog::constraints() const
{
    QList<QPair<QString,QString>> result;
    for (const ConstraintRow *row : m_rows)
        result.append({row->textureFilter(), row->overlayFilter()});
    return result;
}

void SeedSearchDialog::onStart()
{
    const auto cs = constraints();
    if (std::all_of(cs.cbegin(), cs.cend(), [](const auto &p){ return p.first.isEmpty() && p.second.isEmpty(); }))
        return;

    m_resultsList->clear();
    m_searchBtn->setEnabled(false);
    m_stopBtn->setEnabled(true);
    m_progressLbl->setText(QStringLiteral("Seeds tried: 0"));

    m_worker = new SeedSearchWorker(m_config, cs, m_targets, m_overlays, m_maxSpin->value(), this);
    connect(m_worker, &SeedSearchWorker::progress,
            this, [this](int n){ m_progressLbl->setText(QStringLiteral("Seeds tried: ") + QString::number(n)); });
    connect(m_worker, &SeedSearchWorker::found,    this, &SeedSearchDialog::onFound);
    connect(m_worker, &SeedSearchWorker::finished, this, &SeedSearchDialog::onFinished);
    m_worker->start();
}

void SeedSearchDialog::onStop()
{
    if (m_worker) m_worker->stop();
}

void SeedSearchDialog::onFound(qint64 seed)
{
    m_resultsList->addItem(new QListWidgetItem(QString::number(seed), m_resultsList));
    m_applyBtn->setEnabled(true);
}

void SeedSearchDialog::onFinished()
{
    m_searchBtn->setEnabled(true);
    m_stopBtn->setEnabled(false);
}

void SeedSearchDialog::onApply()
{
    const auto items = m_resultsList->selectedItems();
    if (items.isEmpty()) return;
    emit seedSelected(items.first()->text().toLongLong());
    accept();
}

void SeedSearchDialog::updatePaths(const QStringList &targets, const QStringList &overlays)
{
    m_targets  = targets;
    m_overlays = overlays;
}

void SeedSearchDialog::closeEvent(QCloseEvent *event)
{
    if (m_worker && m_worker->isRunning()) {
        m_worker->stop();
        m_worker->wait();
    }
    QDialog::closeEvent(event);
}
