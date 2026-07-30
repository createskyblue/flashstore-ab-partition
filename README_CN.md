[English](README.md)

# FlashStore A/B Partition

A/B 双页 Flash 存储，面向小型 MCU。零依赖，无状态。

https://github.com/createskyblue/flashstore-ab-partition

**选哪个？**

| | FlashStore | FlashDB | LittleFS |
|------|------------|---------|----------|
| 定位 | 单块配置可靠存储 | 嵌入式键值数据库 | 嵌入式文件系统 |
| 数据模型 | 整块二进制 | 键值对 | 文件 |
| 读写方式 | Save/Load 整块 | Set/Get 按 key | Open/Read/Write |
| 磨损均衡 | ❌ | ✅ | ✅ |
| 掉电保护 | ✅ 双页冗余 | ✅ 事务日志 | ✅ |
| 适用 | 配置参数，低频更新 | 多配置项独立读写 | 文件、日志、大块数据 |

## 资源占用（Cortex-M0 -Os 实测）

| 模块 | Flash | 静态 RAM | 栈 |
|------|-------|----------|-----|
| flash_store | 500 bytes | 0 | ~20 bytes |
| chacha20（可选） | 700 bytes | 0 | ~200 bytes |
| xxtea（可选） | 737 bytes | 0 | ~0 |

> flash_store 明细：Load 158B / load_page 108B / save_page 100B / Save 66B / CRC32 52B / MaxDataSize 16B。按需链接，不用就不占。

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
FlashStore_IO io = {
    .read = my_read, .erase = my_erase, .program = my_program,
};
FlashStore_Config cfg = {
    .io = &io, .page_a_address = 0x0800FE00, .page_b_address = 0x0800FE80,
    .page_size = 128,
};
FlashStore_Save(&cfg, data, len);   // 写
FlashStore_Load(&cfg, data, len);   // 读
```

> 需要加密？推荐 [ChaCha20](src/chacha20.c)——RFC 8439 标准，流密码不分块不要求对齐。实测 Cortex-M0 -Os：700B Flash + ~200B 栈，0 静态 RAM。极致省资源用 [XXTEA](src/xxtea.c)，仅混淆级别，要求 4 字节对齐且 ≥8 字节。加密在外部做：`encrypt → FlashStore_Save` / `FlashStore_Load → decrypt`。

## 为什么不会丢数据

**A 写完才写 B。** 任意时刻断电，至少一份完整数据在 flash 上。

```
Save:  A 先 → B 后     Load: 读两页 → CRC 验 → 返回好的 → 顺手修坏的
```

| 断电在… | A | B | Load 结果 |
|---------|----|----|----------|
| 空闲 | v2 | v2 | 返回 v2 |
| 写 A 中途 | 坏 | v1 | 返回 v1，修 A |
| A 完成，B 前 | v2 (CRC=c2) | v1 (CRC=c1) | CRC 不同→A 新→返回 v2，修 B |
| 写 B 中途 | v2 | 坏 | 返回 v2，下次修 B |
| 两页都坏 | 坏 | 坏 | `ERROR_NO_VALID_DATA` |

> `WARN_REPAIR_FAILED`？修复时 erase/program 失败，正常不会发生——收到应检查硬件。

## Header 结构

每页 12 字节 header + payload：

| 偏移 | 字段 | 含义 |
|------|------|------|
| 0 | magic (4B) | `0x46534142` |
| 4 | length (4B) | payload 大小 |
| 8 | crc32 (4B) | payload CRC32 |

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

10 个测试覆盖：正常读写、A 坏回退 B、B 坏从 A 修、A 写失败保 B、CRC 裁决新旧、双页全坏、MaxDataSize、Save/Load 参数校验、ConfigCheck、修复失败告警。

## License

[MIT](LICENSE)
