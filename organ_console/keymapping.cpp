/**
 * @file keymapping.cpp
 * @brief KeyMapping 实现：默认半音阶表与音名转换。
 */

#include "keymapping.h"

KeyMapping::KeyMapping()
{
    resetToDefault(kDefaultBaseNote);
}

void KeyMapping::resetToDefault(int baseNote)
{
    m_entries.clear();
    m_entries.reserve(kChannelCount);
    for (int ch = 1; ch <= kChannelCount; ++ch) {
        PitchMapEntry e;
        e.channel = ch;
        e.midiNote = baseNote + (ch - 1);
        e.noteName = midiNoteToName(e.midiNote);
        m_entries.push_back(e);
    }
}

int KeyMapping::midiNoteForChannel(int channel) const
{
    if (channel < 1 || channel > kChannelCount) {
        return -1;
    }
    return m_entries[channel - 1].midiNote;
}

std::optional<int> KeyMapping::channelForMidiNote(int midiNote) const
{
    if (midiNote < 0 || midiNote > 127) {
        return std::nullopt;
    }
    for (const PitchMapEntry &e : m_entries) {
        if (e.midiNote == midiNote) {
            return e.channel;
        }
    }
    return std::nullopt;
}

bool KeyMapping::setChannelMidiNote(int channel, int midiNote)
{
    if (channel < 1 || channel > kChannelCount) {
        return false;
    }
    if (midiNote < 0 || midiNote > 127) {
        return false;
    }
    PitchMapEntry &e = m_entries[channel - 1];
    e.midiNote = midiNote;
    e.noteName = midiNoteToName(midiNote);
    return true;
}

QString KeyMapping::midiNoteToName(int midiNote)
{
    static const char *kNames[12] = {
        "C", "C#", "D", "D#", "E", "F",
        "F#", "G", "G#", "A", "A#", "B"
    };
    if (midiNote < 0 || midiNote > 127) {
        return QStringLiteral("?");
    }
    const int octave = (midiNote / 12) - 1;
    return QStringLiteral("%1%2").arg(QLatin1String(kNames[midiNote % 12])).arg(octave);
}
