# sodex

sodex は完全自作の実験用 OS です。もともとは 2006-2007 年頃に作ってました。
今はなきフロッピーデバイスとUSBで実機ブート出来てました。

## 元々できていたこと（2006-2007 年の初期実装）

- i486 protected mode カーネル（GDT、IDT、ページング、PIT）
- 独自ブートローダ（FDD / USB の 2 段階ブート → カーネルロード）
- 物理 / 仮想メモリ管理（カーネル 0xC0000000 ハイアーハーフ配置）
- プロセス管理（TSS タスクスイッチ、ELF ローダ、`execve`、`fork`、シグナル）
- ext3 ファイルシステム（inode、ディレクトリ操作、読み書き）
- VGA テキストモード画面出力
- キーボード入力、シリアル（RS-232C）出力
- 基本シェル（`eshell`）と userland コマンド（`ls`、`cat`、`ps`、`kill`、`pwd`）
- デバイスドライバ（PCI、DMA、FDC、UHCI USB、SCSI、マスストレージ、NE2000 NIC）
- uIP TCP/IP スタック（ARP、ルーティング）
- userland libc（`malloc`、`printf`、`string`、`regex`、`sbrk`）
- システムコールインターフェース
- ビルドツール（`kmkfs` ディスクイメージ作成、`getsize`）

## 現在できること

**カーネル & ブート**
- i486 32-bit protected mode カーネルが QEMU 上で boot する
- 独自ブートローダチェーン（第1段階 → 中間段階 → カーネル）
- ページング（PSE 4MB ページ対応）、GDT/IDT、PIT、メモリ管理を独自実装
- プロセス管理（TSS タスクスイッチ、fork、execve、exit、シグナル）
- ELF バイナリローダ

**グラフィカルターミナル**
- framebuffer 上に描画する rich terminal（1024x768+ 解像度）
- VT100/ANSI エスケープシーケンス対応（CSI、SGR、カーソル移動、DECSTBM スクロール領域）
- TTY/PTY ベースの shell（パイプ `|`、リダイレクト `>` `<` `>>`、クォート処理）
- UTF-8 表示、East Asian Width 対応
- 日本語 IME（ローマ字 → ひらがな → カタカナ / 漢字変換、大規模辞書、blob キャッシュ）
- ダメージベースの部分再描画による最適化

**ファイルシステム & コマンド**
- ext3 ファイルシステム（inode、ディレクトリ CRUD、single/double indirect block で 5MB+ の大ファイル対応）
- コマンド群: `ls`（カラー対応）、`cat`、`touch`、`mkdir`、`rm`、`rmdir`、`mv`、`cd`、`pwd`、`ps`、`kill`、`sleep`、`find`、`sort`、`uniq`、`wc`、`head`、`tail`、`grep`、`sed`、`awk`、`cut`、`tr`、`diff`、`tee`
- `vi` エディタ（normal/insert/command モード、undo/redo、検索 `/` `?`、visual 選択、UTF-8 / 日本語入力対応）
- `curl`（HTTP/HTTPS クライアント、BearSSL TLS、chunked encoding 対応）
- `websearch`（SearXNG 経由の Web 検索）、`webfetch`（URL 本文取得、allowlist / truncation）
- `ping`（ICMP echo）、`dig`（DNS lookup）
- `agent`（LLM エージェント CLI: REPL、セッション永続化、メモリストア、ツール呼び出し、Claude API 連携）
- `ask`（Claude API への直接クエリ）
- `sxi`（`sx` source interpreter: `fn` / `let` / block / `if` / `while` / `for` / `break` / `continue` / `return`、unary / binary operator、assignment、`str` / `bool` / `i32`、`io` / `fs` / `proc` / `json` / `text` / `time` / `bytes` / `list` / `map` / `result`、relative `import`、`argv` / env / fd I/O / binary I/O / cwd / `spawn` / `wait` / `pipe` / `spawn_io` / `fork` / `exit` / `try_*` helper、`--check`、`-e`、REPL、runtime stack trace）
- guest image に `/home/user/sx-examples/README.md` と hello / import / operator / loop / scope / recursion / stdin / grep-lite / json / fs / argv / spawn / pipe / fork / env / bytes / list / map / result / literal / network の `sx` サンプルを同梱
- 既定のホーム / 起動ディレクトリは `/home/user`
- agent built-in file tools（`read_file` `write_file` `rename_path` `list_dir`）
  - 相対 path は current directory 基準で解決される
  - `standard` mode の書き込み先は主に `/home/user`、`/tmp`、`/var/agent`

**ネットワーク & サーバランタイム**
- uIP TCP/IP スタック（ARP、DNS、TCP CLOSE_WAIT 対応）
- NE2000 NIC ドライバ
- SSH サーバ（curve25519 鍵交換、Ed25519 host key、パスワード認証 3 回制限、PTY セッション）
- host (macOS) から README 記載の `ssh` 手順で guest shell に接続可能
- HTTP サーバ（port 8080）、Admin サーバ（port 10023、JSON 設定、ランタイム制御）、Debug Shell（port 10024）
- host 側の structured Web fetch gateway と、guest 側 `webfetch` / agent `fetch_url` tool
- 監査ログ（audit logging）

**デバイスドライバ**
- PCI バス列挙、ATA/IDE ディスク
- UHCI USB 1.1 ホストコントローラ、USB マスストレージ、SCSI コマンドセット
- NE2000 NIC、DMA、RS-232C シリアル
- BOCHS VGA（BGA グラフィクスモード）、VGA テキストモード
- FDC フロッピーディスク（レガシー）

**サーバランタイム & デプロイ**
- headless モード（`bin/start.sh server-headless`）で GUI なし運用
- Docker コンテナ上での server runtime 常駐（ARM64/x86_64 対応）
- ポートフォワーディングで host から全サーバにアクセス可能
- init スクリプト（`/etc/init.d/`）によるサービス起動管理

**暗号 & セキュリティ**
- AES、SHA-256、Curve25519、Ed25519、Poly1305（TweetNaCl）
- BearSSL による TLS（HTTPS クライアント用）
- トークン認証、IP allowlist

**userland libc**
- 独自実装の C ライブラリ: `malloc`/`free`、`printf`/`sprintf`/`fprintf`、文字列操作、ファイル I/O
- `regex`、`math`、`inet`、`sbrk`/`brk`
- UTF-8 コーデック、East Asian Width（`wcwidth`）
- VT100 パーサ、ターミナルサーフェス（セルバッファ、スクロールバック）
- シェルパーサ（トークナイザ、パイプラインパーサ、スクリプト実行）

**テスト**
- host 側 unit test（40 以上のテストバイナリ）
- QEMU integration test（boot、メモリスケーリング、FS、shell I/O、Unix text command、vi、IME、SSH、HTTPS、websearch、webfetch、agent）
- Docker server runtime smoke test

## まだやっていないこと

- GUI やウィンドウシステム（現在はフレームバッファ上のテキストターミナルのみ）
- `vi` の複数バッファ、text object、マクロ
- IME の予測変換、学習辞書
- shell のタブ補完（実装進行中）
- マルチユーザー / アクセス制御
- 一般ユーザー向けの install 手順や配布物の整備

## 必要なもの

- `python3`
- `qemu-system-i386`
- `x86_64-elf-gcc`, `x86_64-elf-as`, `x86_64-elf-ld`, `x86_64-elf-ar`
- `g++`

この repo の既定 `makefile.inc` は cross toolchain を `/opt/homebrew/bin` に置く前提です。必要ならローカル環境に合わせて [makefile.inc](makefile.inc) を調整してください。

## ビルド

```sh
make
```

生成物は主に `build/` 配下に出ます。QEMU 起動に使うディスクイメージは `build/bin/fsboot.bin` です。

## 起動

```sh
bin/start.sh
```

通常の `bin/start.sh` は `-display cocoa` を使うため、macOS 前提です。headless 実行は `bin/start.sh server-headless` を使ってください。
既定の QEMU RAM は `512MB` です。`SODEX_QEMU_MEM_MB=1024 bin/start.sh` のように上書きできます。

ネットワーク付きで起動したい場合:

```sh
bin/start.sh net
```

server runtime を headless で起動したい場合:

```sh
bin/start.sh server-headless
```

`server` / `server-headless` は既定で SSH port forward も有効にします。
無効にしたいときは `--no-ssh` を付けてください。
`--ssh` を付けて起動した場合は、必要なら起動前に対応 overlay 付きの `fsboot.bin` を自動再生成します。
通常 GUI 付きで terminal を出したいときは `bin/start.sh --ssh`、headless の server runtime は `bin/start.sh server-headless` を使ってください。

Docker 上で server runtime を常駐させたい場合:

```sh
docker build -f docker/server-runtime/Dockerfile -t sodex-server-runtime .
mkdir -p build/log/server-runtime
docker run --rm \
  -p 18080:18080 \
  -p 10023:10023 \
  -p 10022:10022 \
  -v "$(pwd)/build/log/server-runtime:/var/log/sodex" \
  sodex-server-runtime
```

この image は container 内で `i686-linux-gnu` cross toolchain を使って guest を build します。
Apple Silicon を含む arm64 host でも native image として build / run できます。

published port の peer IP は host により異なります。
Linux host の既定は `10.0.2.2`、Docker Desktop/macOS では `192.168.65.1` を確認しました。
allowlist を変えるときは `SODEX_ADMIN_ALLOW_IP=...` を指定してください。

ready 条件は container の serial/stdout に `AUDIT server_runtime_ready ...` が出た時点です。
`qemu_debug.log` と `monitor.sock` は mount した `/var/log/sodex` 配下に出ます。
entrypoint は bind mount した log dir の owner で build / 起動するので、
`mkdir -p build/log/server-runtime` のように先に作っておけば root 所有物を減らせます。

`/dev/kvm` を使える Linux なら、`--device /dev/kvm -e SODEX_QEMU_ACCEL=kvm` を追加できます。

## テスト

host 側 unit test:

```sh
make test
```

host 側 web fetch gateway test:

```sh
make test-web-fetch-host
```

kernel integration test:

```sh
make test-qemu
```

主な QEMU smoke test:

```sh
make -C src test-qemu-memory
make -C src test-qemu-user-memory
make -C src test-qemu-term
make -C src test-qemu-fs
make -C src test-qemu-shell-io
make -C src test-qemu-unix-text-tools
make -C src test-qemu-sxi
make -C src test-qemu-sxi-agent
make -C src test-qemu-vi
make -C src test-qemu-ime
make test-qemu-websearch
make test-qemu-webfetch
make test-agent-full
```

`make test-qemu-webfetch` は host 側 mock source + gateway を起動し、
guest の `webfetch` 成功 / truncation / allowlist 拒否を確認します。
`make -C src test-qemu-sxi-agent` は `write_file` / `run_command` を使う
`sx` script の write / `sxi --check` / run / fix loop を QEMU で確認します。
`make test-agent-full` は `fetch_url` を含む agent integration 全体を QEMU で通します。

Docker/headless server runtime smoke:

```sh
make test-docker-server
```

ポート衝突を避けたいときは `SODEX_HOST_HTTP_PORT` / `SODEX_HOST_ADMIN_PORT` を上書きできます。
Docker Desktop/macOS では `SODEX_ADMIN_ALLOW_IP=192.168.65.1 make test-docker-server` を使います。

QEMU debug shell smoke:

```sh
make test-qemu-debug-shell
```

debug shell の guest port は `10024` です。
host 側ポートを変えたいときは `SODEX_HOST_DEBUG_SHELL_PORT=11024 make test-qemu-debug-shell` のように上書きできます。

raw TCP client から手で試すときの最小例:

```sh
printf 'TOKEN control-secret\n' | nc 127.0.0.1 10024
```

返答は最初に `OK shell` が 1 行返り、その後は line protocol ではなく raw stream に切り替わります。
対話用途では `nc` より `socat` の方が扱いやすいです。

```sh
socat -,rawer,echo=0 TCP:127.0.0.1:10024
```

接続したら最初の 1 行で `TOKEN control-secret` を送り、以後は shell 入力をそのまま流します。

QEMU SSH smoke:

```sh
make test-qemu-ssh
```

host 側ポートを変えたいときは `SODEX_HOST_SSH_PORT=11022 make test-qemu-ssh` のように上書きできます。

手で試すときは、まず overlay を含めて guest を SSH 付きで作り直して起動します。

```sh
bin/restart.sh server-headless
```

GUI terminal も使いたい場合は次でも構いません。

```sh
bin/restart.sh --ssh
```

その後、別ターミナルから接続します。
現時点では最小実装なので、auth method と host key 確認を明示した `ssh -tt` を前提にします。

```sh
ssh -tt -F /dev/null \
  -o PreferredAuthentications=password \
  -o PubkeyAuthentication=no \
  -o StrictHostKeyChecking=no \
  -o UserKnownHostsFile=/dev/null \
  -p 10022 root@127.0.0.1
```

既定 password は `root-secret` です。`SODEX_SSH_PASSWORD` を export している場合はその値で上書きされます。
password 認証は 1 接続あたり 3 回失敗すると切断されます。
login 後は `sodex /home/user>` prompt が出るので、`ls` / `pwd` / `cat` を実行でき、
`Backspace`, `Ctrl-C`, `exit` も通ります。

`make -C src test-qemu-memory` は `128/256/512/1024MB` の memory scaling matrix を回します。
`make -C src test-qemu-user-memory` は shell 経由で `memgrow` を起動し、`execve` と `malloc/brk` の userland 回帰を確認します。
guest が使う RAM を論理的に絞りたいときは、`SODEX_RAM_CAP_MB=256 make -C src test-qemu-memory` のように cap を付けられます。

## Web Fetch Gateway

host 側には `src/tools/web_fetch_gateway.py` を置き、guest からは `webfetch` command と
agent の `fetch_url` tool で叩けます。`websearch` は URL 探索、`webfetch` / `fetch_url` は
既知 URL の本文取得、`curl` は raw HTTP 確認という分担です。

`bin/start.sh` / `bin/restart.sh` の `user` / `server` / `server-headless` では、
host 側 gateway を自動起動します。既定の guest 接続先は `10.0.2.2:8081/fetch` です。
`net` mode では `10.0.2.2` を使えないため、必要なら `webfetch -h <host-ip>:8081 ...` で上書きしてください。

手で起動する最小例:

```sh
SODEX_WEBFETCH_ALLOWLIST='*' \
python3 src/tools/web_fetch_gateway.py 8081
```

JSON 設定を使う場合:

```sh
cp src/tools/web_fetch_config.example.json /tmp/webfetch.json
SODEX_WEBFETCH_CONFIG=/tmp/webfetch.json \
python3 src/tools/web_fetch_gateway.py
```

主な環境変数:

- `SODEX_WEBFETCH_ALLOWLIST`
- `SODEX_WEBFETCH_TIMEOUT_MS`
- `SODEX_WEBFETCH_MAX_BYTES`
- `SODEX_WEBFETCH_DEFAULT_MAX_CHARS`
- `SODEX_WEBFETCH_MAX_CHARS_LIMIT`
- `SODEX_WEBFETCH_ALLOWED_TYPES`
- `SODEX_WEBFETCH_RENDER_JS_COMMAND`

## リポジトリの見どころ

- [src](src)
  カーネル、userland、terminal、`vi`、TTY/PTY、ドライバ、ネットワークスタック
- [src/usr/command](src/usr/command)
  ユーザー空間コマンド群（`eshell`、`vi`、`term`、`curl`、`agent` 等）
- [src/usr/lib/libc](src/usr/lib/libc)
  独自 userland libc（UTF-8、IME、VT パーサ、シェルパーサ等）
- [src/net](src/net)
  uIP TCP/IP スタック、SSH サーバ、HTTP サーバ、Admin サーバ
- [src/drivers](src/drivers)
  デバイスドライバ（PCI、ATA、UHCI USB、NE2000、BGA 等）
- [tests](tests)
  host 側 unit test
- [specs/rich-terminal/README.md](specs/rich-terminal/README.md)
  terminal / shell / `vi` / UTF-8 / IME 拡張の全体計画
- [specs/agent-transport/README.md](specs/agent-transport/README.md)
  LLM Agent Transport 設計
- [specs/memory-scaling/README.md](specs/memory-scaling/README.md)
  QEMU の RAM 量と guest の allocator / paging をつなぐ拡張計画

## 現状の読み方

ルート README は「今この repo が何をできるか」の要約です。詳細な設計判断や roadmap は [specs/rich-terminal/README.md](specs/rich-terminal/README.md) と [specs/memory-scaling/README.md](specs/memory-scaling/README.md) を参照してください。

## 過去の開発ログ

初期の `sodex` 開発時のメモや実装記録は、昔のブログ記事にも残っています。

- 記事一覧: https://sodex.hatenablog.com/archive/category/sodex

開始とリリース:

- 最初の記事: https://sodex.hatenablog.com/entry/20070406/1175809589
  開発の出発点と、当時の開発環境のメモです。
- `Sodex 0.0.1リリース`: https://sodex.hatenablog.com/entry/20071007/1191695763
  初期リリース時点での到達状況が分かります。

プロセス、ELF、シェル:

- `OS作成 - ページング＆プロセス編`: https://sodex.hatenablog.com/entry/20070713/1184312942
  `execve` とプロセス生成に手が入った時期の記事です。
- `OS作成 - elfローダ編`: https://sodex.hatenablog.com/entry/20070725/1185335342
  ELF ローダとユーザプロセス配置の話です。
- `OS作成 - シェル編`: https://sodex.hatenablog.com/entry/20070924/1190655818
  最初期の shell が動き始めた頃の記事です。

デバイス、ストレージ、boot:

- `OS作成 - NIC編`: https://sodex.hatenablog.com/entry/20080117/1200584746
  NE2000 NIC ドライバまわりの試行錯誤です。
- `usb mass storage`: https://sodex.hatenablog.com/entry/20090729/1248864705
  USB mass storage から 1 セクタ読めた時点の記録です。
- `USB boot`: https://sodex.hatenablog.com/entry/20090814/1250228357
  USB boot を詰めていた時期の記事です。

## フォントとライセンス

rich terminal の UTF-8 / 日本語表示では、`UDEV Gothic` を元に生成した bitmap font pack を使っています。

- 元フォントのライセンスは `SIL Open Font License 1.1`
- kernel には生の TTF/OTF ではなく、生成済み font pack を組み込む構成です
- ライセンス文書は [third_party/fonts/OFL-UDEV-Gothic.txt](third_party/fonts/OFL-UDEV-Gothic.txt) にあります
