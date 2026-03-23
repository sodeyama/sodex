# Plan 06: Network Runtime / Resource Tracking

## 目的

`sxi` runtime に `net` namespace を追加し、socket client / server を
host unit test と QEMU smoke の両方で固定する。

## 背景

Sodex は kernel / userland に socket、poll、DNS を既に持つが、
`sx` からはまだ使えない。
そのため、network を使う用途では shell command や C command に戻る必要がある。

また session reset や REPL の観点では、open socket を runtime が追跡しないと
resource leak を起こしやすい。

## 実装方針

### 1. runtime state

`struct sx_runtime` に socket table を追加する。

- `fd` をそのまま返す
- 返した socket fd を runtime table に登録する
- `net.close`、`io.close`、reset、dispose で table と close を同期する

### 2. host build

`TEST_BUILD` では host POSIX socket を使う。

- `socket` / `bind` / `listen` / `accept` / `connect`
- `send` / `recv`
- `poll`

hostname は最小として `127.0.0.1` と dotted IPv4 を必須にする。

### 3. guest build

guest では既存 userland API を使う。

- `socket` / `bind` / `listen` / `accept`
- `connect`
- `send_msg` / `recv_msg`
- `poll`
- dotted IPv4 は `inet_aton`
- name 解決は `dns_resolve`

### 4. QEMU smoke

- guest client: host server へ接続して応答を読む
- guest server: hostfwd 経由で host client を受ける

片側だけでなく、client / server を分けて検証する。

## テスト方針

### host unit test

- `net.connect` / `net.write` / `net.read` / `net.close`
- `net.listen` / `net.accept` / `net.poll_read`
- socket leak が `sx_runtime_dispose()` で閉じること

### QEMU smoke

- `sxi --check` で network sample を検査
- 実行時に host Python 側で server / client を立てる
- output file と serial marker の両方で結果を確認する

## リスク

### 1. raw fd と cleanup の二重管理

`io.close()` と `net.close()` のどちらでも close されうる。
runtime 側で detach helper を共通化し、double close を避ける。

### 2. QEMU networking の待ち合わせ

guest server の起動タイミングと host client の接続タイミングがずれると、
flake しやすい。
host 側は retry 付きで接続し、guest 側 sample は accept 前に listen 完了まで進める。

### 3. DNS と host build の差

guest は `dns_resolve` を持つ一方、host test は同じ API を持たない。
host test は dotted IPv4 前提に寄せ、guest でのみ DNS branch を許す。

## 実装ステップ

1. `sx_runtime` に socket tracking を追加する
2. `net.*` builtin を host / guest 両方で実装する
3. host runtime test を client / server の両面で追加する
4. QEMU smoke に host side peer を追加する
5. sample と language reference を更新する
