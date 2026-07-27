# FlashStore A/B Partition

一个面向小型 MCU 配置数据的 A/B 双页 Flash 存储示例。核心库不依赖具体 HAL，通过三个回调接入读取、擦除和整页写入；仓库同时提供 Air001 HAL 接入示例。

它适合保存几十到几百字节、低频更新的固定结构体。需要动态 KV、频繁写入、磨损均衡或时序数据时，应选择 FlashDB 等完整存储组件。

## 工作方式

- 每页包含魔数、数据长度、CRC32 和 payload。
- 保存时先更新 A 页，再更新 B 页。
- A 页更新失败时不触碰 B 页，旧数据仍可读取。
- 加载时先校验 A 页；A 页无效时回退 B 页。
- `key` 只做确定性的 XOR 混淆，不提供密码学安全性。

本实现沿用原项目 `Lib/FlashStore` 的双页冗余思路，并改成可测试的 HAL 无关接口。它没有代次号，也不在两页之间轮流选择最新记录；两页保存同一份数据，A 为主副本，B 为回退副本。

## 接入

将以下文件加入工程：

```text
include/flash_store.h
src/flash_store.c
```

实现 `FlashStore_IO` 的三个回调，然后提供两个独立可擦除页和一个页大小的静态工作区。完整的 Air001 接法见 [`examples/air001_example.c`](examples/air001_example.c)。

```c
FlashStore store;
uint8_t workspace[128];

FlashStore_Config config = {
    .io = {
        .read = board_flash_read,
        .erase = board_flash_erase,
        .program = board_flash_program,
    },
    .page_a_address = PAGE_A_ADDRESS,
    .page_b_address = PAGE_B_ADDRESS,
    .page_size = sizeof(workspace),
    .workspace = workspace,
    .workspace_size = sizeof(workspace),
};

FlashStore_Init(&store, &config);
FlashStore_Save(&store, key, (const uint8_t *)&settings, sizeof(settings));
FlashStore_Load(&store, key, (uint8_t *)&settings, sizeof(settings));
```

## 约束

- 两个地址必须属于互不重叠的独立擦除页，并从链接脚本的应用区排除。
- `program` 回调必须完成整页写入；不同芯片的页写 API 和对齐要求需要在回调内处理。
- 工作区会在调用期间被改写，接口不是线程安全或中断可重入的。
- payload 最大长度为 `page_size - 12` 字节。
- 结构体布局变化需要由应用层处理版本兼容。

## 测试

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

主机端测试覆盖正常读写、主副本损坏回退，以及主副本更新失败后保留旧副本。

## License

[MIT](LICENSE)
