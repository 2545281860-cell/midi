/**
 * @file keystateaggregator.h
 * @brief 短窗聚合：高频通道开关合并为一次完整位图快照再发串口。
 *
 * 按设计稿：窗口内只改 shadowState，窗口到期发送最新 32 位图（当前硬件用低 16 位）。
 * 默认聚合窗 4 ms（约 2–5 ms 可配）。
 */

#ifndef KEYSTATEAGGREGATOR_H
#define KEYSTATEAGGREGATOR_H

#include <QObject>
#include <QTimer>
#include <cstdint>

class SerialClient;

class KeyStateAggregator : public QObject
{
    Q_OBJECT

public:
    /** 与固件/设计稿一致的位图宽度（预留 32；现用低 16 位）。 */
    static constexpr int kBitmapBits = 32;
    /** 默认短窗毫秒数。 */
    static constexpr int kDefaultWindowMs = 4;

    explicit KeyStateAggregator(SerialClient *serial, QObject *parent = nullptr);

    /** 配置聚合窗长度（毫秒），建议 2–5。 */
    void setWindowMs(int ms);

    int windowMs() const { return m_windowMs; }

    /** 当前 shadow 位图（bit0=键1）。 */
    quint32 shadowState() const { return m_shadow; }

    /**
     * @brief 更新某一通道并启动/刷新短窗（不立刻发串口）。
     * @param channel 1..16（当前硬件）
     * @param on true 置位，false 清位
     */
    void setChannel(int channel, bool on);

    /** 立即清零并刷出 ST 00000000（急停/切模式）。 */
    void allOffFlush();

    /** 丢弃窗口内未发变更，并把 shadow 置 0（不发串口）。 */
    void resetLocal();

    /** 强制立刻发送当前 shadow（取消定时器）。 */
    void flushNow();

signals:
    /** 已向串口发出一条 ST 帧时的位图（调试日志用）。 */
    void snapshotSent(quint32 bitmap, quint16 seq);

private slots:
    /** 短窗到期：发送最新完整快照。 */
    void onWindowTimeout();

private:
    bool sendBitmapFrame(quint32 bitmap);

    SerialClient *m_serial = nullptr; /**< 不拥有 */
    QTimer m_timer;                   /**< 单次短窗定时器 */
    quint32 m_shadow = 0;             /**< 目标位图 */
    quint32 m_lastSent = 0;           /**< 上次已发位图（去重） */
    quint16 m_seq = 0;                /**< 事务序号，与 ACK 配对 */
    int m_windowMs = kDefaultWindowMs;
    bool m_dirty = false;             /**< 窗内是否有未发送变更 */
};

#endif // KEYSTATEAGGREGATOR_H
