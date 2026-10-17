/**
 * @file midiparser.h
 * @brief 标准 MIDI 文件 SMF Format 0/1 解析为绝对毫秒时间轴上的 Note 事件。
 */

#ifndef MIDIPARSER_H
#define MIDIPARSER_H

#include <QByteArray>
#include <QString>
#include <QVector>
#include <cstdint>

/** 解析后只关心开关类事件（NoteOn velocity=0 已并入 NoteOff）。 */
enum class MidiEventType {
    NoteOn,  /**< 按下，velocity>0 */
    NoteOff  /**< 松开 */
};

/**
 * 一条已换算到毫秒的音符事件。
 * @var timeMs       相对曲目起点
 * @var type         开/关
 * @var midiNote     0..127
 * @var velocity     0..127
 * @var midiChannel  SMF 内 MIDI 通道 0..15，不是风琴通道
 */
struct MidiNoteEvent {
    qint64 timeMs = 0;
    MidiEventType type = MidiEventType::NoteOff;
    int midiNote = 0;
    int velocity = 0;
    int midiChannel = 0;
};

/**
 * parseFile/parseData 的返回值。
 * @var ok               解析成功
 * @var error            失败原因
 * @var format           0 或 1
 * @var trackCount       轨道数
 * @var ticksPerQuarter  每四分音符 tick 数（不支持 SMPTE）
 * @var durationMs       最后事件时间
 * @var events           已按 timeMs 排序
 */
struct MidiParseResult {
    bool ok = false;
    QString error;
    int format = 0;
    int trackCount = 0;
    int ticksPerQuarter = 480;
    qint64 durationMs = 0;
    QVector<MidiNoteEvent> events;
};

class MidiParser
{
public:
    /** 从磁盘读取并解析 .mid / .midi。 */
    static MidiParseResult parseFile(const QString &path);

    /** 从内存缓冲区解析 SMF。 */
    static MidiParseResult parseData(const QByteArray &data);

private:
    /** 速度变化点：某 tick 起 usPerQuarter 生效（默认 500000=120BPM）。 */
    struct TempoPoint {
        qint64 absTick = 0;
        int usPerQuarter = 500000;
    };

    /** 尚未换算毫秒的原始音符，absTick 为轨道内累计 delta。 */
    struct RawEvent {
        qint64 absTick = 0;
        MidiEventType type = MidiEventType::NoteOff;
        int midiNote = 0;
        int velocity = 0;
        int midiChannel = 0;
    };

    static bool readHeader(const QByteArray &data, int &pos,
                           int &format, int &ntrks, int &division,
                           QString &err);
    static bool readTrack(const QByteArray &data, int &pos,
                          QVector<RawEvent> &outEvents,
                          QVector<TempoPoint> &tempos,
                          QString &err);
    /** 读取 MIDI 变长数量（最多 4 字节）。 */
    static quint32 readVarLen(const QByteArray &data, int &pos, QString &err);
    /** 按 tempo 折线把绝对 tick 换成毫秒。 */
    static qint64 ticksToMs(qint64 absTick,
                            int ticksPerQuarter,
                            const QVector<TempoPoint> &tempos);
};

#endif // MIDIPARSER_H
