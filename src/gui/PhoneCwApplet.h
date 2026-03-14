#pragma once

#include <QWidget>

class QPushButton;
class QLabel;
class QSlider;
class QComboBox;

namespace AetherSDR {

class HGauge;
class TransmitModel;

// P/CW applet — phone/CW microphone controls matching the SmartSDR P/CW panel.
//
// Layout (top to bottom):
//  - Title bar: "P/CW"
//  - Level (mic peak) horizontal gauge (-40 to -10 dB)
//  - Compression horizontal gauge (-25 to 0 dB)
//  - Mic profile dropdown
//  - Mic source dropdown + mic level slider + +ACC button
//  - PROC button + NOR/DX/DX+ 3-position slider + DAX button
//  - MON button + monitor volume slider
class PhoneCwApplet : public QWidget {
    Q_OBJECT

public:
    explicit PhoneCwApplet(QWidget* parent = nullptr);

    void setTransmitModel(TransmitModel* model);

public slots:
    void updateMeters(float micLevel, float compLevel,
                      float micPeak, float compPeak);

private:
    void buildUI();
    void syncFromModel();

    TransmitModel* m_model{nullptr};

    // Gauges
    HGauge* m_levelGauge{nullptr};
    HGauge* m_compGauge{nullptr};

    // Mic profile
    QComboBox* m_micProfileCombo{nullptr};

    // Mic source + level
    QComboBox*   m_micSourceCombo{nullptr};
    QSlider*     m_micLevelSlider{nullptr};
    QLabel*      m_micLevelLabel{nullptr};
    QPushButton* m_accBtn{nullptr};

    // Processor
    QPushButton* m_procBtn{nullptr};
    QSlider*     m_procSlider{nullptr};   // 3-position: 0=NOR, 1=DX, 2=DX+
    QPushButton* m_daxBtn{nullptr};

    // Monitor
    QPushButton* m_monBtn{nullptr};
    QSlider*     m_monSlider{nullptr};
    QLabel*      m_monLabel{nullptr};

    bool m_updatingFromModel{false};

    // Client-side peak hold with slow decay for compression gauge
    float m_compHeld{0.0f};          // current held compression value (0 = empty)
    static constexpr float kCompDecayRate = 0.5f;  // dB per meter update toward 0
};

} // namespace AetherSDR
