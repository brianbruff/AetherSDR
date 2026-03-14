#include "PhoneCwApplet.h"
#include "HGauge.h"
#include "models/TransmitModel.h"

#include <QPushButton>
#include <QLabel>
#include <QSlider>
#include <QComboBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSignalBlocker>
#include <QPainter>
#include <QDir>
#include <QStandardPaths>

namespace AetherSDR {

// ── Shared gradient title bar (matches AppletPanel / TxApplet style) ─────────

static QWidget* appletTitleBar(const QString& text)
{
    auto* bar = new QWidget;
    bar->setFixedHeight(16);
    bar->setStyleSheet(
        "QWidget { background: qlineargradient(x1:0,y1:0,x2:0,y2:1,"
        "stop:0 #3a4a5a, stop:0.5 #2a3a4a, stop:1 #1a2a38); "
        "border-bottom: 1px solid #0a1a28; }");

    auto* lbl = new QLabel(text, bar);
    lbl->setStyleSheet("QLabel { background: transparent; color: #8aa8c0; "
                       "font-size: 10px; font-weight: bold; }");
    lbl->setGeometry(6, 1, 240, 14);
    return bar;
}

// ── Style constants ──────────────────────────────────────────────────────────

static constexpr const char* kSliderStyle =
    "QSlider::groove:horizontal { height: 4px; background: #203040; border-radius: 2px; }"
    "QSlider::handle:horizontal { width: 10px; height: 10px; margin: -3px 0;"
    "background: #00b4d8; border-radius: 5px; }";

static const QString kBlueActive =
    "QPushButton:checked { background-color: #0070c0; color: #ffffff; "
    "border: 1px solid #0090e0; }";

static const QString kGreenActive =
    "QPushButton:checked { background-color: #006040; color: #00ff88; "
    "border: 1px solid #00a060; }";

static constexpr const char* kButtonBase =
    "QPushButton { background: #1a3a5a; border: 1px solid #205070; "
    "border-radius: 3px; color: #c8d8e8; font-size: 10px; font-weight: bold; }"
    "QPushButton:hover { background: #204060; }";

// Generate a small down-arrow PNG for combo boxes (Qt stylesheets need a file).
static QString comboArrowPath()
{
    static QString path;
    if (!path.isEmpty()) return path;

    path = QDir::temp().filePath("aethersdr_combo_arrow.png");
    QPixmap pm(8, 6);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0x8a, 0xa8, 0xc0));
    const QPointF tri[] = {{0, 0}, {8, 0}, {4, 6}};
    p.drawPolygon(tri, 3);
    p.end();
    pm.save(path, "PNG");
    return path;
}

static QString comboStyleStr()
{
    return QString(
        "QComboBox { background: #1a2a3a; border: 1px solid #205070; border-radius: 3px;"
        "color: #c8d8e8; font-size: 10px; padding: 1px 4px; }"
        "QComboBox::drop-down { border-left: 1px solid #205070; width: 18px;"
        "subcontrol-origin: padding; subcontrol-position: center right; }"
        "QComboBox::down-arrow { image: url(%1); width: 8px; height: 6px; }"
        "QComboBox QAbstractItemView { background: #1a2a3a; color: #c8d8e8;"
        "selection-background-color: #00b4d8; }")
        .arg(comboArrowPath());
}

// ── PhoneCwApplet ────────────────────────────────────────────────────────────

PhoneCwApplet::PhoneCwApplet(QWidget* parent)
    : QWidget(parent)
{
    hide();
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    buildUI();
}

void PhoneCwApplet::buildUI()
{
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // Title bar
    outer->addWidget(appletTitleBar("P/CW"));

    // Body with margins
    auto* body = new QWidget;
    auto* vbox = new QVBoxLayout(body);
    vbox->setContentsMargins(4, 2, 4, 2);
    vbox->setSpacing(2);

    // ── Level gauge (mic peak, dBFS: -40 to +10) ────────────────────────────
    m_levelGauge = new HGauge(-40.0f, 10.0f, 0.0f, "Level", "dB",
        {{-40, "-40dB"}, {-30, "-30"}, {-20, "-20"}, {-10, "-10"}, {0, "0"}, {5, "+5"}, {10, "+10"}},
        nullptr, -10.0f);
    vbox->addWidget(m_levelGauge);

    // ── Compression gauge (dB: -25 to 0, fills right-to-left) ────────────────
    m_compGauge = new HGauge(-25.0f, 0.0f, 1.0f, "Compression", "",
        {{-25, "-25dB"}, {-20, "-20"}, {-15, "-15"}, {-10, "-10"}, {-5, "-5"}, {0, "0"}});
    m_compGauge->setReversed(true);
    vbox->addWidget(m_compGauge);
    vbox->addSpacing(4);

    // ── Mic profile dropdown ─────────────────────────────────────────────────
    m_micProfileCombo = new QComboBox;
    m_micProfileCombo->setFixedHeight(22);
    m_micProfileCombo->setStyleSheet(comboStyleStr());
    connect(m_micProfileCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
        if (!m_updatingFromModel && m_model) {
            QString name = m_micProfileCombo->currentText();
            if (!name.isEmpty())
                m_model->loadMicProfile(name);
        }
    });
    vbox->addWidget(m_micProfileCombo);

    // ── Mic source + level slider + +ACC ─────────────────────────────────────
    {
        auto* row = new QHBoxLayout;
        row->setSpacing(4);

        m_micSourceCombo = new QComboBox;
        m_micSourceCombo->setFixedWidth(55);
        m_micSourceCombo->setFixedHeight(22);
        m_micSourceCombo->setStyleSheet(comboStyleStr());
        // Default options (overridden by mic list from radio if available)
        m_micSourceCombo->addItems({"MIC", "BAL", "LINE", "ACC", "PC"});
        connect(m_micSourceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int) {
            if (!m_updatingFromModel && m_model) {
                m_model->setMicSelection(m_micSourceCombo->currentText());
            }
        });
        row->addWidget(m_micSourceCombo);

        m_micLevelSlider = new QSlider(Qt::Horizontal);
        m_micLevelSlider->setRange(0, 100);
        m_micLevelSlider->setStyleSheet(kSliderStyle);
        row->addWidget(m_micLevelSlider, 1);

        m_micLevelLabel = new QLabel("50");
        m_micLevelLabel->setStyleSheet("QLabel { color: #c8d8e8; font-size: 10px; }");
        m_micLevelLabel->setFixedWidth(22);
        m_micLevelLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        row->addWidget(m_micLevelLabel);

        m_accBtn = new QPushButton("+ACC");
        m_accBtn->setCheckable(true);
        m_accBtn->setFixedHeight(22);
        m_accBtn->setFixedWidth(48);
        m_accBtn->setStyleSheet(QString(kButtonBase) + kGreenActive);
        row->addWidget(m_accBtn);

        connect(m_micLevelSlider, &QSlider::valueChanged, this, [this](int v) {
            m_micLevelLabel->setText(QString::number(v));
            if (!m_updatingFromModel && m_model)
                m_model->setMicLevel(v);
        });

        connect(m_accBtn, &QPushButton::toggled, this, [this](bool on) {
            if (!m_updatingFromModel && m_model)
                m_model->setMicAcc(on);
        });

        vbox->addLayout(row);
    }

    // ── PROC + NOR/DX/DX+ slider + DAX ──────────────────────────────────────
    {
        auto* row = new QHBoxLayout;
        row->setSpacing(4);

        m_procBtn = new QPushButton("PROC");
        m_procBtn->setCheckable(true);
        m_procBtn->setFixedHeight(22);
        m_procBtn->setFixedWidth(48);
        m_procBtn->setStyleSheet(QString(kButtonBase) + kGreenActive);
        row->addWidget(m_procBtn);

        // 3-position slider with NOR / DX / DX+ labels
        auto* procGroup = new QWidget;
        auto* procVbox = new QVBoxLayout(procGroup);
        procVbox->setContentsMargins(0, 0, 0, 0);
        procVbox->setSpacing(0);

        // Labels row
        auto* labelsRow = new QHBoxLayout;
        labelsRow->setContentsMargins(0, 0, 0, 0);
        auto* norLbl = new QLabel("NOR");
        auto* dxLbl = new QLabel("DX");
        auto* dxPlusLbl = new QLabel("DX+");
        const QString tickLabelStyle = "QLabel { color: #c8d8e8; font-size: 8px; }";
        norLbl->setStyleSheet(tickLabelStyle);
        dxLbl->setStyleSheet(tickLabelStyle);
        dxPlusLbl->setStyleSheet(tickLabelStyle);
        norLbl->setAlignment(Qt::AlignLeft | Qt::AlignBottom);
        dxLbl->setAlignment(Qt::AlignCenter | Qt::AlignBottom);
        dxPlusLbl->setAlignment(Qt::AlignRight | Qt::AlignBottom);
        labelsRow->addWidget(norLbl);
        labelsRow->addWidget(dxLbl);
        labelsRow->addWidget(dxPlusLbl);
        procVbox->addLayout(labelsRow);

        m_procSlider = new QSlider(Qt::Horizontal);
        m_procSlider->setRange(0, 2);
        m_procSlider->setTickInterval(1);
        m_procSlider->setTickPosition(QSlider::NoTicks);
        m_procSlider->setPageStep(1);
        m_procSlider->setFixedHeight(14);
        m_procSlider->setStyleSheet(kSliderStyle);
        procVbox->addWidget(m_procSlider);

        row->addWidget(procGroup, 1);

        m_daxBtn = new QPushButton("DAX");
        m_daxBtn->setCheckable(true);
        m_daxBtn->setFixedHeight(22);
        m_daxBtn->setFixedWidth(48);
        m_daxBtn->setStyleSheet(QString(kButtonBase) + kBlueActive);
        row->addWidget(m_daxBtn);

        connect(m_procBtn, &QPushButton::toggled, this, [this](bool on) {
            if (!m_updatingFromModel && m_model)
                m_model->setSpeechProcessorEnable(on);
        });

        connect(m_procSlider, &QSlider::valueChanged, this, [this](int pos) {
            if (!m_updatingFromModel && m_model) {
                // Map 3 positions to speech_processor_level: 0=NOR, 50=DX, 100=DX+
                static constexpr int kLevels[] = {0, 50, 100};
                m_model->setSpeechProcessorLevel(kLevels[qBound(0, pos, 2)]);
            }
        });

        connect(m_daxBtn, &QPushButton::toggled, this, [this](bool on) {
            if (!m_updatingFromModel && m_model)
                m_model->setDax(on);
        });

        vbox->addLayout(row);
    }

    // ── MON button + monitor volume slider ───────────────────────────────────
    {
        auto* row = new QHBoxLayout;
        row->setSpacing(4);

        m_monBtn = new QPushButton("MON");
        m_monBtn->setCheckable(true);
        m_monBtn->setFixedHeight(22);
        m_monBtn->setFixedWidth(48);
        m_monBtn->setStyleSheet(QString(kButtonBase) + kGreenActive);
        row->addWidget(m_monBtn);

        m_monSlider = new QSlider(Qt::Horizontal);
        m_monSlider->setRange(0, 100);
        m_monSlider->setStyleSheet(kSliderStyle);
        row->addWidget(m_monSlider, 1);

        m_monLabel = new QLabel("50");
        m_monLabel->setStyleSheet("QLabel { color: #c8d8e8; font-size: 10px; }");
        m_monLabel->setFixedWidth(22);
        m_monLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        row->addWidget(m_monLabel);

        connect(m_monBtn, &QPushButton::toggled, this, [this](bool on) {
            if (!m_updatingFromModel && m_model)
                m_model->setSbMonitor(on);
        });

        connect(m_monSlider, &QSlider::valueChanged, this, [this](int v) {
            m_monLabel->setText(QString::number(v));
            if (!m_updatingFromModel && m_model)
                m_model->setMonGainSb(v);
        });

        vbox->addLayout(row);
    }

    outer->addWidget(body);
}

// ── Model binding ────────────────────────────────────────────────────────────

void PhoneCwApplet::setTransmitModel(TransmitModel* model)
{
    m_model = model;
    if (!m_model) return;

    connect(m_model, &TransmitModel::micStateChanged, this, &PhoneCwApplet::syncFromModel);

    connect(m_model, &TransmitModel::micProfileListChanged, this, [this]() {
        m_updatingFromModel = true;
        const QSignalBlocker blocker(m_micProfileCombo);
        const QString current = m_micProfileCombo->currentText();
        m_micProfileCombo->clear();
        m_micProfileCombo->addItems(m_model->micProfileList());
        int idx = m_micProfileCombo->findText(m_model->activeMicProfile());
        if (idx >= 0) m_micProfileCombo->setCurrentIndex(idx);
        else if (!current.isEmpty()) {
            idx = m_micProfileCombo->findText(current);
            if (idx >= 0) m_micProfileCombo->setCurrentIndex(idx);
        }
        m_updatingFromModel = false;
    });

    connect(m_model, &TransmitModel::micInputListChanged, this, [this]() {
        m_updatingFromModel = true;
        const QSignalBlocker blocker(m_micSourceCombo);
        const QString current = m_model->micSelection();
        m_micSourceCombo->clear();
        m_micSourceCombo->addItems(m_model->micInputList());
        int idx = m_micSourceCombo->findText(current);
        if (idx >= 0) m_micSourceCombo->setCurrentIndex(idx);
        m_updatingFromModel = false;
    });

    syncFromModel();
}

void PhoneCwApplet::syncFromModel()
{
    if (!m_model) return;
    m_updatingFromModel = true;

    // Mic source combo
    {
        const QSignalBlocker blocker(m_micSourceCombo);
        int idx = m_micSourceCombo->findText(m_model->micSelection());
        if (idx >= 0) m_micSourceCombo->setCurrentIndex(idx);
    }

    // Mic level
    m_micLevelSlider->setValue(m_model->micLevel());
    m_micLevelLabel->setText(QString::number(m_model->micLevel()));

    // +ACC
    m_accBtn->setChecked(m_model->micAcc());

    // PROC
    m_procBtn->setChecked(m_model->speechProcessorEnable());

    // NOR/DX/DX+ slider — reverse map from 0-100 to 0/1/2
    {
        int level = m_model->speechProcessorLevel();
        int pos = (level <= 25) ? 0 : (level <= 75) ? 1 : 2;
        m_procSlider->setValue(pos);
    }

    // DAX
    m_daxBtn->setChecked(m_model->daxOn());

    // MON
    m_monBtn->setChecked(m_model->sbMonitor());
    m_monSlider->setValue(m_model->monGainSb());
    m_monLabel->setText(QString::number(m_model->monGainSb()));

    // Mic profile combo
    {
        const QSignalBlocker blocker(m_micProfileCombo);
        int idx = m_micProfileCombo->findText(m_model->activeMicProfile());
        if (idx >= 0) m_micProfileCombo->setCurrentIndex(idx);
    }

    m_updatingFromModel = false;
}

// ── Meter updates ────────────────────────────────────────────────────────────

void PhoneCwApplet::updateMeters(float micLevel, float compLevel,
                                  float micPeak, float compPeak)
{
    Q_UNUSED(compLevel);  // no "COMP" meter exists — only COMPPEAK

    m_levelGauge->setValue(micLevel);
    m_levelGauge->setPeakValue(micPeak);
    // During RX, COMPPEAK reads far below gauge range (e.g. -150 dBFS).
    // Only show compression when actually active.
    float comp = (compPeak < -60.0f || compPeak >= 0.0f) ? 0.0f : compPeak;

    // Client-side peak hold with slow decay — radio's COMPPEAK releases too fast.
    // comp is negative (more negative = more compression), 0 = no compression.
    if (comp < m_compHeld) {
        // More compression — snap immediately
        m_compHeld = comp;
    } else {
        // Releasing — decay slowly toward 0
        m_compHeld = qMin(0.0f, m_compHeld + kCompDecayRate);
    }
    m_compGauge->setValue(m_compHeld);
}

} // namespace AetherSDR
