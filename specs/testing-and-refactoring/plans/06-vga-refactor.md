# Plan 06: vga.c のテスタビリティ改善

## 概要

`_kprintf` 等の画面出力関数がVGAハードウェア（0xB8000）に直接書き込むため、
テスト時にモックできない。出力先を抽象化し、テスト時はバッファに書き込めるようにする。

## 現状

### vga.c の構造（204行）

```c
#define VRAM_ADDR 0xC00B8000  // VGA テキストバッファ（仮想アドレス）

PRIVATE char *vram = (char *)VRAM_ADDR;
PRIVATE int cursor_x = 0;
PRIVATE int cursor_y = 0;

PUBLIC void _kputc(char c)
{
    // vram に直接書き込み
    *(vram + (cursor_y * 80 + cursor_x) * 2) = c;
    *(vram + (cursor_y * 80 + cursor_x) * 2 + 1) = gColor;
    // カーソル更新、スクロール処理
}

PUBLIC void _kprintf(char *fmt, ...)
{
    // 可変引数をパースして _kputc() 経由で出力
    // フォーマット: %d, %x, %s, %c 等
}
```

### 問題点

1. `_kputc` がVRAMアドレスに直接書き込む → テスト不可
2. `_kprintf` のフォーマット処理自体はテストしたいがVGA依存で分離できない
3. 他の全モジュールが `_kprintf` を使っている → モック化の影響が大きい

## リファクタリング

### Step 1: 出力バックエンドの抽象化

```c
// 出力先の関数ポインタ
typedef void (*putc_fn)(char c);
PRIVATE putc_fn output_putc = vga_putc;  // デフォルトはVGA

// VGAへの直接出力（既存の_kputc内部ロジック）
PRIVATE void vga_putc(char c)
{
    *(vram + (cursor_y * 80 + cursor_x) * 2) = c;
    *(vram + (cursor_y * 80 + cursor_x) * 2 + 1) = gColor;
    // カーソル更新、スクロール
}

// _kputc は出力バックエンド経由
PUBLIC void _kputc(char c)
{
    output_putc(c);
}

// テスト用: 出力先を差し替え
PUBLIC void set_output_backend(putc_fn fn)
{
    output_putc = fn;
}
```

### Step 2: _kprintf のフォーマット処理を分離

```c
// フォーマット処理のコア（出力先非依存）
// vsnprintf 的な関数を新設
PUBLIC int _kvsnprintf(char *buf, int size, const char *fmt, va_list ap);

// _kprintf は _kvsnprintf + _kputs で実装
PUBLIC void _kprintf(char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    _kvsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    _kputs(buf);
}
```

これにより `_kvsnprintf` は純粋な文字列変換関数となり、
ホスト側でテスト可能になる。

### Step 3: _kvsnprintf のテスト

```c
// test_vga.c
TEST(kprintf_decimal) {
    char buf[64];
    _kvsnprintf(buf, sizeof(buf), "%d", 42);
    ASSERT_STR_EQ(buf, "42");
}

TEST(kprintf_hex) {
    char buf[64];
    _kvsnprintf(buf, sizeof(buf), "%x", 0xFF);
    ASSERT_STR_EQ(buf, "ff");
}

TEST(kprintf_string) {
    char buf[64];
    _kvsnprintf(buf, sizeof(buf), "hello %s", "world");
    ASSERT_STR_EQ(buf, "hello world");
}

TEST(kprintf_mixed) {
    char buf[64];
    _kvsnprintf(buf, sizeof(buf), "%s=%d(0x%x)", "val", 255, 255);
    ASSERT_STR_EQ(buf, "val=255(0xff)");
}

TEST(kprintf_negative) {
    char buf[64];
    _kvsnprintf(buf, sizeof(buf), "%d", -1);
    ASSERT_STR_EQ(buf, "-1");
}
```

## 影響範囲

`_kprintf` は全モジュールで使われているが、関数シグネチャは変わらないため
呼び出し側の変更は不要。内部実装のリファクタリングのみ。

## 変更ファイル一覧

| ファイル | 変更内容 |
|---------|---------|
| `src/vga.c` | 出力バックエンド抽象化、_kvsnprintf分離 |
| `src/include/vga.h` | `_kvsnprintf`, `set_output_backend` 宣言追加 |
| `tests/test_vga.c` | 新規。フォーマット処理テスト |

## 完了条件

- [ ] `_kvsnprintf` が純粋な文字列変換関数として動作する
- [ ] %d, %x, %s, %c のフォーマットテストがPASS
- [ ] 既存の `_kprintf` の動作が変わらない
- [ ] QEMUで画面出力が正常に動作する
