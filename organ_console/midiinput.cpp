/**
 * @file midiinput.cpp
 * @brief Windows MIDI 输入封装；其它系统为空实现。
 */

#include "midiinput.h"

#ifdef Q_OS_WIN
#include <QMetaObject>
#endif

MidiInputDevice::MidiInputDevice(QObject *parent)
    : QObject(parent)
{
}

MidiInputDevice::~MidiInputDevice()
{
    closeDevice();
}

QStringList MidiInputDevice::listDevices() const
{
    QStringList names;
#ifdef Q_OS_WIN
    const UINT n = midiInGetNumDevs();
    for (UINT i = 0; i < n; ++i) {
        MIDIINCAPS caps;
        ZeroMemory(&caps, sizeof(caps));
        if (midiInGetDevCaps(i, &caps, sizeof(caps)) == MMSYSERR_NOERROR) {
#ifdef UNICODE
            names.append(QString::fromWCharArray(caps.szPname));
#else
            names.append(QString::fromLocal8Bit(caps.szPname));
#endif
        } else {
            names.append(QStringLiteral("MIDI In %1").arg(i));
        }
    }
#endif
    return names;
}

bool MidiInputDevice::openDevice(int deviceIndex)
{
    closeDevice();
#ifdef Q_OS_WIN
    if (deviceIndex < 0 || deviceIndex >= static_cast<int>(midiInGetNumDevs())) {
        emit errorOccurred(QStringLiteral("无效的 MIDI 设备下标。"));
        return false;
    }
    const MMRESULT r = midiInOpen(
        &m_handle,
        static_cast<UINT>(deviceIndex),
        reinterpret_cast<DWORD_PTR>(&MidiInputDevice::winMidiProc),
        reinterpret_cast<DWORD_PTR>(this),
        CALLBACK_FUNCTION);
    if (r != MMSYSERR_NOERROR) {
        m_handle = nullptr;
        emit errorOccurred(QStringLiteral("midiInOpen 失败，代码 %1").arg(r));
        return false;
    }
    if (midiInStart(m_handle) != MMSYSERR_NOERROR) {
        midiInClose(m_handle);
        m_handle = nullptr;
        emit errorOccurred(QStringLiteral("midiInStart 失败。"));
        return false;
    }
    m_open = true;
    return true;
#else
    Q_UNUSED(deviceIndex);
    emit errorOccurred(QStringLiteral("当前系统未实现 MIDI 输入（需要 Windows）。"));
    return false;
#endif
}

void MidiInputDevice::closeDevice()
{
#ifdef Q_OS_WIN
    if (m_handle) {
        midiInStop(m_handle);
        midiInReset(m_handle);
        midiInClose(m_handle);
        m_handle = nullptr;
    }
#endif
    m_open = false;
}

#ifdef Q_OS_WIN
void CALLBACK MidiInputDevice::winMidiProc(HMIDIIN handle, UINT msg, DWORD_PTR instance,
                                           DWORD_PTR param1, DWORD_PTR param2)
{
    Q_UNUSED(handle);
    Q_UNUSED(param2);
    auto *self = reinterpret_cast<MidiInputDevice *>(instance);
    if (!self) {
        return;
    }
    if (msg == MIM_DATA) {
        const quint32 packed = static_cast<quint32>(param1);
        QMetaObject::invokeMethod(self, [self, packed]() {
            self->handleShortMsg(packed);
        }, Qt::QueuedConnection);
    }
}

void MidiInputDevice::handleShortMsg(quint32 packed)
{
    const int status = static_cast<int>(packed & 0xFFu);
    const int data1 = static_cast<int>((packed >> 8) & 0xFFu);
    const int data2 = static_cast<int>((packed >> 16) & 0xFFu);
    const int type = status & 0xF0;
    if (type == 0x90) {
        if (data2 > 0) {
            emit noteOn(data1, data2);
        } else {
            emit noteOff(data1, 0);
        }
    } else if (type == 0x80) {
        emit noteOff(data1, data2);
    }
}
#endif
