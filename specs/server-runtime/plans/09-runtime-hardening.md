# Plan 09: Runtime Hardening

## 概要

`server-runtime` の spec 本体が閉じた後に見えた運用 hardening をまとめる。
対象は `SRT-11` から `SRT-13`、すなわち retry 情報の client 可視化、起動設定の fail-safe、
QEMU/Docker smoke を含む異常系回帰の固定である。

## 現在地

2026-03-16 時点で、admin / HTTP / Docker/headless / QEMU smoke の基本導線は動いている。
一方で次の 3 点が運用上の弱い点として残っていた。

- throttle 応答に retry 時間がなく、client 側で再試行待ちを決めにくい
- `/etc/sodex-admin.conf` の不備や token 欠落が ready marker から読み取れない
- smoke が happy path 中心で、`403` / `429` / 回復系の回帰が薄い

## 目標

- HTTP `429` と text protocol の `ERR throttled` で retry 時間を返す
- config 不備や token 欠落時の挙動を audit / ready marker / test で固定する
- QEMU と Docker の smoke で deny / throttle / 回復を継続的に検知できる

## スコープ

### 今回入れるもの

- `admin_server` / `http_server` / `debug_shell_server` の error 応答整理
- `/etc/sodex-admin.conf` parser の invalid line / read error の audit 化
- ready marker への `stok` / `ctok` / `cfgerr` 追加
- overlay generator と smoke script の異常系拡張
- host test での retry / config error / ready marker 検証

### 今回入れないもの

- allowlist の複数 peer / CIDR 対応
- CI workflow 自体の追加
- SSH の known_hosts / fingerprint 固定

## 設計判断

- retry 時間は内部 tick ではなく秒へ丸めて返す
- config の invalid line は fail-open で無視するが、audit に残して `cfgerr` を増やす
- status/control token 欠落時は listener は起動したままにし、ready marker で `off` を見せる
- config file read failure / size over も `cfgerr` に含める
- deny / throttle の挙動は QEMU と Docker で同じ smoke 観点を持たせる

## 実装ステップ

1. retry-after を計算する helper を共通化し、HTTP/text/debug shell に流す
2. config parser を line 単位で audit し、invalid reason と key を残す
3. ready marker と auth_config audit に token 状態と config error 数を載せる
4. overlay generator で意図的な config 不備を注入できるようにする
5. QEMU/Docker smoke に `403` / `429` / 回復確認を追加する
6. host test に retry / config error / ready marker の回帰を追加する

## テスト

- `tests/test_admin_parser`
- `tests/test_http_server_parser`
- `tests/test_debug_shell_parser`
- `make test-qemu-server`
- `make test-docker-server`

## 変更対象

- `src/include/admin_server.h`
- `src/net/admin_server.c`
- `src/net/http_server.c`
- `src/net/debug_shell_server.c`
- `src/test/write_server_runtime_overlay.py`
- `src/test/run_qemu_server_smoke.py`
- `src/test/run_docker_server_smoke.py`
- `tests/test_admin_parser.c`
- `tests/test_http_server_parser.c`
- `specs/server-runtime/TASKS.md`
- `specs/server-runtime/README.md`

## 完了条件

- [x] throttled 応答で retry 時間を client が読める
- [x] config 不備と token 状態が audit / ready marker / test で固定される
- [x] QEMU/Docker smoke が deny / throttle / recovery を回帰で拾える
