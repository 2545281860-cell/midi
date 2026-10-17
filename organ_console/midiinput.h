/**
 * @file midiinput.h
 * @brief 外接 MIDI 键盘/控制器输入（Windows：winmm midiIn）。
 *
 * 将 Note On/Off 转到 Qt 信号，由主窗口按 KeyMapping 映射为通道 1..16。
 * 非 Windows 平台提供空实现，listDevices() 为空。
 */

#ifndef MIDIINPUT_H
#define MIDIINPUT_H

#include <QObject>
#include <QStringList>

#ifdef Q_OS_WIN
#include <windows.h>
#include <mmsystem.h>
#endif

class MidiInputDevice : public QObject
{
    Q_OBJECT

public:
    explicit MidiInputDevice(QObject *parent = nullptr);
    ~MidiInputDevice() override;

    /** 枚举系统中的 MIDI 输入设备名称（下标即打开时的 deviceIndex）。 */
    QStringList listDevices() const;

    /**
     * @brief 打开指定下标的输入设备。
     * @param deviceIndex 0-based，对应 listDevices() 顺序
     */
    bool openDevice(int deviceIndex);

    /** 停止并关闭当前设备。 */
    void closeDevice();

    bool isOpen() const { return m_open; }

signals:
    /** MIDI 音符按下：midiNote=0..127，velocity=1..127。 */
    void noteOn(int midiNote, int velocity);

    /** MIDI 音符松开（含 NoteOn velocity=0）。 */
    void noteOff(int midiNote, int velocity);

    /** 打开失败或驱动错误。 */
    void errorOccurred(const QString &text);

private:
#ifdef Q_OS_WIN
    /**
     * winmm 回调（可能在非 Qt 线程）。
     * 仅把短消息投递到对象所在线程再解析。
     */
    static void CALLBACK winMidiProc(HMIDIIN handle, UINT msg, DWORD_PTR instance,
                                     DWORD_PTR param1, DWORD_PTR param2);

    /** 在 GUI 线程解析 3 字节短消息：status | data1<<8 | data2<<16。 */
    void handleShortMsg(quint32 packed);

    HMIDIIN m_handle = nullptr; /**< winmm 输入句柄 */
#endif
    bool m_open = false; /**< 是否已成功 midiInStart */
};

#endif // MIDIINPUT_H
