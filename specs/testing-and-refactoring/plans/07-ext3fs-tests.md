# Plan 07: ext3fsパース部分のユニットテスト

## 概要

`src/ext3fs.c`（979行）のファイルシステム操作のうち、ディスクI/O非依存の
パース・変換ロジックをテストする。

## テスト可能な部分の分析

### ext3fs.c の関数分類

| 関数 | ディスクI/O | テスト可能性 |
|------|-----------|------------|
| `init_ext3fs()` | あり | 低 |
| `ext3_open()` | あり | 低 |
| `ext3_read()` | あり | 低 |
| `ext3_write()` | あり | 低 |
| `ext3_close()` | なし | 高 |
| `ext3_ls()` | あり | 低 |
| ビットマップ操作 | なし | 高 |
| inode解析 | あり（読み込み後のパースは独立） | 中 |
| パス解析（ディレクトリ走査） | あり | 低 |
| ブロック番号計算（直接/間接） | なし | 高 |

### テスト対象として抽出可能なロジック

1. **ブロック番号計算**: inodeのブロックポインタ（直接/間接/二重間接/三重間接）からデータブロック番号を算出するロジック
2. **ビットマップ操作**: ブロック/inodeビットマップのbit操作
3. **ディレクトリエントリのパース**: バッファ内のext3_dentry構造体の走査
4. **ファイルオフセット→ブロック変換**: ファイル内オフセットから物理ブロック番号への変換

## テストケース

### ブロック番号計算

ext3のinodeには12個の直接ブロックポインタと、間接ポインタがある:
```
i_block[0..11]  : 直接ブロック
i_block[12]     : 間接ブロック（ポインタの配列を指す）
i_block[13]     : 二重間接ブロック
i_block[14]     : 三重間接ブロック
```

```c
TEST(direct_block_index) {
    // ファイルオフセット0〜(12*BLOCK_SIZE-1)は直接ブロック参照
    // offset 0 → i_block[0]
    // offset BLOCK_SIZE → i_block[1]
    // offset 11*BLOCK_SIZE → i_block[11]
}

TEST(indirect_block_index) {
    // offset 12*BLOCK_SIZE以降は間接ブロック参照
    // 間接ブロック内のインデックス計算の正当性確認
}
```

### ディレクトリエントリパース

```c
TEST(parse_dentry_basic) {
    // メモリ内にext3_dentry構造体を手動構築
    // パーサーが正しくname, inode_num, rec_lenを読み取るか確認
}

TEST(parse_dentry_chain) {
    // 複数のdentryが連続するブロックを構築
    // 全エントリを走査できるか確認
}
```

### ビットマップ操作

```c
TEST(bitmap_set_bit) {
    u_int8_t bitmap[128] = {0};
    // ビットN をセット
    // 対応バイトの対応ビットが立っていることを確認
}

TEST(bitmap_clear_bit) {
    u_int8_t bitmap[128];
    memset(bitmap, 0xFF, sizeof(bitmap));
    // ビットN をクリア
    // 対応ビットが0になっていることを確認
}

TEST(bitmap_find_free) {
    u_int8_t bitmap[128] = {0};
    // 空きビットを検索
    // 先頭の0ビット位置が返ること
}
```

## 実装上の課題

### ext3fs.c の関数抽出

現在、ブロック計算やビットマップ操作が `ext3_read()` 等の大きな関数に
直接埋め込まれている場合、まず関数として抽出するリファクタリングが必要。

```c
// 抽出候補
PRIVATE u_int32_t ext3_logical_to_physical_block(
    struct ext3_inode *inode, u_int32_t logical_block);

PRIVATE int ext3_bitmap_test(u_int8_t *bitmap, u_int32_t bit);
PRIVATE void ext3_bitmap_set(u_int8_t *bitmap, u_int32_t bit);
PRIVATE void ext3_bitmap_clear(u_int8_t *bitmap, u_int32_t bit);
PRIVATE u_int32_t ext3_bitmap_find_free(u_int8_t *bitmap, u_int32_t size);
```

### ディスクI/Oのモック化

間接ブロックのテストでは、ポインタテーブルの「読み込み」が必要。
テスト用にメモリ内にブロックデバイスをエミュレートする:

```c
// テスト用ブロックデバイス（メモリ内）
#define TEST_BLOCKS 1024
static u_int8_t test_device[TEST_BLOCKS * BLOCK_SIZE];

// ブロック読み込みのモック
int mock_read_block(u_int32_t block_num, void *buf) {
    memcpy(buf, test_device + block_num * BLOCK_SIZE, BLOCK_SIZE);
    return 0;
}
```

## 変更ファイル一覧

| ファイル | 変更内容 |
|---------|---------|
| `tests/test_ext3fs.c` | 新規。ビットマップ・ブロック計算テスト |
| `src/ext3fs.c` | テスト用の内部関数抽出（必要に応じて） |

## 依存関係

- Plan 01（テストフレームワーク）
- ext3fs.c の内部関数抽出は、テストを書きながら段階的に行う

## 完了条件

- [ ] ビットマップ操作のテストがPASS
- [ ] ブロック番号計算（直接ブロック）のテストがPASS
- [ ] ディレクトリエントリパースのテストがPASS
- [ ] テスト可能な関数が独立した関数として抽出されている
