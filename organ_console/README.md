# organ_console — 短窗聚合 + 单帧位图 CAN

## 本次改动（对齐设计稿）

### 1. 短窗聚合（Qt）
- `KeyStateAggregator`：按键只改 shadow，**默认 4 ms** 窗满后发一张完整位图
- 不再每键一条 `ON/OFF`（多键同时按下合并为 1 次 UART）

### 2. UART 帧
```
@ST,<seq4>,<t_ms8>,<bitmap8>,<crc4>\n
```
- `bitmap`：32 位十六进制，bit0=键1（当前硬件用低 16 位）
- CRC16-CCITT-FALSE，覆盖 `ST,seq,t,bitmap`
- 主板成功发 CAN 后回：`@OK,<seq>,0`；失败：`@ERR,<seq>,code`

### 3. CAN 仲裁与单帧广播
| ID | 用途 | 优先级 |
|----|------|--------|
| 0x000 | ALL_OFF | 最高 |
| 0x100 DLC=4 | 键位位图（大端） | 高 |
| 0x100 其它 DLC | DISCOVER/SYNC/SYS | 同 ID 靠 DLC 区分 |
| 0x201+ | 从板上报 | 低（不与键位抢总线） |

载荷示例 `bitmap=0x00000103` → DATA `00 00 01 03`  
N1 读 `data[3]`，N2 读 `data[2]`。

### 4. Bus-Off 恢复（主板）
- 使能 TWAI alerts；`INVALID_STATE`/Bus-Off 时 `initiate_recovery` + `twai_start` 并重试发送

## 烧录 / 编译
1. 重新 flash：`can_master`、`can_slave1`、`can_slave2`
2. 重新编译运行 `organ_console.pro`

## 调试
勾选「调试回报」可看到 `[TX] @ST ...` 与主板 `@OK`/`KEY=...`。
