/**
 * @file mainwindow.h
 * @brief 主窗口：三模式切换 + 共享串口/映射表 + 调试回报开关。
 *
 * 模式：
 *   0 实时控制 — 外接 MIDI 设备 NoteOn/Off → 映射 → ON/OFF
 *   1 自动演奏 — 导入 SMF → MidiPlayer → ON/OFF
 *   2 直接控制 — 鼠标/电脑键盘 → ON/OFF（原 serial_test）
 */

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "keymapping.h"
#include "keystateaggregator.h"
#include "midiinput.h"
#include "midiplayer.h"
#include "serialclient.h"

#include <QMainWindow>
#include <QVector>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSlider;
class QTabWidget;
class QTableWidget;
class QKeyEvent;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    /** 仅「直接控制」模式：键盘按下 → ON（忽略 auto-repeat）。 */
    void keyPressEvent(QKeyEvent *event) override;
    /** 仅「直接控制」模式：键盘松开 → OFF。 */
    void keyReleaseEvent(QKeyEvent *event) override;

private slots:
    void refreshPortList();
    void toggleSerialPort();
    void onSerialBytes(const QString &text);
    void onSerialError(const QString &text);
    void onSerialConnectionChanged(bool connected);
    void onDebugToggled(bool enabled);
    void onModeTabChanged(int index);

    void refreshMidiDevices();
    void toggleMidiDevice();
    void onLiveNoteOn(int midiNote, int velocity);
    void onLiveNoteOff(int midiNote, int velocity);

    void openMidiFile();
    void onPlay();
    void onPause();
    void onStop();
    void onTempoChanged(int sliderValue);
    void onChannelCommand(int channel, bool on);
    void onUnmappedNote(int midiNote, bool on);
    void onPlayerFinished();
    void onPositionChanged(qint64 ms);

    void onDirectPressed(int channel);
    void onDirectReleased(int channel);
    void onAllOffClicked();
    void onResetMapping();
    void onMappingCellChanged(int row, int column);
    /** 聚合器发出 ST 快照时写日志（仅调试开）。 */
    void onSnapshotSent(quint32 bitmap, quint16 seq);

private:
    void setupUi();
    QWidget *buildRealtimePage();
    QWidget *buildAutoPlayPage();
    QWidget *buildDirectPage();
    void rebuildMappingTable();
    void updatePlayUi();
    void updateLiveUi();
    void highlightChannel(int channel, bool on);
    bool sendChannel(int channel, bool on);
    void appendLog(const QString &text);
    int keyToChannel(int key) const;
    static QString formatMs(qint64 ms);

    SerialClient *m_serial = nullptr;           /**< 主板 COM */
    KeyStateAggregator *m_aggregator = nullptr; /**< 短窗位图聚合 */
    MidiInputDevice *m_midiIn = nullptr;        /**< 外接 MIDI */
    MidiPlayer *m_player = nullptr;             /**< 文件回放 */
    KeyMapping m_mapping;                       /**< 音高↔通道 */

    QComboBox *m_portCombo = nullptr;
    QComboBox *m_baudCombo = nullptr;
    QPushButton *m_refreshPortBtn = nullptr;
    QPushButton *m_connectBtn = nullptr;
    QLabel *m_serialStatus = nullptr;
    QCheckBox *m_debugCheck = nullptr;  /**< 同步下位机 DEBUG ON/OFF */
    QTabWidget *m_modeTabs = nullptr;

    QComboBox *m_midiDevCombo = nullptr;
    QPushButton *m_refreshMidiBtn = nullptr;
    QPushButton *m_openMidiBtn = nullptr;
    QLabel *m_liveHint = nullptr;

    QLineEdit *m_fileEdit = nullptr;
    QPushButton *m_openFileBtn = nullptr;
    QPushButton *m_playBtn = nullptr;
    QPushButton *m_pauseBtn = nullptr;
    QPushButton *m_stopBtn = nullptr;
    QLabel *m_timeLabel = nullptr;
    QSlider *m_tempoSlider = nullptr;
    QLabel *m_tempoLabel = nullptr;

    QVector<QPushButton *> m_directButtons; /**< 直接控制 16 键 */
    QVector<bool> m_keyHeld;                /**< 键盘按住防连发 */

    QTableWidget *m_mapTable = nullptr;
    QPushButton *m_resetMapBtn = nullptr;
    QPlainTextEdit *m_logEdit = nullptr;
    QPushButton *m_allOffBtn = nullptr;

    bool m_blockMapEdit = false; /**< 刷新表格时忽略 cellChanged */
    bool m_debugEnabled = false; /**< 与复选框一致：是否显示 RX/TX 热路径日志 */
    qint64 m_durationMs = 0;
};

#endif // MAINWINDOW_H
