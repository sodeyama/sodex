# Plan 03: lib.c / string.c の重複解消

## 概要

`src/lib/lib.c` と `src/lib/string.c` に同一関数の重複実装がある。
これを1つに統合し、保守性を改善する。

## 現状の重複

### lib.c の関数
```c
strlen, strcmp, strncmp, strcpy, strncpy, strchr, strrchr, pow, log
```

### string.c の関数
```c
strlen, strcmp, strncmp, strcpy, strncpy, memcpy, memset, memcmp, strtol
```

### 重複関数（両方に存在）
- `strlen` — 実装ロジックはほぼ同一
- `strcmp` — 同一
- `strncmp` — 同一
- `strcpy` — 同一
- `strncpy` — 同一

### lib.c のみ
- `strchr`, `strrchr`, `pow`, `log`

### string.c のみ
- `memcpy`, `memset`, `memcmp`, `strtol`

## リファクタリング計画

### Step 1: 実装の差異確認

両ファイルの同名関数を比較し、動作が同一であることを確認する。
もし差異があれば、どちらが正しいか判断する。

### Step 2: string.c に統合

文字列/メモリ操作関数は `string.c` に集約する:
- `strlen`, `strcmp`, `strncmp`, `strcpy`, `strncpy` — string.c に残す
- `memcpy`, `memset`, `memcmp` — string.c に残す
- `strchr`, `strrchr` — lib.c から string.c に移動
- `strtol` — string.c に残す

### Step 3: lib.c を数学関数専用に

lib.c には数学/ユーティリティ関数のみ残す:
- `pow`, `log`
- 将来的に `abs`, `atoi` 等を追加する場所

### Step 4: lib.c から重複関数を削除

重複する `strlen`, `strcmp`, `strncmp`, `strcpy`, `strncpy` を lib.c から削除。

### Step 5: ビルド確認

```bash
make clean && make
```

リンク時にシンボル重複エラーが出ないことを確認。

## 統合後のファイル構成

### string.c（統合後）
```c
// 文字列操作
PUBLIC int strlen(const char *s);
PUBLIC int strcmp(const char *s1, const char *s2);
PUBLIC int strncmp(const char *s1, const char *s2, int n);
PUBLIC char* strcpy(char *dst, const char *src);
PUBLIC char* strncpy(char *dst, const char *src, int n);
PUBLIC char* strchr(const char *s, int c);      // lib.cから移動
PUBLIC char* strrchr(const char *s, int c);     // lib.cから移動

// メモリ操作
PUBLIC void* memcpy(void *dst, const void *src, int n);
PUBLIC void* memset(void *dst, int c, int n);
PUBLIC int memcmp(const void *s1, const void *s2, int n);

// 変換
PUBLIC long strtol(const char *nptr, char **endptr, int base);
```

### lib.c（統合後）
```c
// 数学関数
PUBLIC double pow(double base, double exp);
PUBLIC double log(double x);
```

## ヘッダファイルの整理

### 現状
- `src/include/string.h` が存在するか確認
- `lib.c` の関数の宣言がどのヘッダにあるか確認

### 整理後
- `src/include/string.h` — 文字列/メモリ操作関数の宣言
- `src/include/math.h`（新規、必要なら） — pow, log の宣言

## リスク

- ビルド時のリンク順序で影響がある可能性
- ユーザー空間（`src/usr/lib/libc/string.c`）にも同名関数があるが、
  これはカーネル空間とは別にリンクされるので影響なし

## 変更ファイル一覧

| ファイル | 変更内容 |
|---------|---------|
| `src/lib/string.c` | strchr, strrchr を追加 |
| `src/lib/lib.c` | strlen, strcmp, strncmp, strcpy, strncpy を削除 |
| `src/include/string.h` | 宣言の整理（必要に応じて） |

## 依存関係

- Plan 02（lib テスト）のテストが通る状態でリファクタリングを開始
- リファクタリング後にテストを再実行して回帰がないことを確認

## 完了条件

- [ ] lib.c と string.c に重複関数がない
- [ ] `make clean && make` が成功する
- [ ] Plan 02 のテストが全てPASS
- [ ] QEMUでカーネルが正常起動する
