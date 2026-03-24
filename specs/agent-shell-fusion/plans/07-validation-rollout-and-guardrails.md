# Plan 07: validation / rollout / guardrails

## 概要

agent-shell fusion は UX が広範囲に跨るため、
最後にまとめて hardening を入れないと危ない。

本 Plan では、feature flag、shadow routing、性能計測、
host/QEMU smoke、docs を揃えて rollout 可能な状態にする。

## 初期 scope

- feature flag と既定 mode の設定化
- shadow routing による誤判定観測
- redraw / latency / attach の性能計測
- end-to-end smoke
- 操作説明と既知制約の文書化

## 非ゴール

- telemetry サービス連携
- usage analytics の外部送信
- 全 app の完全サポート

## kernel profile と feature flag

設定候補:

```text
/etc/agent/term.conf
  fusion_enabled=true|false
  default_mode=auto|shell|agent
  drawer_mode=transient|pinned
  pty_write_default=deny|ask
```

rollout 軸は 2 段に分ける。

- kernel profile
  - `classic`
  - `agent`
- userland flag
  - `fusion_enabled`

意味:

- profile=`classic`
  - 常に既存 `/usr/bin/term`
- profile=`agent`, `fusion_enabled=false`
  - `/usr/bin/agent-term` は起動するが融合機能は限定
- profile=`agent`, `fusion_enabled=true`
  - full feature 有効

これで「kernel 設定で terminal を切り替える」と
「agent-term 内で段階 rollout する」を両立できる。

## shadow routing

誤ルーティングを減らすため、
最初は「実行は shell のまま、agent 判定だけ記録する」shadow mode を持つ。

記録項目:

- input
- decided route
- reason
- actual user correction

これにより、自然言語判定のミスを後から見直せる。

## 性能観測

最低限見る指標:

- route 判定時間
- drawer open/close 時の redraw 数
- suggestion 表示時の input latency
- attach 中の terminal scroll / resize 影響
- `vi` preview 表示時の再描画コスト

## E2E smoke

代表シナリオ:

1. shell fast path
2. typo suggestion
3. `@` から agent session 開始
4. agent proposal -> approve -> shell 実行
5. PTY observe
6. `vi` `:AgentEdit`

可能なら 1 つの orchestrator で回す。

## docs

更新対象:

- repo `README.md`
- `specs/agent-shell-fusion/`
- guest help text
- `term` 操作ヘルプ
- `vi` command help

明文化すべき制約:

- shell fast path 優先
- destructive command は自動実行しない
- PTY write は approval 必須
- `vi` では native command を優先し、PTY 注入は既定 deny

## 実装ステップ

1. 設定ファイルを追加する
2. shadow routing を実装する
3. metrics を追加する
4. E2E smoke を作る
5. docs を更新する

## 変更対象

- 既存
  - `src/usr/command/term.c`
  - `src/usr/command/vi.c`
  - `README.md`
- 新規候補
  - `src/rootfs-overlay/etc/agent/term.conf`
  - `src/test/run_qemu_agent_shell_fusion_smoke.py`
  - `tests/test_agent_shell_fusion_metrics.c`

## 検証

- host
  - config parse
  - shadow routing log
  - metrics counters
- QEMU
  - profile=`classic` で従来 `term` が維持される
  - profile=`agent` + flag off で `agent-term` の安全縮退が働く
  - profile=`agent` + flag on で unified input が有効になる
  - E2E smoke が通る

## 完了条件

- kernel profile と feature flag を切り替えられる
- shadow mode で誤判定を観測できる
- 性能退化を継続観測できる
- docs / smoke / guardrails を含めて rollout 準備が整う
