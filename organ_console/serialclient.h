/**
 * @file serialclient.h
 * @brief 主板 UART 客户端：打开 COM、发送文本帧、同步 DEBUG。
 *
 * 热路径推荐：
 *   @ST,<seq>,<t_ms>,<bitmap8hex>,<crc16>\n
 * 兼容：
 *   ALL_OFF / DEBUG ON|OFF / ON n / OFF n（调试用）
 */

#ifndef SERIALCLIENT_H
#define SERIALCLIENT_H

#include <QObject>
#include <QSerialPort>
#include <QString>

class SerialClient : public QObject
{
    Q_OBJECT

public:
    /** 当前硬件通道数（两从板 × 8）；位图协议预留 32 bit。 */
    static constexpr int kChannelCount = 16;

    explicit SerialClient(QObject *parent = nullptr);
    ~SerialClient() override;

    bool isOpen() const;
    bool openPort(const QString &portName, int baud);
    void closePort();

    /**
     * @brief 发送一行 ASCII 并追加 '\\n'。
     * @param line 不含换行
     */
    bool sendLine(const QString &line);

    /**
     * @brief 兼容单键命令（优先走聚合器，一般不直接调用）。
     */
    bool sendChannelState(int channel, bool on);

    bool sendAllOff();
    bool sendRemoteDebug(bool enabled);

signals:
    void bytesReceived(const QString &text);
    void errorOccurred(const QString &text);
    void connectionChanged(bool connected);

private slots:
    void onReadyRead();
    void onSerialError(QSerialPort::SerialPortError error);

private:
    QSerialPort *m_port = nullptr;
};

#endif // SERIALCLIENT_H
