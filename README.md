# FlashStore A/B Partition

面向小型 MCU 的 A/B 双页 Flash 存储库。不依赖具体 HAL，通过三个回调接入；
加密通过函数指针注入，不配置即明文存储。

适合保存几十到几百字节、低频更新的配置数据。需要动态 KV、磨损均衡或
时序数据时，应选择 FlashDB / LittleFS 等完整存储组件。

## 工作方式

- 每页包含魔数、数据长度、CRC32 和 payload。
- 保存时先写 A 页，再写 B 页（镜像冗余）。
- A 页写入失败时不触碰 B 页，旧数据仍可读取。
- 加载时先校验 A 页；A 页无效时回退 B 页。
- 加密是可选的：注入 `encrypt`/`decrypt` 函数指针即启用，否则明文存储。
- 附带独立的 [XXTEA](src/xxtea.c) 实现（Wheeler & Needham, 1998）。

## 接入

将以下文件加入工程：

```text
include/flash_store.h    src/flash_store.c
include/xxtea.h          src/xxtea.c        （可选，加密时使用）
```

### 明文存储（无加密）

```c
FlashStore store;
uint8_t workspace[128];

FlashStore_Config config = {
    .io = {
        .read   = board_flash_read,
        .erase  = board_flash_erase,
        .program = board_flash_program,
    },
    .page_a_address = 0x0800FE00,
    .page_b_address = 0x0800FE80,
    .page_size      = 128,
    .workspace      = workspace,
    /* encrypt / decrypt / cipher_key 留 NULL → 明文 */
};

FlashStore_Init(&store, &config);
FlashStore_Save(&store, data, size);
FlashStore_Load(&store, data, size);
```

### 加密存储（XXTEA）

```c
#include "xxtea.h"

static const uint32_t key[4] = {
    0x12345678, 0x9ABCDEF0, 0x0FEDCBA9, 0x87654321
};

FlashStore_Config config = {
    /* ... 同上 ... */
    .encrypt    = xxtea_encrypt,
    .decrypt    = xxtea_decrypt,
    .cipher_key = key,
};
```

## 约束

- 两个地址必须属于互不重叠的独立擦除页，并从链接脚本的应用区排除。
- `program` 回调必须完成整页写入；不同芯片的页写 API 和对齐要求需在回调内处理。
- `workspace` 必须至少 `page_size` 字节（文档约定，不做运行时检查）。
- 工作区会在调用期间被改写，接口不是线程安全或中断可重入的。
- Payload 最大长度：`page_size - 12`（明文）或 `page_size - 15`（加密，含对齐余量）。
- 结构体布局变化需由应用层处理版本兼容。

## RAM 占用（32-bit MCU）

| 项目 | 大小 |
|------|------|
| `FlashStore` 结构体 | 44 bytes |
| `workspace` | page_size（如 128 bytes） |
| `cipher_key[4]`（加密时） | 16 bytes |
| **典型总计（128B 页 + 加密）** | **188 bytes** |

## 测试

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## License

[MIT](LICENSE)
