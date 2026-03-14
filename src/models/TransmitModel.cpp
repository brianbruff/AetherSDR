#include "TransmitModel.h"
#include <QDebug>

namespace AetherSDR {

TransmitModel::TransmitModel(QObject* parent)
    : QObject(parent)
{}

// ── Status parsing ──────────────────────────────────────────────────────────

void TransmitModel::applyTransmitStatus(const QMap<QString, QString>& kvs)
{
    bool changed = false;
    bool tuneChanged_ = false;

    if (kvs.contains("rfpower")) {
        int v = qBound(0, kvs["rfpower"].toInt(), 100);
        if (m_rfPower != v) { m_rfPower = v; changed = true; }
    }
    if (kvs.contains("tunepower")) {
        int v = qBound(0, kvs["tunepower"].toInt(), 100);
        if (m_tunePower != v) { m_tunePower = v; changed = true; }
    }
    if (kvs.contains("tune")) {
        bool v = kvs["tune"] == "1";
        if (m_tune != v) { m_tune = v; changed = true; tuneChanged_ = true; }
    }

    // ── Mic / monitor / processor keys ──────────────────────────────────────
    bool micChanged = false;

    if (kvs.contains("mic_selection")) {
        QString v = kvs["mic_selection"].toUpper();
        if (m_micSelection != v) { m_micSelection = v; micChanged = true; }
    }
    if (kvs.contains("mic_level")) {
        int v = qBound(0, kvs["mic_level"].toInt(), 100);
        if (m_micLevel != v) { m_micLevel = v; micChanged = true; }
    }
    if (kvs.contains("mic_acc")) {
        bool v = kvs["mic_acc"] == "1";
        if (m_micAcc != v) { m_micAcc = v; micChanged = true; }
    }
    if (kvs.contains("speech_processor_enable")) {
        bool v = kvs["speech_processor_enable"] == "1";
        if (m_speechProcEnable != v) { m_speechProcEnable = v; micChanged = true; }
    }
    if (kvs.contains("speech_processor_level")) {
        int v = qBound(0, kvs["speech_processor_level"].toInt(), 100);
        if (m_speechProcLevel != v) { m_speechProcLevel = v; micChanged = true; }
    }
    if (kvs.contains("compander")) {
        bool v = kvs["compander"] == "1";
        if (m_companderOn != v) { m_companderOn = v; micChanged = true; }
    }
    if (kvs.contains("compander_level")) {
        int v = qBound(0, kvs["compander_level"].toInt(), 100);
        if (m_companderLevel != v) { m_companderLevel = v; micChanged = true; }
    }
    if (kvs.contains("dax")) {
        bool v = kvs["dax"] == "1";
        if (m_daxOn != v) { m_daxOn = v; micChanged = true; }
    }
    if (kvs.contains("sb_monitor")) {
        bool v = kvs["sb_monitor"] == "1";
        if (m_sbMonitor != v) { m_sbMonitor = v; micChanged = true; }
    }
    if (kvs.contains("mon_gain_sb")) {
        int v = qBound(0, kvs["mon_gain_sb"].toInt(), 100);
        if (m_monGainSb != v) { m_monGainSb = v; micChanged = true; }
    }

    if (changed) emit stateChanged();
    if (tuneChanged_) emit tuneChanged(m_tune);
    if (micChanged) emit micStateChanged();
}

void TransmitModel::applyAtuStatus(const QMap<QString, QString>& kvs)
{
    bool changed = false;

    if (kvs.contains("status")) {
        auto s = parseAtuTuneStatus(kvs["status"]);
        if (m_atuStatus != s) { m_atuStatus = s; changed = true; }
    }
    if (kvs.contains("atu_enabled")) {
        bool v = kvs["atu_enabled"] == "1";
        if (m_atuEnabled != v) { m_atuEnabled = v; changed = true; }
    }
    if (kvs.contains("memories_enabled")) {
        bool v = kvs["memories_enabled"] == "1";
        if (m_memoriesEnabled != v) { m_memoriesEnabled = v; changed = true; }
    }
    if (kvs.contains("using_mem")) {
        bool v = kvs["using_mem"] == "1";
        if (m_usingMemory != v) { m_usingMemory = v; changed = true; }
    }

    if (changed) emit atuStateChanged();
}

void TransmitModel::setProfileList(const QStringList& profiles)
{
    if (m_profileList != profiles) {
        m_profileList = profiles;
        emit profileListChanged();
    }
}

void TransmitModel::setActiveProfile(const QString& profile)
{
    if (m_activeProfile != profile) {
        m_activeProfile = profile;
        emit stateChanged();
    }
}

// ── Commands ────────────────────────────────────────────────────────────────

void TransmitModel::setRfPower(int power)
{
    power = qBound(0, power, 100);
    emit commandReady(QString("transmit set rfpower=%1").arg(power));
}

void TransmitModel::setTunePower(int power)
{
    power = qBound(0, power, 100);
    emit commandReady(QString("transmit set tunepower=%1").arg(power));
}

void TransmitModel::startTune()
{
    emit commandReady("transmit tune 1");
}

void TransmitModel::stopTune()
{
    emit commandReady("transmit tune 0");
}

void TransmitModel::setMox(bool on)
{
    emit commandReady(QString("xmit %1").arg(on ? 1 : 0));
}

void TransmitModel::atuStart()
{
    emit commandReady("atu start");
}

void TransmitModel::atuBypass()
{
    emit commandReady("atu bypass");
}

void TransmitModel::setAtuMemories(bool on)
{
    emit commandReady(QString("atu set memories_enabled=%1").arg(on ? 1 : 0));
}

void TransmitModel::loadProfile(const QString& name)
{
    emit commandReady(QString("profile tx load \"%1\"").arg(name));
}

// ── Mic profile setters (called from RadioModel) ────────────────────────────

void TransmitModel::setMicProfileList(const QStringList& profiles)
{
    if (m_micProfileList != profiles) {
        m_micProfileList = profiles;
        emit micProfileListChanged();
    }
}

void TransmitModel::setActiveMicProfile(const QString& profile)
{
    if (m_activeMicProfile != profile) {
        m_activeMicProfile = profile;
        emit micStateChanged();
    }
}

void TransmitModel::setMicInputList(const QStringList& inputs)
{
    if (m_micInputList != inputs) {
        m_micInputList = inputs;
        emit micInputListChanged();
    }
}

// ── Mic / monitor / processor commands ──────────────────────────────────────

void TransmitModel::setMicSelection(const QString& input)
{
    emit commandReady(QString("mic input %1").arg(input.toUpper()));
}

void TransmitModel::setMicLevel(int level)
{
    level = qBound(0, level, 100);
    emit commandReady(QString("transmit set miclevel=%1").arg(level));
}

void TransmitModel::setMicAcc(bool on)
{
    emit commandReady(QString("mic acc %1").arg(on ? 1 : 0));
}

void TransmitModel::setSpeechProcessorEnable(bool on)
{
    emit commandReady(QString("transmit set speech_processor_enable=%1").arg(on ? 1 : 0));
}

void TransmitModel::setSpeechProcessorLevel(int level)
{
    level = qBound(0, level, 100);
    emit commandReady(QString("transmit set speech_processor_level=%1").arg(level));
}

void TransmitModel::setDax(bool on)
{
    emit commandReady(QString("transmit set dax=%1").arg(on ? 1 : 0));
}

void TransmitModel::setSbMonitor(bool on)
{
    emit commandReady(QString("transmit set sb_monitor=%1").arg(on ? 1 : 0));
}

void TransmitModel::setMonGainSb(int gain)
{
    gain = qBound(0, gain, 100);
    emit commandReady(QString("transmit set mon_gain_sb=%1").arg(gain));
}

void TransmitModel::loadMicProfile(const QString& name)
{
    emit commandReady(QString("profile mic load \"%1\"").arg(name));
}

// ── Helpers ─────────────────────────────────────────────────────────────────

ATUStatus TransmitModel::parseAtuTuneStatus(const QString& s)
{
    // Values from FlexLib Radio.cs ParseATUTuneStatus()
    if (s == "NONE")               return ATUStatus::None;
    if (s == "TUNE_NOT_STARTED")   return ATUStatus::NotStarted;
    if (s == "TUNE_IN_PROGRESS")   return ATUStatus::InProgress;
    if (s == "TUNE_BYPASS")        return ATUStatus::Bypass;
    if (s == "TUNE_SUCCESSFUL")    return ATUStatus::Successful;
    if (s == "TUNE_OK")            return ATUStatus::OK;
    if (s == "TUNE_FAIL_BYPASS")   return ATUStatus::FailBypass;
    if (s == "TUNE_FAIL")          return ATUStatus::Fail;
    if (s == "TUNE_ABORTED")       return ATUStatus::Aborted;
    if (s == "TUNE_MANUAL_BYPASS") return ATUStatus::ManualBypass;
    qDebug() << "TransmitModel: unknown ATU status:" << s;
    return ATUStatus::None;
}

} // namespace AetherSDR
