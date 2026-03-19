# LLM Agent OS 調査レポート — Sodexへの統合検討

**調査日**: 2026-03-19
**対象**: AIOS, MemGPT/Letta, Composable OS Kernels, OS-R1, AgentOS, OS for Agents, AutoGen
**目的**: LLMエージェントをOS内部に組み込む実験的OSを調査し、Sodexへの取り込み可能性を検討する

---

## 1. エグゼクティブサマリー

LLMエージェントをOS内部に配置する試みは、大きく3つのアプローチに分類される:

| アプローチ | 代表プロジェクト | 概要 |
|-----------|----------------|------|
| **A. OSメタファーの借用** | AIOS, MemGPT | OSの概念（スケジューリング、仮想メモリ）をPythonでシミュレーション |
| **B. 実カーネル拡張** | Composable OS Kernels | Linux LKMにテンソル処理・ML推論を埋め込む |
| **C. エージェントランタイム** | AgentOS, OS for Agents | WASM等で隔離されたエージェント実行環境を提供 |

**Sodexの独自性**: 上記いずれのプロジェクトも「実カーネル上でLLMエージェントを動作させる完全な独自OS」を実現していない。Sodexは既にユーザランドでClaude APIクライアント・ツール実行・セッション管理を独自実装しており、**真のOS統合として最も進んだ位置にある**。

---

## 2. 主要プロジェクト詳細分析

### 2.1 AIOS (agiresearch/AIOS)

**論文**: "AIOS: LLM Agent Operating System" (COLM 2025)
**GitHub**: https://github.com/agiresearch/AIOS

#### アーキテクチャ

3層構造: Application Layer → AIOS Kernel → Hardware Layer

```
┌─────────────────────────────────┐
│  Application Layer              │
│  ├── Agent SDK (Python)         │
│  └── External Agents            │
├─────────────────────────────────┤
│  AIOS Kernel (FastAPI:8000)     │
│  ├── LLM Core(s)               │  ← LLMをCPUコアに見立てる
│  ├── Context Manager            │  ← KVキャッシュの保存/復元
│  ├── Memory Manager             │  ← ChromaDB/Qdrantベクトル検索
│  ├── Storage Manager (LSFS)     │  ← Redisバージョニング(最大20版)
│  ├── Tool Manager (MCP Server)  │  ← 外部ツール実行
│  └── Access Manager             │  ← 権限グループ制御
├─────────────────────────────────┤
│  OS Kernel (Linux/macOS/Win)    │
│  Hardware (CPU/GPU/Storage)     │
└─────────────────────────────────┘
```

**実態**: 「AIOS Kernel」はPythonプロセス（uvicorn）。syscallは型付きPythonオブジェクト（`LLMSyscall`, `MemorySyscall`, `StorageSyscall`, `ToolSyscall`）で、HTTP POSTで受け付ける。実際のカーネルコードは一切なし。

#### スケジューリング

| スケジューラ | 方式 | 詳細 |
|-------------|------|------|
| FIFOScheduler | バッチ処理 | 1.0秒間隔でリクエスト収集、ThreadPoolExecutorで並列実行 |
| RRScheduler | タイムスライス | 1.0秒/リクエスト、SimpleContextManagerでKVキャッシュ保存/復元 |

4つのリソースキュー（LLM, Memory, Storage, Tool）ごとに永続スレッドが走る。

#### コンテキストスイッチ

LLMの「コンテキストスイッチ」は2種類:
- **テキストベース**: クラウドAPI向け。生成済みテキストを保存
- **ロジットベース**: ローカルモデル向け。`past_key_values`（KVキャッシュ）と`generated_tokens`を保存し、ビームサーチ木を維持

これはCPUレジスタ保存と直接的にアナロジーが成立する。

#### syscallインターフェース

```
Agent → HTTP POST /query → SyscallExecutor.create_syscall()
  → Queueに追加 → 要求スレッドはjoin()でブロック
  → Scheduler dequeue → Manager.address_request()
  → syscall.set_response() + event.set() → ブロック解除
```

| モジュール | syscall例 |
|-----------|----------|
| LLM Core | execute_llm_syscall, get_model_response |
| Memory | add_memory, retrieve_memory, update_memory |
| Storage | sto_create_file, sto_write, sto_retrieve, sto_rollback |
| Tool | load_tool_instance, execute tool calls |
| Access | add_privilege, check_access, ask_permission |

#### 性能

- ベースラインフレームワーク比で最大**2.1x**スループット向上
- 250〜2000エージェントまでリニアスケーリング

#### Sodexへの示唆

- **syscall分類体系（LLM/Memory/Storage/Tool/Access）は優秀な設計パターン**。Sodexのtool_dispatchに直接応用可能
- ただしAIOSは完全にPythonシミュレーション。OS統合の深さではSodexが既に上回っている
- コンテキストスイッチの概念はプロセスレベルで応用可能（後述）

---

### 2.2 MemGPT / Letta

**論文**: "MemGPT: Towards LLMs as Operating Systems" (NeurIPS 2023 Workshop)
**プロジェクト**: https://www.letta.com/ (MemGPTをLettaにリブランド)

#### コアアイデア: 仮想メモリのアナロジー

| OS概念 | MemGPT対応物 |
|--------|-------------|
| RAM（物理メモリ） | **メインコンテキスト**（コンテキストウィンドウ内） |
| ディスク | **外部コンテキスト**（データベース） |
| 仮想メモリ | **仮想コンテキスト**（無限コンテキストの幻想） |
| ページフォルト/エビクション | コンテキスト圧縮 + function call検索 |

#### メモリ階層

**Tier 1 — メインコンテキスト（コンテキストウィンドウ内）:**
1. システム命令（読み取り専用 — OSファームウェアに相当）
2. コアメモリ（固定サイズ、function callで書き換え可能 — 各ブロック最大5000文字）
3. 会話履歴（FIFOキュー、最古メッセージを自動エビクト）

**Tier 2 — 外部コンテキスト（コンテキストウィンドウ外）:**
1. リコールストレージ: 全イベント履歴（テキスト検索）
2. アーカイバルストレージ: 構造化知識ベース（セマンティック検索）

#### 自己管理メモリ

LLM自身がメモリ管理を行う（ツール呼び出しとして）:
- `core_memory_append` / `core_memory_replace`: コアメモリ操作
- `archival_memory_insert` / `archival_memory_search`: アーカイバル操作
- `conversation_search`: 会話履歴検索
- `send_message`: ユーザーに見えるメッセージ送信（**唯一の出力手段**）

#### Inner/Outer Loop制御フロー

- **Outer loop**: ユーザーメッセージ → LLM推論
- **Inner loop**: `request_heartbeat=true`でfunction callを連鎖。ユーザー入力なしで多段検索・メモリ操作を実行

#### Sodexへの示唆（最重要）

**MemGPTの洞察はSodexにとって最もアーキテクチャ的に価値が高い:**

- 「LLMが自らのメモリを管理する」パラダイムは、Sodexの既存session.cの圧縮（compaction）機構と直接的に統合できる
- コンテキストウィンドウ = 物理メモリ、セッションファイル = ディスクという対応が自然
- Sodexは既にCONV_TOKEN_LIMIT（190K）でのコンパクション機構を持つ — これはまさに「ページアウト」
- **提案**: session.cにセマンティック検索（ベクトルDB不要、TF-IDF的な軽量実装）を追加し、「ページイン」機能を実現

---

### 2.3 Composable OS Kernel Architectures for Autonomous Intelligence

**論文**: arxiv.org/abs/2508.00604 (2025)

#### アーキテクチャ（3つの貢献）

**1. AI対応LKM（Loadable Kernel Modules）:**
- カーネル空間でテンソル処理: `kmalloc()`, `get_user_pages()`, `kmap()`
- ゼロコピーDMAバッファ: `dma_alloc_coherent`
- AVX-512 SIMDベクトル化
- `kthread_run`でマルチスレッド（最大8スレッド）

**2. KernelAGIサブシステム:**
- 浮動小数点エンジン: `_schedule()`にフックしたFPUコンテキスト隔離
- GPUドライバフレームワーク: 1MB共有バッファ、CUDA/OpenCL統合
- MLメモリプール: **512MB事前確保プール**、4KBブロック、ビットマップ追跡
- ML対応スケジューラ: 専用スケジューリングクラス（`ML_SCHED_PRIORITY=10`）
- セキュリティ: ハードウェア支援仮想化、メモリ保護キー

**3. RaBAB-NeuSymカーネル:** カテゴリ理論・ホモトピー型理論による神経シンボリック推論

#### カーネル vs ユーザランド分割

- **カーネル空間**: レイテンシクリティカルな推論、テンソル演算、スケジューリング判断
- **ユーザ空間**: 高レベルオーケストレーション、PyTorch/TFなどのMLフレームワーク

#### 報告された課題

| 課題 | 詳細 | Sodex関連性 |
|------|------|------------|
| kmallocの制限 | 大モデル向けの仮想メモリ柔軟性不足 | Sodexのmemory.cのmem_holeにも同様の制約 |
| FPUコンテキスト管理 | システム全体への副作用リスク | i486にはFPU/SSE問題はない（浮動小数点は別途） |
| デバッグ困難 | `printk`と`debugfs`のみ | Sodexもシリアルログに限定 |
| セキュリティ | 特権実行によるバッファオーバーフローリスク | permission.confによる緩和策あり |
| 非決定的レイテンシ | ML推論がカーネルに非決定性を持ち込む | ネットワーク越しのAPI呼び出しは元々非決定的 |

#### Sodexへの示唆

- i486ではローカル推論は非現実的（メモリ・CPU制約）。API呼び出し前提で正しい
- MLメモリプールの概念は、エージェント用メモリリージョンの事前確保として応用可能
- ML対応スケジューラの優先度概念は、エージェントプロセスの優先度管理に使える

---

### 2.4 OS-R1: LLMによるカーネルチューニング

**論文**: arxiv.org/abs/2508.12551 (2025)

逆方向のアプローチ: LLMエージェントで**Linux カーネル自体をチューニング**する。

- Linux設定空間（18,000+項目）をRL環境として抽象化
- ルールベースRLで構成を探索
- ヒューリスティック比で最大**5.6%改善**、**13xコスト削減**

#### Sodexへの示唆

- Sodexのエージェントが「自分が動作するOSのカーネルパラメータを調整する」自己最適化は面白い将来構想
- 現時点では直接的な取り込み対象ではないが、長期ビジョンとして記録

---

### 2.5 AgentOS (iii-hq)

**GitHub**: https://github.com/iii-hq/agentos

#### アーキテクチャ

3つのプリミティブを`iii-engine`バスで実行:
- **Worker**: 言語非依存の計算ユニット（Rust, TypeScript, Python）
- **Function**: trigger()で呼び出し可能な関数
- **Trigger**: 呼び出し機構

18 Rustクレート（~5,500 LOC）+ 46 TypeScriptワーカー + 1 Pythonワーカー

#### OS的機能

| 機能 | 詳細 |
|------|------|
| Realm | マルチテナント隔離ドメイン |
| Hierarchy | サイクル安全DFSツリーによる組織構造 |
| Ledger | バジェット強制（ソフト/ハードリミット）、バージョン化CAS |
| Council | SHA-256マークルチェーン監査証跡によるガバナンス |
| Pulse | コンテキスト対応スケジュール実行（バジェットゲーティング） |
| Bridge | 6つのランタイムアダプタ（Process, HTTP, ClaudeCode, Codex, Cursor, OpenCode） |
| WASM sandboxing | 非信頼コード実行 |
| Artifact DAG | Git式コンテンツアドレッシング（SHA-256） |

#### Sodexへの示唆

- Ledger（バジェット管理）の概念は、トークン消費の上限管理に直接応用可能
- Artifact DAGはセッション履歴のバージョン管理に応用可能
- WASMサンドボックスの代わりに、Sodexではプロセス隔離（ユーザ空間セグメント）で同等の効果

---

### 2.6 OS for Agents (AControlLayer)

**サイト**: https://www.osforagents.com/

#### 3つのコアプリミティブ

1. **Identity**: エージェントごとのX.509証明書（テナント、ロール、ケーパビリティアサーション）
2. **Isolation**: WASMスタイルのサンドボックス（メモリ・ツールアクセス隔離）
3. **Governance**: "Gavel"カーネルモジュールがエージェント出力をPVS-1標準でインターセプト

#### 注目機能

- **タイムトラベルデバッグ**: 状態バージョニングで任意時点にロールバック（ハルシネーション調査用）
- **リソースクォータ**: エージェントごとのトークン使用量・APIコスト管理
- **Human-in-the-loop sudo**: 実行一時停止 → 人間の承認 → コンテキスト注入（再起動不要）
- **モデル非依存**: OpenAI/Anthropic/ローカルLlamaをコード変更なしで切り替え

#### Sodexへの示唆

- **最も参考になるのはGavernanceモデル**: permissions.confの発展形として、出力検証（ハルシネーション検出）を追加可能
- Human-in-the-loop sudoはSodexの既存STANDARD/STRICT権限モードと自然に統合
- タイムトラベルデバッグはsession.cのJSONL形式で既に基盤がある

---

### 2.7 Microsoft AutoGen

**GitHub**: https://github.com/microsoft/autogen

マルチエージェントフレームワーク（OSではない）。

- **コア**: 非同期イベント駆動メッセージパッシング（ローカル/分散ランタイム）
- **AgentChat API**: ラピッドプロトタイピング向け
- **エージェント間通信**: 構造化チャットメッセージ
- **スケジューラなし**: エージェントはイベント駆動

#### Sodexへの示唆

- メッセージパッシングモデルはマイクロカーネルIPCパターンに近い
- AIOSのsyscallベースよりもSodexの設計思想に合う可能性
- 複数エージェントの協調実行時の参考になる

---

## 3. 横断比較

### 3.1 カーネル空間 vs ユーザランド

| プロジェクト | 実カーネルコード | ユーザランドOS抽象化 | Sodex現状 |
|-------------|----------------|---------------------|-----------|
| AIOS | なし — 100% Python | syscall, scheduler, context switch | — |
| MemGPT | なし | メモリ管理のみ | — |
| Composable OS Kernels | あり — Linux LKM | オーケストレーション | — |
| AgentOS | なし | Functionバス + WASM | — |
| OS for Agents | なし | WASM + Governance | — |
| **Sodex** | **最小限（socket拡張等）** | **完全なエージェントランタイム** | **唯一の独自OS実装** |

### 3.2 LLM API呼び出し方式

| プロジェクト | 方式 | Sodex |
|-------------|------|-------|
| AIOS | ThreadPoolExecutor経由Python HTTP | — |
| MemGPT | 標準API呼び出し | — |
| Composable OS Kernels | カーネルLKMでローカル推論のみ | — |
| **Sodex** | **独自HTTP/TLS/SSEスタック（BearSSL、NE2000ドライバ経由）** | **唯一のネイティブ実装** |

### 3.3 コンテキスト管理

| プロジェクト | 戦略 | Sodex応用可能性 |
|-------------|------|----------------|
| AIOS | KVキャッシュスナップショット | プロセスごとのコンテキスト保存 |
| MemGPT | 仮想コンテキスト（ページング） | **session.cのcompactionを拡張** |
| AgentOS | トークンバジェット | トークン上限管理 |

### 3.4 エージェント間通信

| プロジェクト | 方式 | 成熟度 |
|-------------|------|--------|
| AIOS | 間接的（共有ストレージ経由） | 低 |
| AutoGen | 構造化メッセージパッシング | 中 |
| AgentOS | Coordination Board | 中 |
| AIOS Server | DHT (Kademlia) + Gossipプロトコル | 実験的 |

**全プロジェクトでエージェント間通信は未成熟** — オープンな設計空間。

---

## 4. Sodexへの統合提案

### 4.1 アーキテクチャ方針: 「OSネイティブ・エージェントランタイム」

既存のユーザランドファースト設計を維持しつつ、AIOS/MemGPTの概念を**実OS上で初めて具現化**する。

```
┌────────────────────────────────────────────────┐
│  エージェントアプリケーション層                      │
│  ├── agent（CLIコマンド）                         │
│  ├── 外部エージェント（将来: マルチエージェント）      │
│  └── エージェント間メッセージバス（将来）             │
├────────────────────────────────────────────────┤
│  エージェントランタイム（libagent — ユーザランド）     │
│  ├── agent_loop       ← エージェント実行ループ       │
│  ├── claude_client    ← LLMコア（API呼び出し）      │
│  ├── context_manager  ← [新] コンテキスト管理        │
│  ├── memory_manager   ← [新] 仮想コンテキスト        │
│  ├── tool_dispatch    ← ツール実行                  │
│  ├── permissions      ← アクセス制御                │
│  └── session          ← セッション永続化            │
├────────────────────────────────────────────────┤
│  カーネル                                         │
│  ├── プロセス管理     ← エージェントプロセス管理      │
│  ├── メモリ管理      ← エージェント用メモリリージョン  │
│  ├── ファイルシステム  ← セッション/メモリストア       │
│  ├── ネットワーク     ← NE2000 + uIP + socket       │
│  └── [新] agent_sched ← エージェント優先度管理       │
└────────────────────────────────────────────────┘
```

### 4.2 具体的な取り込み項目

#### Phase 1: MemGPT式仮想コンテキスト（優先度: 高）

**目的**: コンテキストウィンドウをOSの仮想メモリとしてモデル化

**現状のsession.c compaction機構を拡張:**

```
メインコンテキスト（190Kトークン上限）
├── システムプロンプト（固定、読み取り専用）
├── コアメモリ（ツールで書き換え可能）  ← [新規]
│   ├── user_profile: ユーザー情報
│   ├── workspace_state: 作業状態
│   └── learned_facts: 学習した事実
├── 会話履歴（FIFOエビクション）       ← 既存のcompactionを拡張
└── 直近ツール結果

外部コンテキスト（ディスク）
├── セッションJSONL                    ← 既存
├── アーカイバルストア                  ← [新規] /var/agent/archival/
└── ワークスペースメモリ                ← 既存の memory/<hash>.md を拡張
```

**新規ツール:**
- `core_memory_update`: コアメモリブロックの更新
- `archival_search`: アーカイバルストアのキーワード検索
- `archival_insert`: アーカイバルストアへの知識保存
- `conversation_recall`: 過去の会話から関連ターンを検索

**実装指針:**
- ベクトルDBは使わない（i486制約）。代わりにTF-IDF的なキーワードマッチング
- アーカイバルストアはJSONLファイル（既存パターンに従う）
- コアメモリは固定サイズバッファ（例: 各ブロック2KB、合計8KB）

#### Phase 2: AIOS式syscall分類（優先度: 中）

**目的**: ツール呼び出しをOS syscall体系に整理

現在のtool_dispatchは平坦な関数テーブル。これをAIOSに倣い分類する:

```c
enum agent_syscall_class {
    AGENT_SYS_LLM,      // LLM呼び出し（claude_client）
    AGENT_SYS_MEMORY,   // メモリ操作（コアメモリ、アーカイバル）
    AGENT_SYS_STORAGE,  // ファイルI/O（read_file, write_file, list_dir）
    AGENT_SYS_TOOL,     // 外部コマンド実行（run_command）
    AGENT_SYS_ACCESS,   // 権限確認（permission_check）
    AGENT_SYS_SYSTEM,   // システム情報（get_system_info, manage_process）
};
```

**利点:**
- リソースごとのキューイング/優先度制御が可能になる
- 統計・監査がsyscallクラス別に集計可能
- 将来のマルチエージェント化でリソース競合管理の基盤

#### Phase 3: エージェント対応スケジューラ（優先度: 中）

**目的**: カーネルのプロセススケジューラにエージェント優先度を導入

```
通常プロセス: 標準優先度
エージェントプロセス: 動的優先度
  ├── LLM API応答待ち: 低優先度（CPUを他に譲る）
  ├── ツール実行中: 中優先度
  └── ユーザー対話中: 高優先度
```

**カーネル変更（最小限）:**
- `process.c`にエージェントフラグを追加
- スケジューラがフラグを見て優先度を動的調整
- `setsockopt`のタイムアウトと連携（API待ちでブロック中は他プロセスにCPU時間を回す）

#### Phase 4: マルチエージェント基盤（優先度: 低・将来）

**目的**: 複数エージェントの協調実行

```
Agent A (コード分析)  ←→  Agent B (テスト実行)
      ↕                         ↕
   共有メモリ領域（/var/agent/shared/）
      ↕
   メッセージキュー（パイプまたは名前付きFIFO）
```

**設計方針:**
- AutoGenのメッセージパッシングモデルを参考
- カーネルのパイプ/FIFO機構をそのまま活用
- 共有ストレージ経由の間接通信から始める（AIOS方式）
- 将来的にIPC syscallを追加

#### Phase 5: ガバナンス/監査（優先度: 低・将来）

**OS for Agentsを参考に:**
- エージェント出力の検証層（ハルシネーション検出の軽量版）
- タイムトラベルデバッグ（セッションJSONLの任意時点への巻き戻し）
- トークンバジェット管理（AgentOSのLedger概念）

---

## 5. Sodexの差別化ポイント

既存プロジェクトと比較した**Sodexだけの強み**:

### 5.1 真のOSネイティブ統合

| 他プロジェクト | Sodex |
|--------------|-------|
| PythonでOSをシミュレーション | 実カーネル上で動作 |
| 標準ライブラリに依存 | すべて自前実装（-nostdlib） |
| Linux/macOS上のプロセス | 独自スケジューラ、独自FS、独自NIC |

### 5.2 ハードウェアからLLMまでの完全な垂直統合

```
NE2000 NICドライバ → uIP TCP/IP → socket → DNS → HTTP/1.1 → TLS(BearSSL) → SSE → Claude API
  ↑ すべてSodex独自実装 — 他に例がない
```

### 5.3 真のリソース制約下での設計

- i486（32ビット、数十MBメモリ）でのエージェント動作
- ベクトルDBもGPUも使えない環境での創意工夫
- 「AIOSがPythonでシミュレートしていることを、ベアメタルで実現する」

### 5.4 カーネルレベルのエージェント認知

- プロセススケジューラがエージェントプロセスを認識（Phase 3）
- メモリ管理がエージェント用リージョンを確保（Phase 3）
- これは**世界初**の試み

---

## 6. リスクと課題

| リスク | 対策 |
|--------|------|
| i486でのメモリ不足（セッション肥大化） | MemGPT式エビクション + compaction強化 |
| ネットワークレイテンシ（API呼び出し） | 非同期I/O + バックグラウンドプリフェッチ |
| 複雑性の増加 | Phase分割で段階的に実装、各Phase独立テスト可能 |
| ツール実行のセキュリティ | 既存permission.confを拡張、OS for Agentsのガバナンス参考 |
| マルチエージェントのデッドロック | メッセージパッシング + タイムアウトで回避 |

---

## 7. 実装ロードマップ

```
Phase 1: 仮想コンテキスト       ← MemGPT概念の実OS実装（最優先）
  └── core_memory, archival_store, conversation_recall
  └── session.cのcompaction拡張

Phase 2: syscall分類             ← AIOS概念の整理
  └── tool_dispatchのリファクタ
  └── リソースクラス別統計

Phase 3: エージェント対応スケジューラ  ← カーネル拡張（最小限）
  └── process.cにagentフラグ
  └── 動的優先度調整

Phase 4: マルチエージェント      ← AutoGen/AIOS Server参考
  └── エージェント間メッセージング
  └── 共有コンテキスト

Phase 5: ガバナンス              ← OS for Agents参考
  └── 出力検証、タイムトラベル、バジェット管理
```

---

## 8. 参考文献

1. Ge, Y., et al. "AIOS: LLM Agent Operating System." COLM 2025. https://arxiv.org/abs/2403.16971
2. Packer, C., et al. "MemGPT: Towards LLMs as Operating Systems." NeurIPS 2023 Workshop. https://arxiv.org/abs/2310.08560
3. "Composable OS Kernel Architectures for Autonomous Intelligence." 2025. https://arxiv.org/abs/2508.00604
4. "OS-R1: Agentic OS Kernel Tuning with RL." 2025. https://arxiv.org/abs/2508.12551
5. AgentOS (iii-hq). https://github.com/iii-hq/agentos
6. OS for Agents (AControlLayer). https://www.osforagents.com/
7. Microsoft AutoGen. https://github.com/microsoft/autogen
8. Letta (formerly MemGPT). https://www.letta.com/

---

## 付録A: AIOSソースコード構造（参考）

```
agiresearch/AIOS/
├── aios/
│   ├── core/                   # カーネル本体
│   │   ├── syscall/            # syscall定義・ディスパッチ
│   │   │   ├── llm.py
│   │   │   ├── memory.py
│   │   │   ├── storage.py
│   │   │   └── tool.py
│   │   ├── scheduler/          # FIFO/RRスケジューラ
│   │   │   ├── fifo_scheduler.py
│   │   │   └── rr_scheduler.py
│   │   ├── context/            # KVキャッシュ管理
│   │   │   └── simple_context.py
│   │   └── memory/             # ベクトルメモリ
│   │       └── base_memory_manager.py
│   ├── runtime/                # FastAPIサーバ
│   │   ├── launch.py
│   │   └── kernel.py
│   └── storage/                # LSFS (Redis)
│       └── lsfs/
├── sdk/                        # エージェントSDK
└── example_agents/             # サンプルエージェント
```

## 付録B: Sodex現行エージェントアーキテクチャ

```
sodex/src/usr/
├── command/
│   ├── agent.c                 # CLIコマンド（REPL, one-shot, resume）
│   └── agent_integ.c           # 統合テスト
├── lib/libagent/
│   ├── agent_loop.c            # エージェント実行ループ
│   ├── claude_client.c         # HTTP+TLS Claude API呼び出し
│   ├── claude_adapter.c        # Messages API JSON処理
│   ├── conversation.c          # 会話状態管理
│   ├── session.c               # セッション永続化（JSONL）
│   ├── tool_dispatch.c         # ツールディスパッチ
│   ├── tool_init.c             # ツール登録
│   ├── tool_read_file.c        # ファイル読み取りツール
│   ├── tool_write_file.c       # ファイル書き込みツール
│   ├── tool_list_dir.c         # ディレクトリ一覧ツール
│   ├── tool_run_command.c      # コマンド実行ツール
│   ├── permissions.c           # 権限チェック
│   ├── http_client.c           # HTTP/1.1クライアント
│   ├── dns.c                   # DNS リゾルバ
│   ├── tls_client.c            # BearSSL TLS
│   ├── sse_parser.c            # SSEパーサ
│   └── json.c                  # JSONパーサ/ライタ
├── include/agent/
│   ├── agent.h, agent_loop.h
│   ├── claude_client.h, claude_adapter.h
│   ├── conversation.h, session.h
│   ├── tool_registry.h, tool_handlers.h, tool_dispatch.h
│   ├── permissions.h
│   ├── http_client.h, dns.h, tls_client.h
│   ├── sse_parser.h, json.h
│   └── path_utils.h
└── lib/libc/
    ├── shell_executor.c        # シェル実行
    └── shell_vars.c            # シェル変数展開
```
