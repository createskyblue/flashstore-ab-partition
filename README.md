[中文](README_CN.md)

# FlashStore A/B Partition

A/B dual-page flash storage for small MCUs. Zero dependencies. 28 bytes RAM.

**Which to choose?**

| | FlashStore | FlashDB | LittleFS |
|------|------------|---------|----------|
| Purpose | Single-blob reliable storage | Embedded key-value DB | Embedded file system |
| Data model | Binary blob | Key-value pairs | Files |
| I/O | Save/Load whole blob | Set/Get by key | Open/Read/Write |
| Wear leveling | ❌ | ✅ | ✅ |
| Power-loss safe | ✅ Dual-page | ✅ Transaction log | ✅ |
| RAM | 28 bytes | Very low | Higher (buffers) |
| Best for | Config data, infrequent writes | Multiple KVs, independent updates | Files, logs, large data |

## Quick start

1. Copy `include/flash_store.h` and `src/flash_store.c` into your project. Add `chacha20` or `xxtea` if you need encryption.
2. Implement 3 callbacks:

```c
bool my_read(void *ctx, uint32_t addr, uint8_t *out, size_t len) {
    memcpy(out, (void *)(uintptr_t)addr, len);  // or HAL read
    return true;
}
bool my_erase(void *ctx, uint32_t addr, size_t len) {
    HAL_FLASH_ErasePage(addr);                  // full page erase
    return true;
}
bool my_program(void *ctx, uint32_t addr, const uint8_t *data, size_t len) {
    // word-by-word program, or memcpy if byte-addressable flash
    return true;
}
```

3. Init and go:

```c
FlashStore store;
FlashStore_Config cfg = {
    .io.read = my_read, .io.erase = my_erase, .io.program = my_program,
    .page_a_address = 0x0800FE00,
    .page_b_address = 0x0800FE80,
    .page_size      = 128,
};
FlashStore_Init(&store, &cfg);
FlashStore_Save(&store, data, len);   // write
FlashStore_Load(&store, data, len);   // read
```

> Need encryption? Use [ChaCha20](src/chacha20.c) — RFC 8439, stream cipher, no alignment requirements. Cortex-M0 -Os: 700B Flash + ~200B stack, 0 static RAM. For extreme resource savings, [XXTEA](src/xxtea.c) — obfuscation only, requires 4-byte alignment and ≥8 bytes. Encrypt externally: `encrypt → FlashStore_Save` / `FlashStore_Load → decrypt`.

## Why data won't get lost

**A is written before B.** At any power-loss moment, at least one valid copy exists in flash.

```
Save:  A first → B second     Load: read both → CRC check → return good → repair bad
```

| Power loss at… | A | B | Load result |
|---------|----|----|----------|
| Idle | v2 | v2 | Returns v2 |
| Writing A | bad | v1 | Returns v1, repairs A |
| A done, before B | v2 (CRC=c2) | v1 (CRC=c1) | CRC differ → A is newer → returns v2, repairs B |
| Writing B | v2 | bad | Returns v2, repairs B next time |
| Both pages bad | bad | bad | `ERROR_NO_VALID_DATA` |

> `WARN_REPAIR_FAILED`? Means erase/program failed during repair. Should not happen in normal operation — check your hardware or IO.

## Header layout

12-byte header + payload per page:

| Offset | Field | Description |
|------|------|------|
| 0 | magic (4B) | `0x46534142` |
| 4 | length (4B) | Payload size |
| 8 | crc32 (4B) | Payload CRC32 |

## Resource usage (Cortex-M0 -Os measured)

| Module | Flash | Static RAM | Stack |
|------|-------|----------|-----|
| flash_store | 596 bytes | 28 bytes | ~20 bytes |
| chacha20 | 700 bytes | 0 | ~200 bytes |
| xxtea | 737 bytes | 0 | ~0 |

> flash_store breakdown: Load 158B / Init 108B / load_page 104B / save_page 92B / Save 66B / CRC32 52B. Link what you use.

## Status codes

| Return | Meaning |
|--------|------|
| `OK` | Success |
| `ERROR_ARGUMENT` | Bad parameter |
| `ERROR_WRITE` | Erase or program failed |
| `ERROR_NO_VALID_DATA` | Both pages are corrupt |
| `WARN_REPAIR_FAILED` | Data OK but redundancy not restored |

## Tests

```bash
cmake -S . -B build && cmake --build build && ./build/flash_store_tests.exe
```

9 tests: round-trip, corrupt A falls back to B, corrupt B repaired from A, CRC staleness detection, both pages corrupt, repair failure warning, arg validation, MaxDataSize.

## License

[MIT](LICENSE)
