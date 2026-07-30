# FlashStore A/B Partition

面向小型 MCU 的 A/B 双页 Flash 存储库。零依赖，不绑定 HAL，通过三个回调接入。
**不需要工作缓冲区** — header 在栈上构建，数据直接从用户 buffer 写入 flash。

适合保存几十到几百字节、低频更新的配置数据。需要动态 KV、磨损均衡或
时序数据时，应选择 FlashDB / LittleFS 等完整存储组件。

## 工作方式

- 每页 = 12 字节 header（魔数 + 长度 + CRC32）+ payload。
- 保存时先擦除再分别写入 header 和 payload，page A → page B。
- A 页写入失败（擦除或编程）立即返回错误，不触碰 B 页。
- 加载时先校验 A 页；A 页无效时回退 B 页。
- **不含加密** — 加密由用户在外部处理（encrypt before Save, decrypt after Load）。
- 仓库附带独立的 [XXTEA](src/xxtea.c) 实现（Wheeler & Needham, 1998）供加密使用。

## 可靠性

### 写入顺序：A 完成才动 B

```
Save:
  ① erase A  →  失败立即返回，B 完好
  ② write A header + data  →  失败立即返回，B 完好
  ─── A 完整写入 ───
  ③ erase B  →  失败：A 已有新数据，Load 会读到 A
  ④ write B header + data  →  失败：A 已有新数据，Load 会读到 A
  ─── 两页一致 ───
```

> 任何时候断电，要么拿到旧数据，要么拿到新数据。不存在"新旧掺杂"或"数据丢失"的窗口。

### 加载 + 自动修复

每次 Load 都**同时校验两页**，按三种情况处理：

```
Load:
  读 A 的 header+data, 读 B 的 header+data
  │
  ├─ 两页都 OK ──────────────────→ 返回 A 的数据
  │
  ├─ 只有一页 OK ──→ 用好页修坏页 ──→ 返回好页的数据
  │
  └─ 两页都坏 ──────────────────→ 返回错误
```

| 状态 | 触发场景 | 修复动作 |
|------|---------|---------|
| **A OK, B OK** | 正常状态 | 无 |
| **A OK, B 坏** | 上次 Save 写到 B 时断电，或 B 硬件故障 | 从 A 重写 B |
| **A 坏, B OK** | A 硬件故障 / 旧数据被意外擦除 | 从 B 重写 A |
| **两页都坏** | 完全未初始化，或硬件严重故障 | 返回错误，无法自动恢复 |

- **CRC32** 覆盖整个 payload，单 bit 翻转必定检出。
- **修复是重写整页**（erase + program），恢复后两页再次一致。
- 修复失败不影响返回数据——调用者已经拿到了正确的 payload。

### 断电时序表

| 断电时机 | A 状态 | B 状态 | 下次 Load |
|----------|--------|--------|-----------|
| 空闲 | 好 | 好 | 读到 A（新）|
| 擦 A 中 | 坏 | 好（旧）| 回退 B → **自动修 A** |
| 写 A 中 | 坏 | 好（旧）| 回退 B → **自动修 A** |
| A 完成，B 前 | 好（新）| 好（旧）| 读到 A（新）|
| 擦 B 中 | 好（新）| 坏 | 读到 A（新）|
| 写 B 中 | 好（新）| 坏 | 读到 A（新）→ **自动修 B** |

## 接入

将以下文件加入工程：

```text
include/flash_store.h    src/flash_store.c
include/xxtea.h          src/xxtea.c        （可选，加密用）
```

```c
FlashStore store;

FlashStore_Config config = {
    .io = {
        .read   = board_flash_read,
        .erase  = board_flash_erase,
        .program = board_flash_program,
    },
    .page_a_address = 0x0800FE00,
    .page_b_address = 0x0800FE80,
    .page_size      = 128,
};

FlashStore_Init(&store, &config);
FlashStore_Save(&store, data, size);
FlashStore_Load(&store, data, size);
```

加密示例（XXTEA）：

```c
#include "xxtea.h"

static const uint32_t key[4] = { 0x12, 0x34, 0x56, 0x78 };

/* save: encrypt → store */
xxtea_encrypt(data, padded_size, key);
FlashStore_Save(&store, data, padded_size);

/* load: load → decrypt */
FlashStore_Load(&store, data, padded_size);
xxtea_decrypt(data, padded_size, key);
```

完整示例见 [`examples/air001_example.c`](examples/air001_example.c)。

## IO 回调约定

| 回调 | 语义 |
|------|------|
| `read(ctx, addr, out, len)` | 从 `addr` 读 `len` 字节到 `out`。`len` 可能小于 page_size。 |
| `erase(ctx, addr, len)` | 擦除 `addr` 起始的 `len` 字节。`len == page_size`（整页擦除）。 |
| `program(ctx, addr, data, len)` | 将 `data` 的 `len` 字节写入 `addr`。`len` 可能小于 page_size。 |

## 约束

- 两个地址必须属于互不重叠的独立擦除页。
- 接口不是线程安全或中断可重入的。
- Payload 最大长度：`page_size - 12` 字节。
- 结构体布局变化需由应用层处理版本兼容。

## RAM 占用（32-bit MCU）

| 项目 | 大小 |
|------|------|
| `FlashStore` 结构体 | 28 bytes |
| **总计** | **28 bytes** |

## 测试

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## License

[MIT](LICENSE)
