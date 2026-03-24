# agent-shell 融合リサーチメモ

**調査日**: 2026-03-24  
**対象**: ログイン後ターミナルを agent-first に再設計する構想  
**目的**: 「普通のコマンド入力はそのまま shell として動き、自然言語や typo 回復や長手数タスクは agent が吸収する」形の実装方針を、現行プロダクトと Sodex の前提を踏まえて整理する

---

## 1. 結論

- 2026-03-24 時点の現行プロダクトを見ると、成功しているのは「shell を消す」設計ではなく、**同じ入力面に shell と agent を同居させつつ、モード境界と権限境界を明示する**設計である
- typo 補正は有望だが、既存の実装はほぼすべて **suggestion 型** であり、無言で自動実行する設計は主流ではない
- interactive CLI や `vim`/`vi` への agent 介入は既に成立しているが、実用上は
  - PTY に attach して live buffer を読む方式
  - 選択範囲 + diff preview で編集する方式
  の二系統に分かれている
- Sodex では、既に `term`、PTY/TTY、`eshell`、`vi`、`agent` CLI があるので、**agent 置換 shell** ではなく **agent を前提にした terminal client** として進めるのが最短である
- `vi` への統合は、最初から自由入力 PTY 操作に寄せるより、**visual selection / command mode / diff apply** の三点から始める方が破綻しにくい

以下の詳細評価には、Web 一次資料を踏まえた推論を含む。

---

## 2. Sodex 側の前提

repo 現状を見ると、今回の構想に必要な土台はかなり揃っている。

- `README.md` 上で、rich terminal、PTY ベース shell、`vi`、`agent`、`ask`、`websearch`、`webfetch` が既に到達点として明記されている
- `specs/rich-terminal/README.md` 上でも、terminal client が userland にあり、shell と表示が PTY で分離された構造になっている
- つまり Sodex は
  - command input surface
  - shell parser / exec
  - full-screen editor
  - agent runtime
  を既に別層として持っており、構想の本質は「新規 OS 機能の発明」より **ルーティングと UX の再設計** である

この前提は大きい。一般的な Unix shell に agent をあと付けするより、Sodex の方が terminal/agent/editor の境界を設計し直しやすい。

---

## 3. Web 調査で見えた現在地

### 3.1 主要プロダクトの比較

| 系統 | 確認できたこと | 今回の構想への示唆 |
|---|---|---|
| Warp | Universal Input で shell command と agent prompt を同一入力面に載せつつ、Agent Modality で terminal view と conversation view を明確に分けている。`!` prefix で shell 強制、自然言語 auto-detection、Command Corrections、Full Terminal Use による `vim`/`psql`/`gdb` attach まで実装している | 最も近い先行例。重要なのは「融合」しつつも、mode と permission は分けている点 |
| Claude Code | terminal 常駐の agent として成立しており、allow/ask/deny の permission rule、`/permissions`、`/compact`、`/memory`、`/agents`、`/vim` を持つ | agent-first でも権限・履歴圧縮・作業文脈の明示が必要。単なる chat では足りない |
| GitHub Copilot CLI | trusted directory、tool/path/URL permission、plan mode、autopilot mode、custom agents を持つ。path 判定には heuristic の限界があることも docs で明記している | 自律実行を増やすほど、working directory と path 権限と approval の粒度が重要になる |
| Cursor | sidepane の Agent が terminal 実行と code edit を持ち、Ask/Agent/Manual の mode 分離、diff review、checkpoints、terminal history 維持を持つ。terminal 実行時は重い prompt を避けるため `CURSOR_AGENT` 環境変数まで案内している | shell prompt や terminal 装飾は agent 実行を壊す。agent session 用の簡易 prompt 切替は実戦上かなり重要 |
| fish / 従来 shell UX | autosuggestion は history/path/completion から出すが、受理されるまで実行されない | typo 回復は「補助」であって「勝手に実行」ではない方が安全で理解しやすい |
| Neovim 系 (`copilot.vim`, `avante.nvim`, CodeCompanion.nvim) | ghost text、inline transform、chat buffer、visual selection、diff preview、accept/reject、ACP 連携が成熟してきている | `vi` 統合は「editor 内で自然言語を受ける」より、「選択範囲を文脈化して差分を適用する」方が現実的 |
| ACP | editor/client と coding agent の通信を JSON-RPC で標準化し、session、permission、fs、terminal を分けて扱う | Sodex でも terminal frontend と agent runtime の境界を内部 RPC として整理する価値がある |
| OpenAI shell tool | model は command を提案し、実行は integration 側が担う。sandbox、allow/deny、audit log を強く推奨している | 「agent が shell を使う」のではなく「shell 実行権を誰が持つか」を分離する設計が基本になる |

### 3.2 ここから読める市場の傾向

共通しているのは次の 4 点である。

1. shell と agent は同じ場所から起動できる方が便利
2. ただし mode は見た目と操作系で分ける
3. permission と audit は会話 UX より先に必要
4. editor 統合は freeform より diff/apply に寄せた方が安定する

言い換えると、「全部 agent に吸わせる」は発想として魅力的だが、実際に使われている設計は **agent を shell の上位互換にはせず、shell の上に agent control plane を載せる** 方向である。

---

## 4. 構想を分解すると何が必要か

### 4.1 入力面の統合

ユーザーのイメージは「ログインした瞬間から agent がいる terminal」だが、実装上は次のルータが本体になる。

```text
ユーザー入力
  ↓
1. 明示 override 判定
   - shell 強制
   - agent 強制
  ↓
2. shell fast path
   - parser 成功
   - 実在コマンド / builtin / alias / path 解決成功
  ↓
3. command-not-found / typo 回復
   - history
   - PATH 上の executable
   - cwd path
   - known workflow
  ↓
4. 自然言語 / 長手数タスク判定
   - summarize / explain / fix / investigate
  ↓
5. 曖昧時は確認
```

重要なのは、**shell fast path を一切 LLM に通さない** ことだと思われる。  
理由:

- quoting、pipe、redirect、subshell、env 展開は deterministic に shell が一番正しい
- 単純な `ls`, `cat`, `grep`, `find` まで agent 経由にすると遅い
- コストと監査面でも、通常 command を毎回 LLM に渡すのは不利

したがって「agent が login shell を置き換える」と言っても、実体は **agent-aware terminal input** であって、command execution の正系は依然 shell であるべきだと考える。

### 4.2 typo 補正

typo 補正には 3 段階ある。

1. **非 LLM の高信頼補正**
   - executable 名の 1-edit 距離
   - `cd` の path 補完
   - history 近傍
   - known command rule
2. **console error ベース補正**
   - missing flag
   - permission 不足
   - upstream branch なし
3. **LLM fallback**
   - command の意味理解が必要な場合だけ使う

既存例を見る限り、ここは suggestion 型が妥当である。  
特に destructive risk のある command では、「推測して実行」は UX として気持ちよく見えても、誤作動時の損失が大きい。

推奨は次の通り。

- 既定: inline suggestion + 右矢印/Tab/Enter で採用
- 高信頼かつ非破壊の範囲だけ auto-apply を許可
- `rm`、`mv`、`dd`、`git push`、network 書き込み系は常に確認

### 4.3 agent 実行面

agent を shell と並列に置くなら、session ごとに最低限以下が要る。

- working directory
- attached files / recent command blocks
- 権限 mode
- 実行履歴と audit
- compact / summarize 済みの長文脈
- tool 実行結果の再利用

ここは Claude Code、Copilot CLI、ACP 系がかなり収束している。  
Sodex にも既に `agent` CLI と session 機構があるので、既存 `agent` を backend とし、`term` 側から conversation を前面に出す形が自然である。

### 4.4 interactive CLI / full-screen app 介入

Warp の Full Terminal Use は、running PTY に agent を attach し、live buffer を読み、必要なら PTY に書き込む。これは強い。

ただし、実用上は次の注意が要る。

- shell prompt や fancy theme が出力解析を壊す
- full-screen app は terminal state の追跡が難しい
- `vim` では「どの keystroke が editor state をどう変えるか」が文脈依存で fragile

したがって、interactive app 対応は二層に分けた方がよい。

1. **observe / suggest 層**
   - PTY buffer を読む
   - 次の command を提案する
   - user が採用する
2. **act 層**
   - PTY に入力する
   - 基本は approval 付き
   - 長い自律操作は session 限定

---

## 5. `vi` にはどう入れるべきか

### 5.1 いきなり PTY 自動操作に寄せない方がよい

`vi`/`vim` 内 agent の夢は理解できるが、現時点の実装例を見ると安定しているのは次の三類型である。

1. **ghost text / inline suggestion**
   - `copilot.vim`
2. **selection-aware edit + diff preview**
   - CodeCompanion.nvim
   - Avante.nvim
3. **外側の terminal agent が PTY に attach**
   - Warp Full Terminal Use

この中で Sodex `vi` と最も相性が良いのは 2 だと思われる。  
理由:

- `vi` は既に buffer / selection / command mode を持っている
- 差分適用なら undo/redo と整合しやすい
- PTY 文字注入より editor 内部構造に沿った操作になる

### 5.2 Sodex `vi` への最初の統合案

最初の実装候補は次の通り。

- `:AgentAsk <prompt>`
  - 現在 buffer か visual selection を文脈に質問する
- `:AgentEdit <prompt>`
  - visual selection を対象に修正案を生成し、diff preview を出す
- `:AgentFix`
  - 現在行/選択範囲の diagnostics や obvious issue を修正候補化する
- `:AgentReview`
  - file 全体を review して finding だけ返す

必要な UX は次の 4 つで足りる。

1. 選択範囲の取り込み
2. 差分 preview
3. accept / reject / partial accept
4. session context の保持

### 5.3 `vi` にも agent を「自然に常駐」させるなら

常駐感を出すなら、次の設計が良い。

- insert mode 中は何もしない
- normal mode / command mode でだけ agent command を受ける
- visual selection があるときは、それを最優先コンテキストにする
- agent が返すのは raw text ではなく
  - replace selection
  - insert below
  - open scratch buffer
  - show diff
  のどれかに分類する

これは CodeCompanion の inline assistant が `replace/add/before/new/chat` を分類する考え方に近い。

---

## 6. Sodex に落とすならこの段階導入が妥当

### Phase 1: shell を壊さない融合

- `term` 入力面に shell/agent ルータを入れる
- 明示 prefix を用意する
- command-not-found 時だけ typo 補正候補を出す
- 自然言語は `agent` へ流す
- mode 表示を UI に出す

この phase の goal は「login 後に同じ入力欄から shell と agent が両方使える」ことだけで良い。

### Phase 2: permission / audit / memory

- agent 実行の allow/ask/deny
- path / command / network の許可範囲
- session compact
- tool 実行ログ
- recent command block の自動文脈化

ここを飛ばして autonomy を上げると危ない。

### Phase 3: agent-driven shell actions

- agent が shell command を提案
- 1 回許可 / session 許可 / deny を選べる
- 非 interactive command は自律実行可
- long-running command の attach/detach を追加

ここで初めて「agent が普通に terminal で作業する」体験に近づく。

### Phase 4: interactive PTY attach

- `less`, `top`, `gdb`, `psql`, `python`, `vi` などへ attach
- observe / suggest を先に実装
- PTY write は approval 付きから始める
- agent session 用の簡易 prompt / 環境変数も検討する

### Phase 5: `vi` の editor-native 統合

- `:AgentAsk`, `:AgentEdit`, `:AgentFix`
- visual selection ベース編集
- diff preview
- undo/redo と整合する apply path

---

## 7. 具体アーキテクチャ案

```text
term input
  ├─ shell 強制 prefix
  ├─ agent 強制 prefix
  └─ auto route
       ├─ eshell parser/executor
       ├─ command-not-found / typo engine
       └─ agent conversation launcher

agent conversation
  ├─ session memory
  ├─ permission profile
  ├─ tool dispatcher
  ├─ shell command proposal/execution
  └─ PTY attach controller

vi integration
  ├─ selection exporter
  ├─ prompt command
  ├─ diff preview buffer
  └─ apply/reject
```

ここで重要なのは、agent が直接 shell を置き換えるのではなく、

- **実行** は shell
- **判断** は router + agent
- **権限** は permission layer
- **編集** は editor-native diff apply

に分けることである。

---

## 8. 主要なリスク

### 8.1 誤ルーティング

自然言語に見える command や、command に見える自然言語がある。  
したがって auto-detection だけに依存しない設計が必要。

### 8.2 typo 補正の暴走

補正そのものより「補正後にそのまま実行」が危ない。  
特に filesystem 書き込み・network・process 操作で誤爆すると致命傷になりやすい。

### 8.3 shell 構文の難しさ

path permission や command inspection は heuristic に限界がある。  
GitHub Copilot CLI が docs で明示している通り、複雑な shell 構文から path を正確に抜くのは難しい。

### 8.4 prompt / terminal 装飾

agent が terminal 出力を読む場合、重い prompt、色、非標準 escape、右 prompt などが邪魔になる。  
agent session では prompt を簡素化する導線が必要になる可能性が高い。

### 8.5 `vi` の fragile さ

PTY への keystroke 注入は、screen state、mode state、IME state に依存しやすい。  
`vi` だけは shell 以上に editor-native API が欲しい。

---

## 9. 推奨方針

この構想は十分ありで、Sodex でもかなり相性が良い。  
ただし「shell と agent の融合」は、**agent に全部飲ませること** ではなく、**shell の決定性を残したまま agent を前面化すること** と捉えた方が成功確率が高い。

特に推奨するのは次の順番である。

1. login 後 terminal の入力面を unified にする
2. shell fast path は完全に保持する
3. command-not-found と自然言語だけ agent へ流す
4. permission / audit を入れる
5. その後で PTY attach
6. `vi` は selection + diff apply から始める

一言で言うと、目指すべきは **agent shell** より **agent-aware terminal OS** である。

---

## 参考 URL

- Sodex README: `README.md`
- Sodex rich terminal spec: `specs/rich-terminal/README.md`
- Warp Universal Input: https://docs.warp.dev/terminal
- Warp Agent Modality: https://docs.warp.dev/agent-platform/local-agents/interacting-with-agents/agent-modality
- Warp Command Corrections: https://docs.warp.dev/terminal/entry/command-corrections
- Warp Full Terminal Use: https://docs.warp.dev/agents/full-terminal-use
- Claude Code overview: https://docs.anthropic.com/en/docs/claude-code/overview
- Claude Code settings: https://docs.anthropic.com/en/docs/claude-code/settings
- Claude Code slash commands: https://docs.anthropic.com/en/docs/claude-code/slash-commands
- GitHub Copilot CLI overview: https://docs.github.com/en/copilot/how-tos/copilot-cli/use-copilot-cli-agents/overview
- GitHub Copilot CLI permissions: https://docs.github.com/en/copilot/how-tos/copilot-cli/set-up-copilot-cli/configure-copilot-cli
- GitHub Copilot CLI autopilot: https://docs.github.com/en/copilot/concepts/agents/copilot-cli/autopilot
- Cursor agent overview: https://docs.cursor.com/chat/overview
- Cursor terminal integration: https://docs.cursor.com/en/agent/terminal
- fish interactive use / autosuggestions: https://fishshell.com/docs/current/
- `avante.nvim`: https://github.com/yetone/avante.nvim
- `copilot.vim`: https://github.com/github/copilot.vim
- CodeCompanion.nvim top: https://codecompanion.olimorris.dev/
- CodeCompanion inline assistant: https://codecompanion.olimorris.dev/usage/inline-assistant
- CodeCompanion ACP reference: https://codecompanion.olimorris.dev/usage/acp-protocol
- Agent Client Protocol overview: https://agentclientprotocol.com/protocol/overview
- OpenAI shell tool: https://platform.openai.com/docs/guides/tools-shell
