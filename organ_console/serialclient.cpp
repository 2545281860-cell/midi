/**
 * @file serialclient.cpp
 * @brief SerialClient 实现：8N1 收发与通道命令封装。
 */

#include "serialclient.h"

SerialClient::SerialClient(QObject *parent)
    : QObject(parent)
    , m_port(new QSerialPort(this))
{
    connect(m_port, &QSerialPort::readyRead, this, &SerialClient::onReadyRead);
    connect(m_port, &QSerialPort::errorOccurred, this, &SerialClient::onSerialError);
}

SerialClient::~SerialClient()
{
    closePort();
}

bool SerialClient::isOpen() const
{
    return m_port->isOpen();
}

bool SerialClient::openPort(const QString &portName, int baud)
{
    if (m_port->isOpen()) {
        closePort();
    }

    m_port->setPortName(portName);
    m_port->setBaudRate(baud);
    m_port->setDataBits(QSerialPort::Data8);
    m_port->setParity(QSerialPort::NoParity);
    m_port->setStopBits(QSerialPort::OneStop);
    m_port->setFlowControl(QSerialPort::NoFlowControl);

    if (!m_port->open(QIODevice::ReadWrite)) {
        emit errorOccurred(m_port->errorString());
        emit connectionChanged(false);
        return false;
    }

    emit connectionChanged(true);
    return true;
}

void SerialClient::closePort()
{
    if (m_port->isOpen()) {
        m_port->close();
        emit connectionChanged(false);
    }
}

bool SerialClient::sendLine(const QString &line)
{
    if (!m_port->isOpen()) {
        return false;
    }
    const QByteArray payload = (line.trimmed() + QLatin1Char('\n')).toUtf8();
    const qint64 n = m_port->write(payload);
    m_port->flush();
    return n == payload.size();
}

bool SerialClient::sendChannelState(int channel, bool on)
{
    if (channel < 1 || channel > kChannelCount) {
        return false;
    }
    const QString cmd = QStringLiteral("%1 %2")
                            .arg(on ? QStringLiteral("ON") : QStringLiteral("OFF"))
                            .arg(channel);
    return sendLine(cmd);
}

bool SerialClient::sendAllOff()
{
    return sendLine(QStringLiteral("ALL_OFF"));
}

bool SerialClient::sendRemoteDebug(bool enabled)
{
    return sendLine(enabled ? QStringLiteral("DEBUG ON")
                            : QStringLiteral("DEBUG OFF"));
}

void SerialClient::onReadyRead()
{
    const QByteArray data = m_port->readAll();
    if (data.isEmpty()) {
        return;
    }
    emit bytesReceived(QString::fromLocal8Bit(data));
}

void SerialClient::onSerialError(QSerialPort::SerialPortError error)
{
    if (error == QSerialPort::NoError || error == QSerialPort::TimeoutError) {
        return;
    }
    emit errorOccurred(m_port->errorString());
    if (m_port->isOpen()) {
        m_port->close();
        emit connectionChanged(false);
    }
}
