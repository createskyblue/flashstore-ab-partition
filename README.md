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
