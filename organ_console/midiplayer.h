/**
 * @file midiplayer.h
 * @brief 按时间轴回放 MidiNoteEvent，经 KeyMapping 转为通道 ON/OFF 信号。
 */

#ifndef MIDIPLAYER_H
#define MIDIPLAYER_H

#include "keymapping.h"
#include "midiparser.h"

#include <QObject>
#include <QElapsedTimer>
#include <QTimer>
#include <QVector>
#include <bitset>

class MidiPlayer : public QObject
{
    Q_OBJECT

public:
    explicit MidiPlayer(QObject *parent = nullptr);

    /** 设置映射表指针（不拥有，须保证播放期间有效）。 */
    void setMapping(const KeyMapping *mapping);

    /** 载入已解析事件；会先 stop()。 */
    void setEvents(const QVector<MidiNoteEvent> &events);

    bool isPlaying() const { return m_playing; }
    bool isPaused() const { return m_paused; }

    /** 当前逻辑播放位置（已乘速度系数），毫秒。 */
    qint64 positionMs() const;

    qint64 durationMs() const { return m_durationMs; }

public slots:
    void play();
    void pause();
    void stop();
    /** 1.0=原速；内部限制约 0.1..4.0。 */
    void setTempoScale(double scale);

signals:
    void channelCommand(int channel, bool on);
    void unmappedNote(int midiNote, bool on);
    void positionChanged(qint64 ms);
    void finished();
    void stopped();

private slots:
    /** QTimer 到期：发放所有 timeMs<=now 的事件并预约下一次。 */
    void onTick();

private:
    void releaseAllChannels();
    void applyEvent(const MidiNoteEvent &ev);
    void scheduleNext();

    const KeyMapping *m_mapping = nullptr; /**< 音高→通道 */
    QVector<MidiNoteEvent> m_events;       /**< 已排序事件 */
    int m_index = 0;                       /**< 下一条待发放下标 */
    qint64 m_durationMs = 0;               /**< 曲长 */
    double m_tempoScale = 1.0;             /**< 速度倍率 */

    bool m_playing = false;
    bool m_paused = false;
    qint64 m_pauseOffsetMs = 0;            /**< 暂停时累计逻辑时间 */

    QElapsedTimer m_clock;                 /**< 自上次 play/resume 的墙钟 */
    QTimer m_timer;                        /**< 精确定时器 */
    std::bitset<17> m_channelOn;           /**< bit1..16 当前是否已发 ON */
};

#endif // MIDIPLAYER_H
