/**
 * @file midiparser.cpp
 * @brief SMF 解析：MThd/MTrk、Running Status、Meta Tempo(0x51)、Note On/Off。
 */

#include "midiparser.h"

#include <QFile>
#include <QtEndian>
#include <algorithm>

namespace {

/** 从 pos 起读 16 位大端。 */
quint16 be16(const QByteArray &d, int pos)
{
    return qFromBigEndian<quint16>(
        reinterpret_cast<const uchar *>(d.constData() + pos));
}

/** 从 pos 起读 32 位大端。 */
quint32 be32(const QByteArray &d, int pos)
{
    return qFromBigEndian<quint32>(
        reinterpret_cast<const uchar *>(d.constData() + pos));
}

} // namespace

MidiParseResult MidiParser::parseFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        MidiParseResult r;
        r.error = QStringLiteral("无法打开文件：%1").arg(path);
        return r;
    }
    return parseData(f.readAll());
}

quint32 MidiParser::readVarLen(const QByteArray &data, int &pos, QString &err)
{
    quint32 value = 0;
    for (int i = 0; i < 4; ++i) {
        if (pos >= data.size()) {
            err = QStringLiteral("变长数量越界");
            return 0;
        }
        const quint8 b = static_cast<quint8>(data.at(pos++));
        value = (value << 7) | (b & 0x7F);
        if ((b & 0x80) == 0) {
            return value;
        }
    }
    err = QStringLiteral("变长数量过长");
    return 0;
}

bool MidiParser::readHeader(const QByteArray &data, int &pos,
                            int &format, int &ntrks, int &division,
                            QString &err)
{
    if (pos + 14 > data.size()) {
        err = QStringLiteral("MThd 过短");
        return false;
    }
    if (data.mid(pos, 4) != QByteArray("MThd")) {
        err = QStringLiteral("缺少 MThd");
        return false;
    }
    pos += 4;
    const quint32 len = be32(data, pos);
    pos += 4;
    if (len < 6 || pos + static_cast<int>(len) > data.size()) {
        err = QStringLiteral("MThd 长度非法");
        return false;
    }
    format = be16(data, pos);
    ntrks = be16(data, pos + 2);
    division = be16(data, pos + 4);
    pos += static_cast<int>(len);

    if (division & 0x8000) {
        err = QStringLiteral("暂不支持 SMPTE 时基");
        return false;
    }
    if (format > 1) {
        err = QStringLiteral("暂不支持 Format 2");
        return false;
    }
    return true;
}

bool MidiParser::readTrack(const QByteArray &data, int &pos,
                           QVector<RawEvent> &outEvents,
                           QVector<TempoPoint> &tempos,
                           QString &err)
{
    if (pos + 8 > data.size()) {
        err = QStringLiteral("MTrk 过短");
        return false;
    }
    if (data.mid(pos, 4) != QByteArray("MTrk")) {
        err = QStringLiteral("缺少 MTrk");
        return false;
    }
    pos += 4;
    const quint32 trackLen = be32(data, pos);
    pos += 4;
    const int trackEnd = pos + static_cast<int>(trackLen);
    if (trackEnd > data.size()) {
        err = QStringLiteral("MTrk 长度越界");
        return false;
    }

    qint64 absTick = 0;
    quint8 runningStatus = 0;

    while (pos < trackEnd) {
        const quint32 delta = readVarLen(data, pos, err);
        if (!err.isEmpty()) {
            return false;
        }
        absTick += delta;

        if (pos >= trackEnd) {
            err = QStringLiteral("事件越界");
            return false;
        }

        quint8 status = static_cast<quint8>(data.at(pos));
        if (status & 0x80) {
            ++pos;
            runningStatus = status;
        } else {
            if (runningStatus == 0) {
                err = QStringLiteral("缺少 running status");
                return false;
            }
            status = runningStatus;
        }

        if (status == 0xFF) {
            if (pos >= trackEnd) {
                err = QStringLiteral("Meta 越界");
                return false;
            }
            const quint8 metaType = static_cast<quint8>(data.at(pos++));
            const quint32 metaLen = readVarLen(data, pos, err);
            if (!err.isEmpty()) {
                return false;
            }
            if (pos + static_cast<int>(metaLen) > trackEnd) {
                err = QStringLiteral("Meta 数据越界");
                return false;
            }
            if (metaType == 0x51 && metaLen == 3) {
                const int us = (static_cast<quint8>(data.at(pos)) << 16)
                             | (static_cast<quint8>(data.at(pos + 1)) << 8)
                             | static_cast<quint8>(data.at(pos + 2));
                TempoPoint tp;
                tp.absTick = absTick;
                tp.usPerQuarter = us > 0 ? us : 500000;
                tempos.push_back(tp);
            }
            if (metaType == 0x2F) {
                pos = trackEnd;
                break;
            }
            pos += static_cast<int>(metaLen);
            continue;
        }

        if (status == 0xF0 || status == 0xF7) {
            const quint32 sysexLen = readVarLen(data, pos, err);
            if (!err.isEmpty()) {
                return false;
            }
            pos += static_cast<int>(sysexLen);
            continue;
        }

        const quint8 eventType = status & 0xF0;
        const int midiCh = status & 0x0F;

        auto need = [&](int n) -> bool {
            if (pos + n > trackEnd) {
                err = QStringLiteral("MIDI 事件数据不足");
                return false;
            }
            return true;
        };

        if (eventType == 0x80 || eventType == 0x90) {
            if (!need(2)) {
                return false;
            }
            const int note = static_cast<quint8>(data.at(pos++));
            const int vel = static_cast<quint8>(data.at(pos++));
            RawEvent ev;
            ev.absTick = absTick;
            ev.midiNote = note;
            ev.velocity = vel;
            ev.midiChannel = midiCh;
            if (eventType == 0x90 && vel > 0) {
                ev.type = MidiEventType::NoteOn;
            } else {
                ev.type = MidiEventType::NoteOff;
            }
            outEvents.push_back(ev);
        } else if (eventType == 0xA0 || eventType == 0xB0 || eventType == 0xE0) {
            if (!need(2)) {
                return false;
            }
            pos += 2;
        } else if (eventType == 0xC0 || eventType == 0xD0) {
            if (!need(1)) {
                return false;
            }
            pos += 1;
        } else {
            err = QStringLiteral("未知状态字节 0x%1")
                      .arg(status, 2, 16, QLatin1Char('0'));
            return false;
        }
    }

    pos = trackEnd;
    return true;
}

qint64 MidiParser::ticksToMs(qint64 absTick, int ticksPerQuarter,
                             const QVector<TempoPoint> &tempos)
{
    if (ticksPerQuarter <= 0) {
        return 0;
    }

    QVector<TempoPoint> t = tempos;
    if (t.isEmpty() || t.first().absTick != 0) {
        TempoPoint head;
        head.absTick = 0;
        head.usPerQuarter = 500000;
        t.prepend(head);
    }
    std::sort(t.begin(), t.end(),
              [](const TempoPoint &a, const TempoPoint &b) {
                  return a.absTick < b.absTick;
              });

    double ms = 0.0;
    for (int i = 0; i < t.size(); ++i) {
        const qint64 segStart = t[i].absTick;
        const qint64 segEnd = (i + 1 < t.size()) ? t[i + 1].absTick : absTick;
        const qint64 end = qMin(absTick, segEnd);
        if (end <= segStart) {
            if (absTick <= segStart) {
                break;
            }
            continue;
        }
        const qint64 ticks = end - segStart;
        ms += (static_cast<double>(ticks) * t[i].usPerQuarter)
              / (static_cast<double>(ticksPerQuarter) * 1000.0);
        if (end >= absTick) {
            break;
        }
    }
    return static_cast<qint64>(ms + 0.5);
}

MidiParseResult MidiParser::parseData(const QByteArray &data)
{
    MidiParseResult result;
    int pos = 0;
    int format = 0;
    int ntrks = 0;
    int division = 480;

    if (!readHeader(data, pos, format, ntrks, division, result.error)) {
        return result;
    }

    QVector<RawEvent> raw;
    QVector<TempoPoint> tempos;

    for (int i = 0; i < ntrks; ++i) {
        if (!readTrack(data, pos, raw, tempos, result.error)) {
            return result;
        }
    }

    result.format = format;
    result.trackCount = ntrks;
    result.ticksPerQuarter = division;

    result.events.reserve(raw.size());
    for (const RawEvent &r : raw) {
        MidiNoteEvent e;
        e.timeMs = ticksToMs(r.absTick, division, tempos);
        e.type = r.type;
        e.midiNote = r.midiNote;
        e.velocity = r.velocity;
        e.midiChannel = r.midiChannel;
        result.events.push_back(e);
    }

    std::stable_sort(result.events.begin(), result.events.end(),
                     [](const MidiNoteEvent &a, const MidiNoteEvent &b) {
                         if (a.timeMs != b.timeMs) {
                             return a.timeMs < b.timeMs;
                         }
                         return static_cast<int>(a.type) > static_cast<int>(b.type);
                     });

    if (!result.events.isEmpty()) {
        result.durationMs = result.events.last().timeMs;
    }

    result.ok = true;
    return result;
}
