# FlashStore A/B Partition

A/B 双页 Flash 存储，面向小型 MCU。零依赖，28 bytes RAM。

**选哪个？**

| | FlashStore | FlashDB | LittleFS |
|------|------------|---------|----------|
| 定位 | 单块配置可靠存储 | 嵌入式键值数据库 | 嵌入式文件系统 |
| 数据模型 | 整块二进制 | 键值对 | 文件 |
| 读写方式 | Save/Load 整块 | Set/Get 按 key | Open/Read/Write |
| 磨损均衡 | ❌ | ✅ | ✅ |
| 掉电保护 | ✅ 双页冗余 | ✅ 事务日志 | ✅ |
| RAM | 28 bytes | 极低 | 较高（缓冲区） |
| 适用 | 配置参数，低频更新 | 多配置项独立读写 | 文件、日志、大块数据 |


## 5 分钟接入

1. 复制 `include/flash_store.h`、`src/flash_store.c`。需要加密再加 `chacha20` 或 `xxtea`
2. 实现 3 个回调：

```c
bool my_read(void *ctx, uint32_t addr, uint8_t *out, size_t len) {
    memcpy(out, (void *)(uintptr_t)addr, len);  // 或 HAL 读
    return true;
}
bool my_erase(void *ctx, uint32_t addr, size_t len) {
    HAL_FLASH_ErasePage(addr);                  // 整页擦除
    return true;
}
bool my_program(void *ctx, uint32_t addr, const uint8_t *data, size_t len) {
    // 按 word 编程，或 memcpy（支持字节寻址的 flash）
    return true;
}
```

3. 初始化并读写：

```c
FlashStore store;
FlashStore_Config cfg = {
    .io.read = my_read, .io.erase = my_erase, .io.program = my_program,
    .page_a_address = 0x0800FE00,
    .page_b_address = 0x0800FE80,
    .page_size      = 128,
};
FlashStore_Init(&store, &cfg);
FlashStore_Save(&store, data, len);   // 写
FlashStore_Load(&store, data, len);   // 读
```

> 需要加密？推荐 [ChaCha20](src/chacha20.c)——RFC 8439 标准，流密码不分块不要求对齐。实测 Cortex-M0 -Os：700B Flash + ~200B 栈，0 静态 RAM。极致省资源用 [XXTEA](src/xxtea.c)，仅混淆级别，要求 4 字节对齐且 ≥8 字节。加密在外部做：`encrypt → FlashStore_Save` / `FlashStore_Load → decrypt`。

## 为什么不会丢数据

### 写顺序：A 先，B 后

```
Save:
  ① erase A  →  失败 → 立即返回，B 完好（旧数据还在）
  ② write A  →  失败 → 同上
  ③ erase B  →  失败 → A 已有新数据，下次 Load 会读到 A
  ④ write B  →  失败 → 同上
```

任意时刻断电，要么拿到旧数据，要么拿到新数据。不存在"半新半旧"。

### CRC 裁决新旧

A 永远先于 B 写入。两页 CRC 都正确但内容不同 → A 必然更新。

```
Save 前:  A=v1 (CRC=c1)  B=v1 (CRC=c1)

 ① write A(v2, CRC=c2)
 ② ⚡ 断电！B 没碰

断电后:  A=v2  B=v1  ← 两页 CRC 都对，但不同

Load:
  读 A(→c2) → 读 B(→c1) → c2≠c1 → A 更新 → 重读 A → 修复 B ✓
```

CRC 相同时跳过重读，零额外开销：

```
Load 决策:
  ├─ A OK, B OK, CRC 相同 ──→ 直接返回，啥也不做
  ├─ A OK, B OK, CRC 不同 ──→ 重读 A → 修复 B
  ├─ A OK, B 坏 ──→ 重读 A（B 覆盖了 buffer）→ 修复 B
  ├─ A 坏, B OK ──→ data 已是 B 的数据，直接修复 A
  └─ A 坏, B 坏 ──→ 返回错误
```

### 修复失败？

修复失败？出现 `WARN_REPAIR_FAILED` 说明你的 IO 接口或硬件有问题——能读到好页说明 read 正常，能写入说明 erase/program 以前也能用。正常运行时修复不应该失败，收到这个状态码应检查硬件或 IO 实现。

## Header 结构

每页 = **12 字节 header + payload**

```
[0..3]  magic   0x46534142
[4..7]  length  payload 大小
[8..11] crc32   payload CRC32
```

## 约束

- 两页地址必须不同、不重叠、页对齐
- 不是线程安全 / 中断可重入
- 最大 payload：`page_size - 12`
- FlashStore 结构体：28 bytes（不含用户 buffer）

## 状态码

| 返回值 | 含义 |
|--------|------|
| `OK` | 正常 |
| `ERROR_ARGUMENT` | 参数错误 |
| `ERROR_WRITE` | 擦除或编程失败 |
| `ERROR_NO_VALID_DATA` | 两页都坏了 |
| `WARN_REPAIR_FAILED` | 数据正确但冗余修复失败 |

## 测试

```bash
cmake -S . -B build && cmake --build build && ./build/flash_store_tests.exe
```

9 个测试覆盖：正常读写、A 坏回退 B、B 坏从 A 修、CRC 裁决新旧、双页全坏、修复失败告警、参数校验、MaxDataSize。

## License

[MIT](LICENSE)
