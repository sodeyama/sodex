# userland server runtime config / audit 調査メモ

**調査日**: 2026-03-16  
**対象**: `USS-10`, `USS-11`  
**目的**: `ssh_*` config の分離方針と、userland server 共通 audit sink の妥当な形を一次資料ベースで決める

---

## 1. 結論

- `/etc/sodex-admin.conf` は当面維持する
- ただし `ssh` / `debug shell` / `http` が直接 `admin_server` の runtime API を読む形はやめ、shared entrypoint として `server_runtime_config` / `server_audit` を置く
- audit は sink を 1 つに保ちつつ、line 自体で daemon を識別できる形を維持する

今回の判断は「config file を今すぐ分割しない」と「API と責務の境界は先に分ける」を両立するもの。

---

## 2. 一次資料から確認したこと

### 2.1 OpenSSH は daemon 専用 config を持つが、段階的な分割も許している

OpenBSD `sshd_config(5)` は、`sshd` が daemon 専用の設定ファイルを持ち、さらに `Include` で設定断片を読み込めることを示している。  
同じ man page には `LogLevel`、`SyslogFacility`、`MaxAuthTries`、`LoginGraceTime` のような daemon 単位の設定も並んでいる。

ここからの判断:

- 専用 config file を持つ設計自体は自然
- ただし `Include` がある時点で、設定境界は「最初から完全分離」だけが正解ではない
- Sodex の現段階では、overlay と起動導線を壊さず `/etc/sodex-admin.conf` を維持しつつ、API 境界だけ先に分けるのが妥当

### 2.2 共通 sink でも daemon 識別子は必要

RFC 5424 は syslog message の header に `APP-NAME` を持ち、送信元アプリケーションを識別できる形を前提にしている。  
つまり audit sink を共通化しても、「どの daemon の event か」が line から分かることは残すべきである。

ここからの判断:

- Sodex でも audit ring / serial 出力の sink は 1 つでよい
- ただし `ssh_*`、`debug_shell_*`、`listener_ready kind=*` のように daemon を line から識別できる形式は維持する

---

## 3. Sodex への適用判断

### 3.1 USS-10

専用 config file への即時分離は見送る。  
理由は次の通り。

- 現状の overlay 生成、`server` / `server-headless` 起動導線、smoke が `/etc/sodex-admin.conf` 前提でそろっている
- OpenSSH でも専用 file の上に `Include` を重ねており、境界整理は段階的に進められる
- 今の Sodex で先に効くのは file 名の変更より、`ssh` が `admin_server` の内部 runtime API に直接ぶら下がらないこと

したがって、今回は file path は維持し、shared module 経由に寄せる。

### 3.2 USS-11

audit sink は共通化する。  
ただし出力文字列の互換性は維持する。

- `debug shell` smoke は `listener_ready kind=debug_shell` を待っている
- `ssh` smoke は `listener_ready kind=ssh` や `ssh_auth_success` を見ている
- 既存の audit line は daemon 識別子を既に含んでいる

そのため、sink の実装だけ共有化し、event 文字列は既存形式を崩さない。

---

## 4. 実装方針

- `src/include/server_runtime_config.h`
  - `ssh` / `debug shell` が読む shared config API を置く
- `src/include/server_audit.h`
  - 共通 audit sink API を置く
- `src/net/server_runtime_config.c`
  - 現段階では `admin_server` runtime の wrapper として置く
- `src/net/server_audit.c`
  - 現段階では `admin_server` audit sink の wrapper として置く

この段階で達成したいことは ownership の全面移設ではなく、依存方向の整理である。  
将来 `/etc/sodex-sshd.conf` のような専用 file に切る場合も、`ssh` 側の call site を再度大きく触らずに済む。

---

## 5. 今回の完了条件との対応

- `USS-10`: `/etc/sodex-admin.conf` 継続を決定し、`ssh` の参照先を shared config API に寄せる
- `USS-11`: `ssh` / `debug shell` / `http` が shared audit API を使う形に寄せる

---

## 6. 参考資料

一次資料のみ。

- OpenBSD `sshd_config(5)`: https://man.openbsd.org/sshd_config.5
- RFC 5424, "The Syslog Protocol": https://www.rfc-editor.org/rfc/rfc5424.html

---

## 7. 補足

「専用 config file が一般的」という点は一次資料から確認できる。  
一方で「Sodex は今すぐ file を分けず API 境界を先に分けるべき」という部分は、上記資料と repo 現状を合わせた設計上の推論である。
