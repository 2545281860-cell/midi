/**
 * @file keymapping.h
 * @brief 音高(MIDI Note 0..127) ↔ 风琴通道 1..16 映射表。
 *
 * 默认连续半音：通道1 = C4(60) … 通道16 = D#5(75)。
 * 与下位机键号一致：1–8 = 从板 N1，9–16 = 从板 N2。
 * 实时 MIDI 与自动演奏共用同一张表。
 */

#ifndef KEYMAPPING_H
#define KEYMAPPING_H

#include <QString>
#include <QVector>
#include <optional>

/**
 * 一行映射：一个风琴通道对应一个 MIDI 音高。
 * @var channel  通道号 1..16，对应串口 ON/OFF n
 * @var midiNote MIDI 音符号 0..127
 * @var noteName 显示用音名，如 "C4"
 */
struct PitchMapEntry {
    int channel = 0;
    int midiNote = 0;
    QString noteName;
};

class KeyMapping
{
public:
    /** 当前硬件通道数（两从板 × 8）。 */
    static constexpr int kChannelCount = 16;
    /** 默认映射起点：中央 C 上一组 C4 = MIDI 48。 */
    static constexpr int kDefaultBaseNote = 48;

    KeyMapping();

    /**
     * 恢复默认：通道 i 对应 midiNote = baseNote + (i-1)。
     * @param baseNote 通道1 的 MIDI 音高
     */
    void resetToDefault(int baseNote = kDefaultBaseNote);

    /** 通道 → MIDI 音高；非法通道返回 -1。 */
    int midiNoteForChannel(int channel) const;

    /** MIDI 音高 → 通道；未映射返回 nullopt。 */
    std::optional<int> channelForMidiNote(int midiNote) const;

    /**
     * 设置某通道的 MIDI 音高并更新音名。
     * @return 通道或音高越界时 false
     */
    bool setChannelMidiNote(int channel, int midiNote);

    /** 只读访问整表（表格刷新用）。 */
    const QVector<PitchMapEntry> &entries() const { return m_entries; }

    /** MIDI 音高转音名（C-1..G9 惯例，60=C4）。 */
    static QString midiNoteToName(int midiNote);

private:
    QVector<PitchMapEntry> m_entries; /**< index 0 → 通道 1 */
};

#endif // KEYMAPPING_H
