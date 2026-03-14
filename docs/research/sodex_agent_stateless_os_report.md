# Sodex Agent: ステートレス自律OSの構想リサーチ

**調査日**: 2026-03-14
**目的**: 内部にデータを持たず、API/MCP/CLI経由のネットワーク操作のみで動作する自律Agent OSの設計可能性を調査する

---

## 1. エグゼクティブサマリー

### コンセプト: "Kernel as API Router"

Sodex Agentは**内部に永続データを一切持たない**。カーネルはLLM APIとの通信ループを回し、すべてのデータ操作はMCPサーバやREST APIを通じて外部サービスに委譲する。一時的な処理データのみメモリ上に保持し、タスク完了後に暗号的消去で即座にクリーンアップする。

```
従来のOS:  アプリ → syscall → カーネル → ローカルディスク
Sodex Agent: Agent Loop → MCP/HTTP → 外部サービス（S3, DB, Git等）
                              ↕
                         LLM API（思考エンジン）
```

### なぜこの設計が今可能か

1. **MCP (Model Context Protocol)** がOS機能を外部サービス化する標準プロトコルとして成熟（2024-2025年、13,000+サーバがGitHub上に公開）
2. **Streamable HTTP** トランスポート（MCP仕様2025-03-26）により、完全ステートレスなMCPサーバ運用が可能に
3. **BearSSL** のmalloc不要設計がベアメタルからのHTTPS通信を現実的に
4. **MicrosoftがWindows 11にMCPをネイティブ統合**（Build 2025発表）— MCPがOS機能として認知された

### 歴史的文脈

| 時代 | プロジェクト | 概念 | 結果 |
|------|------------|------|------|
| 1996 | Oracle Network Computer | ディスクレスPC | ネットワーク速度不足で失敗 |
| 1999 | Sun Ray | 完全ステートレスクライアント | ホットデスキング実現。ネット依存が課題 |
| 2011 | ChromeOS | クラウドファースト | 成功。ローカルキャッシュとの妥協 |
| 2020 | Talos Linux | API管理のみ（SSH/シェルなし） | Kubernetes用として成功 |
| 2025 | Windows 365 Link | クラウドPCシンクライアント | Dell/ASUS参入、業界トレンドに |
| **2026** | **Sodex Agent** | **LLM駆動ステートレスOS** | **ベアメタルでは世界初の試み** |

---

## 2. MCP — OSサービスの外部化基盤

### MCP技術仕様

**プロトコル**: JSON-RPC 2.0ベース
**3つのプリミティブ**:
- **Tools**: 副作用を持つ実行可能な関数（JSONスキーマで定義）
- **Resources**: 読み取り専用のデータソース
- **Prompts**: 再利用可能なプロンプトテンプレート

**トランスポート**:

| トランスポート | 状態 | 特徴 |
|-------------|------|------|
| stdio | 現行 | ローカルCLI向け。Sodex Agentでは不要 |
| SSE | **非推奨**（2025-03-26で廃止） | HTTP/2非互換、双方向通信に不向き |
| **Streamable HTTP** | **現行標準** | 単一エンドポイント（`POST /mcp`）。**完全ステートレス運用可能**。ロードバランサ背後に配置可 |

**ライフサイクル**:
1. クライアント→サーバ: `initialize` (capabilities negotiation)
2. サーバ→クライアント: ツール/リソース/プロンプトの一覧
3. クライアント→サーバ: `tools/call` (ツール実行要求)
4. サーバ→クライアント: 実行結果（JSONまたはSSEストリーム）

### MCPで置換可能な従来のOS機能

| 従来のOS機能 | MCPサーバによる代替 | 具体例 |
|------------|-------------------|--------|
| ファイルシステム (ext4, NTFS) | Filesystem MCP / S3 MCP / GCS MCP | `read_file`, `write_file`, `search_files` |
| プロセス実行 | Code Execution MCP | コード実行・結果返却 |
| ネットワークI/O | Fetch MCP / HTTP Client MCP | Webコンテンツ取得 |
| データベース | PostgreSQL MCP / Couchbase MCP | スキーマ検査、クエリ実行 |
| バージョン管理 | Git MCP / GitHub MCP | コミット、ブランチ、Issue操作 |
| シェル/ターミナル | Desktop Commander MCP | コマンド実行 |
| 記憶/状態管理 | Memory MCP (知識グラフ) | エージェントの長期記憶 |
| 検索 | Brave Search MCP / Web Search | プライバシー重視の検索 |

### MCPのセキュリティ課題

調査で判明した深刻な問題:
- **88%のMCPサーバが認証を要求するが、過半数が静的APIキーという脆弱な方式**（OAuth対応は8.5%のみ）
- **CVE-2025-6514** (CVSS 9.6): mcp-remote OAuthプロキシの脆弱性でRCE可能
- **Tool Poisoning攻撃**: ツール記述に悪意ある指示を埋め込み、AIモデルに不正操作させる
- **492個の公開MCPサーバ**が認証・暗号化なしで公開されている

**Sodex Agentへの対策**:
- 接続先MCPサーバをホワイトリスト制御（Capability-based）
- すべての通信をTLS必須化
- ツール記述の検証レイヤーを実装
- 認証にはOAuth 2.1を採用

---

## 3. ステートレスアーキテクチャの設計パターン

### 3.1 完全ステートレスの定義

```
ステートレスOS:
  - 永続ストレージへの書き込み = ゼロ
  - すべてのデータ = メモリ上の一時バッファ（暗号化済み）
  - 再起動 = ファクトリーリセット（常に同一イメージから起動）
  - 設定/認証情報 = 起動時に外部から注入 or ブートイメージに埋め込み
```

### 3.2 参考アーキテクチャ: Talos Linux

Sodex Agentに最も近い既存システム:
- **SSH、シェル、bash、sshdが一切存在しない**
- **gRPC APIのみ**で管理（`talosctl`コマンド）
- SquashFSルートファイルシステムは**RAMから実行**、ディスクに書き込まない
- カスタムプロセスマネージャ`machineD`（Go製）が従来のinitを置換
- プライマリディスクはすべてKubernetesに委譲

**Sodex Agentとの違い**: Talosの「クライアント」はKubernetesだが、Sodex Agentの「クライアント」はLLM。管理APIの先がコンテナオーケストレータではなくAIエージェントループになる。

### 3.3 外部状態管理パターン

マイクロサービスアーキテクチャから応用可能なパターン:

#### Event Sourcing
```
Agent → [Action Event] → 外部イベントストア
                              ↓
                         状態の再構築はイベントの再生で行う
                         OS内部に状態のスナップショットを持たない
```

#### Saga Pattern
```
複雑なマルチステップ操作を分解:
  Step 1: MCP-A (ファイル読み取り) → 成功
  Step 2: MCP-B (データ変換)      → 成功
  Step 3: MCP-C (結果書き込み)    → 失敗 → 補償トランザクション発行

各ステップは独立した外部サービス呼び出し
OS内部にトランザクション状態を持たない
```

#### CQRS (Command Query Responsibility Segregation)
```
書き込み: Agent → MCP tools/call → 外部ストレージ
読み取り: Agent → MCP resources/read → 最適化された読み取りサービス
```

### 3.4 Plan 9の教訓: "Everything is an API Call"

Plan 9は「すべてをファイルに」を徹底し、9Pプロトコル（わずか**13メッセージタイプ**）ですべてのリソースアクセスを統一した。

**Sodex Agentへの応用**:
- Plan 9の「すべてはファイル（9P）」→ Sodex Agentの「すべてはMCPツール呼び出し」
- 9Pがネットワーク透過だったように、MCPもローカル/リモートの区別なし
- Plan 9のプロセスごとの名前空間 → Sodex Agentのタスクごとのキャパビリティセット

---

## 4. テンポラリデータの安全な管理

### 4.1 メモリオンリーデータのライフサイクル

```
1. [受信] LLMレスポンスまたはMCPレスポンスをメモリバッファに受信
2. [暗号化] 受信即座にタスク固有の暗号鍵で暗号化
3. [処理] 暗号化されたデータを復号しながら処理
4. [送信] 処理結果を外部サービスに送信
5. [消去] 暗号鍵を破壊 → メモリをゼロフィル → バッファを解放
```

### 4.2 暗号的消去 (Crypto-Shredding)

最も重要なクリーンアップ手法:

- **原理**: すべての一時データをタスク固有の暗号鍵（MEK: Media Encryption Key）で暗号化。タスク完了時に鍵を破壊。暗号化データは復号不可能になる
- **速度**: 数秒で完了（物理上書きは数時間かかる）
- **標準準拠**: NIST推奨、ISO/IEC 27040準拠。最低128ビット鍵 + 128ビットエントロピー
- **Sodex Agentでの実装**: ChaCha20-Poly1305（i486にAES-NIなし、ソフトウェア実装に最適）

### 4.3 セキュアメモリ操作

| 操作 | 実装方法 | 備考 |
|------|---------|------|
| メモリゼロ化 | volatileポインタ経由のmemset | コンパイラ最適化による除去を防止 |
| メモリロック | ページテーブルのスワップ禁止ビット | スワップ領域への漏洩防止（Sodexにはスワップないので自動的に安全） |
| バッファ割り当て | kalloc + 暗号化 | BearSSL方式のcaller-allocatedバッファ |
| 解放時消去 | ゼロフィル → kfree | 解放前に必ず上書き |

### 4.4 シークレット管理

APIキー等の機密情報の取り扱い:

**最終方針**: Just-In-Time動的シークレット
```
1. 起動時: 最小権限の初期認証情報でVault/OAuthトークン発行エンドポイントに接続
2. 取得:  短命なアクセストークンまたは一時シークレットを取得
3. 使用:  メモリ上でのみ保持し、必要なMCP/LLM呼び出しに限定して使用
4. 更新:  有効期限前に自動ローテーション
5. 消去:  タスク完了時または期限切れ時に暗号的消去
```

**初期実装**（段階的導入）:
- Phase 1-2ではLLM API用の静的APIキーのみを使用し、外部MCPは使わない
- Phase 3ではローカル検証用または事前に固定したホワイトリスト先MCPのみを許可
- Phase 4でOAuth 2.1または同等の短命トークン運用を追加し、リビルドなしの資格情報更新を可能にする
- すべての資格情報はメモリ上でのみ保持し、タスク完了時にメモリゼロ化する

---

## 5. エージェントループの設計

### 5.1 ネットワークI/Oのみのエージェントループ

```
┌─────────────────────────────────────────────────────────┐
│  Sodex Agent Kernel                                      │
│                                                          │
│  ┌────────────────────────────────────────────────────┐  │
│  │  Agent Loop (永続的に実行)                          │  │
│  │                                                    │  │
│  │  1. [Observe] MCPサーバからコンテキスト取得         │  │
│  │       → MCP resources/read                        │  │
│  │       → メモリバッファに一時保持                    │  │
│  │                                                    │  │
│  │  2. [Think] LLM APIに送信                          │  │
│  │       → POST https://api.anthropic.com/v1/messages │  │
│  │       → SSEストリーミングでレスポンス受信           │  │
│  │       → tool_use レスポンスを解析                   │  │
│  │                                                    │  │
│  │  3. [Act] ツール呼び出し実行                       │  │
│  │       → MCP tools/call (Streamable HTTP)           │  │
│  │       → 結果をメモリバッファに一時保持              │  │
│  │                                                    │  │
│  │  4. [Report] 結果をLLMにフィードバック              │  │
│  │       → tool_result としてLLM APIに送信            │  │
│  │                                                    │  │
│  │  5. [Cleanup] 一時バッファの暗号的消去              │  │
│  │       → 暗号鍵破壊 → メモリゼロ化 → 解放          │  │
│  │                                                    │  │
│  │  6. [Loop] LLMがtool_useを返す限り2-5を繰り返す    │  │
│  │       → tool_useなしのレスポンス = タスク完了       │  │
│  └────────────────────────────────────────────────────┘  │
│                                                          │
│  ┌──────────┐ ┌──────────┐ ┌───────────┐ ┌──────────┐  │
│  │ TLS      │ │ HTTP/1.1 │ │ JSON      │ │ Crypto   │  │
│  │ (BearSSL)│ │ Client   │ │ Parser    │ │ Erasure  │  │
│  └──────────┘ └──────────┘ └───────────┘ └──────────┘  │
│  ┌──────────────────────────────────────────────────┐   │
│  │ TCP/IP Stack (uIP/lwIP) + DNS                     │   │
│  └──────────────────────────────────────────────────┘   │
│  ┌──────────────────────────────────────────────────┐   │
│  │ NE2000 / virtio-net Driver                        │   │
│  └──────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────┘
```

### 5.2 Claude API tool_use プロトコルの詳細

Claudeのtool_useレスポンス形式:
```json
{
  "role": "assistant",
  "content": [
    {
      "type": "text",
      "text": "ファイルの内容を確認します。"
    },
    {
      "type": "tool_use",
      "id": "toolu_01A09q90qw90lq917835lq9",
      "name": "read_file",
      "input": {"path": "/project/src/main.c"}
    }
  ],
  "stop_reason": "tool_use"
}
```

Sodex Agentの処理フロー:
1. `stop_reason: "tool_use"` を検出
2. `name` フィールドからMCPツール名を特定
3. `input` フィールドをMCP `tools/call` のパラメータとして送信
4. MCP結果を `tool_result` としてLLM APIの次のリクエストに含める
5. `stop_reason: "end_turn"` になるまでループ

### 5.3 ストリーミングレスポンス（SSE）

LLM APIからのストリーミング:
```
data: {"type":"content_block_start","index":0,"content_block":{"type":"text","text":""}}
data: {"type":"content_block_delta","index":0,"delta":{"type":"text_delta","text":"Hello"}}
data: {"type":"content_block_delta","index":0,"delta":{"type":"text_delta","text":" world"}}
data: {"type":"content_block_stop","index":0}
data: {"type":"message_delta","delta":{"stop_reason":"end_turn"}}
data: {"type":"message_stop"}
```

**Sodex Agentでの処理**:
- HTTPレスポンスを行単位でパース（`data: `プレフィックス）
- JSONデルタを逐次的にVGA画面に出力（リアルタイム表示）
- `tool_use`ブロックが完了した時点でMCPツール呼び出しを開始
- メモリ使用を最小化（全レスポンスをバッファリングしない）

---

## 6. 通信プロトコルスタック

### 推奨構成

```
┌─────────────────────────────────────────────┐
│ Layer 5: Agent Protocol                      │
│  ┌──────────┐ ┌──────────┐ ┌─────────────┐ │
│  │ Claude   │ │ MCP      │ │ A2A         │ │
│  │ API      │ │ Streamable│ │ (将来)     │ │
│  │ (SSE)    │ │ HTTP     │ │             │ │
│  └──────────┘ └──────────┘ └─────────────┘ │
├─────────────────────────────────────────────┤
│ Layer 4: Application Protocol                │
│  ┌──────────┐ ┌──────────┐                  │
│  │ HTTP/1.1 │ │ JSON-RPC │                  │
│  │          │ │ 2.0      │                  │
│  └──────────┘ └──────────┘                  │
├─────────────────────────────────────────────┤
│ Layer 3: Security                            │
│  ┌──────────────────────────────────────┐   │
│  │ BearSSL (TLS 1.2)                    │   │
│  │ ChaCha20-Poly1305 (暗号スイート)      │   │
│  └──────────────────────────────────────┘   │
├─────────────────────────────────────────────┤
│ Layer 2: Transport                           │
│  ┌──────┐ ┌──────┐ ┌──────┐               │
│  │ TCP  │ │ UDP  │ │ DNS  │               │
│  │      │ │      │ │Client│               │
│  └──────┘ └──────┘ └──────┘               │
├─────────────────────────────────────────────┤
│ Layer 1: Network                             │
│  ┌──────────┐ ┌──────────┐                  │
│  │ IP (v4)  │ │ ARP      │                  │
│  └──────────┘ └──────────┘                  │
├─────────────────────────────────────────────┤
│ Layer 0: Driver                              │
│  ┌──────────────────────────────────────┐   │
│  │ NE2000 / virtio-net                  │   │
│  └──────────────────────────────────────┘   │
└─────────────────────────────────────────────┘
```

### プロトコル選定理由

| プロトコル | 用途 | 選定理由 |
|-----------|------|---------|
| **HTTP/1.1** | LLM API / MCP通信 | HTTP/2は実装が複雑。HTTP/1.1で十分（MCP仕様が対応） |
| **JSON-RPC 2.0** | MCP通信 | MCP仕様の必須要件 |
| **SSE** | LLMストリーミング | 単方向で十分。WebSocketより実装が簡単 |
| **TLS 1.2** | 暗号化 | BearSSLが対応。TLS 1.3は将来対応 |
| **ChaCha20-Poly1305** | TLS暗号スイート | i486にAES-NI命令なし。ソフトウェア実装でタイミング攻撃耐性 |

---

## 7. セキュリティアーキテクチャ

### 7.1 ゼロトラストモデル

NIST SP 800-207の7原則をSodex Agentに適用:

| NIST原則 | Sodex Agent実装 |
|---------|-----------------|
| すべてのリソースを等しく保護 | 外部MCP通信はすべてTLS必須 |
| ネットワーク位置を信頼しない | すべてのMCPサーバに認証要求 |
| リクエストごとのアクセス制御 | Capabilityトークンによるツール呼び出しゲーティング |
| 最小権限 | タスクごとに必要最小限のMCPツールのみアクセス可能 |
| 継続的監視 | すべてのAPI呼び出しをシリアルコンソールにログ出力 |
| アクセスログ | 外部ログサービスに送信（MCP経由） |
| 適応的ポリシー | LLMの判断に基づくCapability動的調整（将来） |

### 7.2 Capability-Based ツールアクセス制御

```
タスク生成時:
  Task {
    id: "task_001",
    capabilities: [
      Cap::McpTool("filesystem", "read_file"),    // 読み取りのみ
      Cap::McpTool("github", "create_issue"),      // Issue作成のみ
      Cap::HttpEndpoint("api.anthropic.com:443"),   // LLM APIのみ
    ],
    // write_file, delete_file等は含まれない → アクセス不可
  }
```

LLMが`delete_file`ツールを要求しても、Capabilityに含まれていなければカーネルが拒否。

### 7.3 イミュータブルブート

```
1. QEMU起動 → ブートイメージ（読み取り専用）をロード
2. ブートローダ → カーネルをRAMにロード
3. カーネル初期化 → すべてRAM上で実行
4. ディスクへの書き込みは一切なし
5. 再起動 = 完全にクリーンな状態に復帰
```

---

## 8. アーキテクチャ全体図

```
                    ┌─────────────────────────────────┐
                    │        External Services         │
                    │                                  │
                    │  ┌───────┐ ┌───────┐ ┌───────┐ │
                    │  │Claude │ │GitHub │ │  S3   │ │
                    │  │  API  │ │  MCP  │ │  MCP  │ │
                    │  └───┬───┘ └───┬───┘ └───┬───┘ │
                    │      │         │         │      │
                    │  ┌───┴───┐ ┌───┴───┐ ┌───┴───┐ │
                    │  │Memory │ │  DB   │ │ Search│ │
                    │  │  MCP  │ │  MCP  │ │  MCP  │ │
                    │  └───┬───┘ └───┬───┘ └───┬───┘ │
                    └──────┼─────────┼─────────┼──────┘
                           │         │         │
                    ═══════╪═════════╪═════════╪══════════
                     HTTPS │   HTTPS │   HTTPS │  (TLS 1.2)
                    ═══════╪═════════╪═════════╪══════════
                           │         │         │
┌──────────────────────────┼─────────┼─────────┼──────────┐
│  Sodex Agent Kernel      │         │         │          │
│                          ▼         ▼         ▼          │
│  ┌──────────────────────────────────────────────────┐   │
│  │  MCP Client (Streamable HTTP + JSON-RPC 2.0)     │   │
│  │  ┌────────────┐ ┌──────────┐ ┌───────────────┐  │   │
│  │  │ Tool       │ │ Resource │ │ Capability    │  │   │
│  │  │ Dispatcher │ │ Fetcher  │ │ Checker       │  │   │
│  │  └────────────┘ └──────────┘ └───────────────┘  │   │
│  └──────────────────────────────────────────────────┘   │
│                          ▲                               │
│                          │                               │
│  ┌───────────────────────┴──────────────────────────┐   │
│  │  Agent Loop                                       │   │
│  │  ┌──────────┐ ┌──────────┐ ┌──────────────────┐  │   │
│  │  │ Observe  │→│  Think   │→│  Act             │  │   │
│  │  │ (MCP     │ │ (Claude  │ │ (MCP tools/call) │  │   │
│  │  │ resource)│ │  API)    │ │                  │  │   │
│  │  └──────────┘ └──────────┘ └──────────────────┘  │   │
│  │       ▲                              │            │   │
│  │       └──────── Cleanup ◄────────────┘            │   │
│  └───────────────────────────────────────────────────┘   │
│                                                          │
│  ┌──────────────────────────────────────────────────┐   │
│  │  Ephemeral Memory Manager                         │   │
│  │  ┌────────────┐ ┌─────────────┐ ┌─────────────┐ │   │
│  │  │ Encrypted  │ │ Crypto      │ │ Secure      │ │   │
│  │  │ Buffers    │ │ Erasure     │ │ Zero-fill   │ │   │
│  │  │ (ChaCha20) │ │ (Key Dest.) │ │             │ │   │
│  │  └────────────┘ └─────────────┘ └─────────────┘ │   │
│  └──────────────────────────────────────────────────┘   │
│                                                          │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐  │
│  │ BearSSL  │ │ HTTP/1.1 │ │ JSON     │ │ SSE      │  │
│  │ TLS 1.2  │ │ Client   │ │ Parser   │ │ Parser   │  │
│  └──────────┘ └──────────┘ └──────────┘ └──────────┘  │
│  ┌──────────────────────────────────────────────────┐   │
│  │ TCP/IP + DNS (uIP/lwIP)                           │   │
│  └──────────────────────────────────────────────────┘   │
│  ┌──────────────────────────────────────────────────┐   │
│  │ NE2000 / virtio-net │ PIT │ PRNG │ VGA │ KB     │   │
│  └──────────────────────────────────────────────────┘   │
└──────────────────────────────────────────────────────────┘
```

---

## 9. 実装フェーズ（改訂版）

前回のレポート（`autonomous_os_architecture_report.md`）のフェーズ計画を、障害切り分けしやすいように通信層ごとの検証点を明示して再分割する:

### Phase 1: Transport Bring-up（固定IP + 平文HTTP）

**目標**: DNS/TLS/SSEを入れる前に、QEMU上のSodexから固定IPのモックサーバへTCP接続し、平文HTTPの往復を確認する

| ステップ | 内容 | 依存 |
|---------|------|------|
| 1-1 | 固定IP宛てTCP接続の安定化（既存uIP/ソケット経路の確認） | なし |
| 1-2 | 最小HTTP/1.1クライアント（POST、ヘッダ、Content-Length） | 1-1 |
| 1-3 | JSONパーサ（最小限） | なし |
| 1-4 | ホスト側モックHTTPサーバとの結合テスト | 1-2, 1-3 |

**成果物**: `POST http://10.0.2.2:8080/echo` → レスポンス本文をVGAまたはシリアルに表示

### Phase 2: HTTPS + Claude 単独統合

**目標**: MCPをまだ入れず、Claude API単独でHTTPS/SSE経路を成立させる

| ステップ | 内容 | 依存 |
|---------|------|------|
| 2-1 | エントロピー源（PITタイマージッタPRNG） | なし |
| 2-2 | `memmove()`追加 | なし |
| 2-3 | BearSSL移植（I/OコールバックをuIP TCPに接続） | 2-1, 2-2 |
| 2-4 | DNSリゾルバ（UDPクエリ） | なし |
| 2-5 | TLS証明書検証またはピンニング | 2-3, 2-4 |
| 2-6 | TLS上のHTTP/1.1クライアント | 2-3, 2-4, 2-5 |
| 2-7 | SSEパーサ（レコード分割・断片受信対応） | 2-6 |
| 2-8 | Claude APIクライアント統合 | 2-6, 2-7 |

**成果物**: `POST https://api.anthropic.com/v1/messages` → ストリーミング応答を表示

### Phase 3: MCP Client + Deny-by-Default Security

**目標**: 外部ツール実行の前に、接続先と実行可能ツールを明示的に制限したMCP経路を作る

| ステップ | 内容 | 依存 |
|---------|------|------|
| 3-1 | JSON-RPC 2.0クライアント | Phase 2 |
| 3-2 | MCP `initialize` / `tools/list` / `resources/read` 実装 | 3-1 |
| 3-3 | Capabilityトークン構造体の定義 | 3-1 |
| 3-4 | タスクごとのCapabilityセット管理 | 3-3 |
| 3-5 | ホワイトリストベースのMCPサーバ接続制御 | 3-2, 3-4 |
| 3-6 | `tools/call` 実行前のCapabilityチェック | 3-4, 3-5 |
| 3-7 | Claude `tool_use` の解析・ディスパッチ | Phase 2, 3-6 |
| 3-8 | Agent Loop統合（Observe→Think→Act→Cleanup） | 3-2, 3-7 |

**成果物**: Claudeが許可済みMCPに対してのみツール呼び出しを行い、未許可ツールは実行前に拒否される

**制約**: この段階ではローカル検証用または管理下のMCPのみを対象とし、OAuth前提の外部SaaS MCPはまだ接続しない

### Phase 4: 認証ハードニングとシークレット更新

**目標**: 初期実装の固定資格情報を、短命トークンと更新可能な認証フローに置き換える

| ステップ | 内容 |
|---------|------|
| 4-1 | 起動時シークレット注入インターフェース |
| 4-2 | メモリ上のみで保持するシークレットストア |
| 4-3 | LLM APIキーとMCPトークンの期限管理 |
| 4-4 | OAuth 2.1または同等のトークン取得フロー |
| 4-5 | トークン更新失敗時のフェイルクローズ |
| 4-6 | 認証イベントの監査ログ |

**成果物**: リビルドなしで資格情報更新が可能になり、期限切れまたは更新失敗時は安全側に停止する

### Phase 5: Ephemeral Memory + Crypto Erasure

**目標**: 一時データと資格情報の安全な管理と自動クリーンアップ

| ステップ | 内容 |
|---------|------|
| 5-1 | ChaCha20-Poly1305のソフトウェア実装（BearSSLに含まれる） |
| 5-2 | タスク固有暗号鍵の生成・管理 |
| 5-3 | 暗号化バッファアロケータ |
| 5-4 | セキュアメモリゼロ化（volatile pointer write） |
| 5-5 | タスク完了時の自動クリーンアップ |

### Phase 6: 自律運用

| ステップ | 内容 |
|---------|------|
| 6-1 | ウォッチドッグタイマー（ハング検出・自動再起動） |
| 6-2 | ネットワーク障害時のリトライ・バックオフ |
| 6-3 | Memory MCPを利用した長期記憶 |
| 6-4 | 複数MCPサーバの並行呼び出し（将来） |

---

## 10. 技術的リスクと対策

| リスク | 影響 | 対策 |
|--------|------|------|
| **層をまたぐ同時実装** | 障害箇所が特定できず開発停止 | 固定IP/平文HTTP → TLS/Claude単独 → MCP の順に段階分割し、各Phaseに独立した完了条件を置く |
| **ネットワーク断** | 全機能停止 | 指数バックオフリトライ + 最小限のオフライン診断モード |
| **TLS 1.2非推奨化** | API接続不可 | BearSSL→picotls移行パス確保。当面TLS 1.2は有効 |
| **MCPサーバの信頼性** | ツール呼び出し失敗 | タイムアウト + フォールバック + LLMへのエラー通知 |
| **OAuth導入の複雑さ** | 初期 bring-up が遅延 | 外部SaaS MCP接続はPhase 4まで遅らせ、Phase 3は管理下MCPのみで検証 |
| **メモリ不足** | 大きなレスポンスで溢れ | ストリーミング処理でバッファ使用を最小化。最大バッファサイズ制限 |
| **LLM APIレート制限** | 処理遅延 | リトライ + バックオフ。429ステータスのハンドリング |
| **i486の演算速度** | TLSハンドシェイクが遅い | ChaCha20選択（AES-GCMより高速）。TLS session resumption |
| **プロンプトインジェクション** | 意図しないアクション | Capabilityゲート + ツール記述の検証レイヤー |

---

## 11. 保守性設計 — API仕様変更への耐性

### 11.1 問題の本質

Sodex AgentはLLM API・MCP・外部サービスに完全依存するため、**外部APIの仕様変更がカーネルの動作不能に直結する**。従来のOSではハードウェア仕様は数年〜数十年安定するが、Web APIは数ヶ月で変わりうる。これはステートレスAgent OS固有の最大の保守リスクである。

### 11.2 変更が予想されるポイント

| レイヤー | 変更頻度 | 変更内容の例 | 影響度 |
|---------|---------|------------|--------|
| **Claude API** | 高（年数回） | エンドポイントURL変更、ヘッダ追加、レスポンス形式変更、認証方式変更 | 致命的 |
| **MCP仕様** | 中（年1-2回） | トランスポート廃止（SSE→Streamable HTTP実例あり）、新プリミティブ追加 | 高 |
| **TLS** | 低（数年単位） | 暗号スイート廃止、プロトコルバージョン更新 | 高 |
| **MCPサーバ個別** | 高（随時） | ツール名変更、パラメータ追加/削除、レスポンス構造変更 | 中 |
| **HTTP** | 極低 | HTTP/1.1は安定。HTTP/2/3への移行圧力 | 低 |
| **JSON** | 極低 | 事実上不変 | 極低 |

### 11.3 設計原則: レイヤー分離と差し替え可能性

```
┌──────────────────────────────────────────────────────────────┐
│  変更しやすい（データ駆動・テーブル駆動）                       │
│  ┌────────────────────────────────────────────────────────┐  │
│  │ Config Layer (書き換えるだけで対応)                      │  │
│  │  - APIエンドポイントURL                                │  │
│  │  - HTTPヘッダ名・値                                    │  │
│  │  - APIバージョン文字列                                  │  │
│  │  - MCPサーバアドレス一覧                                │  │
│  │  - CA証明書                                            │  │
│  └────────────────────────────────────────────────────────┘  │
│  ┌────────────────────────────────────────────────────────┐  │
│  │ Adapter Layer (小規模な関数差し替えで対応)               │  │
│  │  - Claude APIリクエスト組み立て                         │  │
│  │  - Claude APIレスポンス解析                             │  │
│  │  - MCP初期化ハンドシェイク                              │  │
│  │  - 認証フロー（APIキー → OAuth等）                     │  │
│  │  - tool_use/tool_result のマーシャリング                │  │
│  └────────────────────────────────────────────────────────┘  │
│  ┌────────────────────────────────────────────────────────┐  │
│  │ Core Layer (めったに変更しない)                         │  │
│  │  - HTTP/1.1クライアント                                │  │
│  │  - JSON パーサ/シリアライザ                             │  │
│  │  - SSE パーサ                                          │  │
│  │  - JSON-RPC 2.0 クライアント                           │  │
│  │  - TLS (BearSSL)                                       │  │
│  │  - TCP/IP, DNS                                        │  │
│  │  - Agent Loop制御フロー                                │  │
│  │  - メモリ管理 / Crypto Erasure                          │  │
│  └────────────────────────────────────────────────────────┘  │
│  変更しにくい（ハードウェア依存）                              │
└──────────────────────────────────────────────────────────────┘
```

**核心**: API固有の知識をConfig LayerとAdapter Layerに閉じ込め、Core Layerには汎用的なHTTP/JSON/TLSの実装のみを置く。API仕様変更時にCore Layerは触らない。

### 11.4 具体的な保守性パターン

#### パターン1: テーブル駆動のHTTPリクエスト構築

APIのエンドポイント、ヘッダ、バージョン文字列をハードコードせず、設定テーブルで管理する。

```c
/* api_config.h — API仕様変更時はここだけ変更 */

struct api_endpoint {
    const char *host;           /* "api.anthropic.com"              */
    const char *path;           /* "/v1/messages"                   */
    u_int16_t   port;           /* 443                              */
};

struct api_header {
    const char *name;
    const char *value;
};

/* Claude API設定 — 仕様変更時はこの配列を書き換えるだけ */
PRIVATE const struct api_endpoint claude_endpoint = {
    .host = "api.anthropic.com",
    .path = "/v1/messages",
    .port = 443,
};

PRIVATE const struct api_header claude_headers[] = {
    { "content-type",      "application/json"  },
    { "x-api-key",         API_KEY             },
    { "anthropic-version", "2023-06-01"        },
    { NULL, NULL }  /* 終端 */
};
```

**変更例**: `anthropic-version`が`2024-01-01`に更新された場合、`claude_headers[]`の1行を書き換えてリビルドするだけ。

#### パターン2: Adapter関数によるリクエスト/レスポンスの分離

API固有のJSON構造の知識をAdapter関数に閉じ込める。

```c
/* claude_adapter.h — Claude API固有の変換ロジック */

/* リクエスト組み立て: 内部表現 → Claude APIのJSON形式 */
int claude_build_request(
    struct json_writer *jw,         /* 汎用JSONライター */
    const char *model,              /* モデル名 */
    const struct message *msgs,     /* メッセージ配列 */
    int msg_count,
    const struct tool_def *tools,   /* ツール定義（MCPから取得） */
    int tool_count
);

/* レスポンス解析: Claude APIのJSON → 内部表現 */
int claude_parse_response(
    const struct json_value *json,  /* パース済みJSON */
    struct agent_action *out        /* 解析結果: text / tool_use */
);

/* SSEデルタ解析: ストリーミングチャンクの逐次処理 */
int claude_parse_sse_delta(
    const char *event_data,
    struct stream_state *state      /* ストリーム状態 */
);
```

**変更例**: Claudeが`tool_use`のJSON構造を変更した場合、`claude_parse_response()`の1関数だけ修正。HTTPクライアントやJSONパーサは触らない。

#### パターン3: MCP Adapterの共通インターフェース

MCPサーバ側の仕様変更にも同じパターンを適用。

```c
/* mcp_adapter.h — MCP固有の変換ロジック */

struct mcp_client {
    struct api_endpoint endpoint;   /* MCPサーバのアドレス */
    char   session_id[64];          /* オプショナルなセッションID */
    int    protocol_version;        /* 交渉済みプロトコルバージョン */
};

/* ツール呼び出し: 内部表現 → MCP JSON-RPC */
int mcp_build_tool_call(
    struct json_writer *jw,
    const char *tool_name,
    const struct json_value *arguments
);

/* 結果解析: MCP JSON-RPC → 内部表現 */
int mcp_parse_tool_result(
    const struct json_value *json,
    struct tool_result *out
);
```

#### パターン4: プロバイダ抽象化（LLM切り替え対応）

将来的にClaude以外のLLM（OpenAI, Gemini等）にも対応するための抽象化。

```c
/* llm_provider.h — LLMプロバイダの抽象インターフェース */

struct llm_provider {
    const char *name;                      /* "claude", "openai", "gemini" */
    const struct api_endpoint *endpoint;

    /* リクエスト構築（プロバイダ固有） */
    int (*build_request)(struct json_writer *jw,
                         const char *model,
                         const struct message *msgs, int msg_count,
                         const struct tool_def *tools, int tool_count);

    /* レスポンス解析（プロバイダ固有） */
    int (*parse_response)(const struct json_value *json,
                          struct agent_action *out);

    /* SSEデルタ解析（プロバイダ固有） */
    int (*parse_sse_delta)(const char *event_data,
                           struct stream_state *state);

    /* HTTPヘッダ一覧（プロバイダ固有） */
    const struct api_header *headers;
};

/* プロバイダ登録 */
PRIVATE const struct llm_provider provider_claude = {
    .name           = "claude",
    .endpoint       = &claude_endpoint,
    .build_request  = claude_build_request,
    .parse_response = claude_parse_response,
    .parse_sse_delta = claude_parse_sse_delta,
    .headers        = claude_headers,
};

/* Agent Loopはプロバイダを意識しない */
int agent_think(const struct llm_provider *provider,
                const struct message *msgs, int msg_count,
                struct agent_action *out);
```

**効果**: LLMプロバイダの追加/切り替えが`struct llm_provider`の実装を1つ追加するだけで完了。Agent Loop本体のコードは一切変更不要。

### 11.5 変更シナリオ別の影響範囲

| 変更シナリオ | 修正対象 | 修正量 | Core Layer変更 |
|------------|---------|--------|---------------|
| APIバージョン文字列の更新 | `claude_headers[]` の1行 | 1行 | **不要** |
| エンドポイントURL変更 | `claude_endpoint` の1-2フィールド | 2行 | **不要** |
| 新しいHTTPヘッダの追加要求 | `claude_headers[]` に1エントリ追加 | 1行 | **不要** |
| `tool_use` JSONフォーマット変更 | `claude_parse_response()` | ~20-50行 | **不要** |
| SSEイベント形式変更 | `claude_parse_sse_delta()` | ~20-30行 | **不要** |
| 認証方式変更（APIキー→OAuth） | `claude_adapter` に認証フロー追加 | ~100行 | **不要** |
| MCP新プリミティブ追加 | `mcp_adapter` に新メソッド追加 | ~30-50行 | **不要** |
| MCPトランスポート変更 | Core Layer のHTTPクライアント拡張 | ~100-200行 | **必要** |
| TLS 1.3への移行 | BearSSL→picotls差し替え | 大規模 | **必要** |
| HTTP/2対応 | HTTPクライアント書き換え | 大規模 | **必要** |

**目標**: 頻度の高い変更（上4行）は**数行の修正とリビルドで完了**。Core Layer変更が必要なのはプロトコルレベルの大変更のみ。

### 11.6 リビルド・デプロイの容易さ

ステートレス設計の大きな利点: **内部に状態がないのでリビルド→再起動だけで完全に新仕様に移行できる**。

```
API仕様変更への対応フロー:

1. api_config.h または *_adapter.c を修正
2. make remake  (クロスコンパイル、~数秒)
3. QEMU再起動  (ブートイメージ差し替え)
4. 完了 — マイグレーション不要、データ移行不要、ロールバックはイメージ差し戻しだけ
```

従来のステートフルなOSではデータベースマイグレーション、設定ファイル更新、サービス再起動の順序管理が必要だが、Sodex Agentではイメージの差し替えだけで済む。

### 11.7 バージョンネゴシエーション

API仕様変更を実行時に検出・適応する仕組み:

```c
/* バージョン自動検出の例 */

int claude_negotiate_version(const struct llm_provider *provider)
{
    /* 1. 現在のバージョンでリクエストを試行 */
    int status = http_post(provider->endpoint, provider->headers, ...);

    if (status == 400 || status == 404) {
        /* 2. エラーレスポンスからヒントを取得 */
        /*    {"error":{"message":"unsupported anthropic-version"}} */
        com1_printf("API version mismatch detected\n");

        /* 3. フォールバックバージョンを試行 */
        /* 4. 結果をシリアルコンソールに出力 → 開発者に通知 */
        com1_printf("ACTION REQUIRED: Update anthropic-version in api_config.h\n");
        return -1;
    }

    return 0;
}
```

MCPについてはプロトコル自体にcapabilities negotiationが組み込まれているため:

```
1. MCP initialize → サーバがサポートするプロトコルバージョンを返す
2. クライアント側が対応可能なバージョンを選択
3. 非互換の場合はシリアルコンソールに警告出力
```

### 11.8 診断・デバッグ支援

API仕様変更の問題を素早く特定するための仕組み:

```c
/* シリアルコンソールへの詳細ログ出力 */

#define LOG_API_REQUEST  1   /* 送信HTTPリクエストをダンプ */
#define LOG_API_RESPONSE 1   /* 受信HTTPレスポンスをダンプ */
#define LOG_MCP_JSONRPC  1   /* MCP JSON-RPCメッセージをダンプ */
#define LOG_TLS_HANDSHAKE 0  /* TLSハンドシェイク詳細（通常はoff） */
```

- すべてのHTTPリクエスト/レスポンスをシリアルコンソール（`build/log/serial.log`）に出力
- 想定外のHTTPステータスコード、JSONパースエラー、MCPエラーコードを即座に報告
- ログレベルをコンパイル時フラグで制御（デバッグビルド vs リリースビルド）

### 11.9 テスト戦略

API仕様変更に対する回帰テスト:

```
tests/
├── test_claude_adapter.c    # Claude APIのリクエスト/レスポンス変換テスト
├── test_mcp_adapter.c       # MCP JSON-RPCの変換テスト
├── test_json_parser.c       # JSONパーサの単体テスト
├── test_sse_parser.c        # SSEパーサの単体テスト
├── test_http_client.c       # HTTPクライアントの単体テスト
└── fixtures/
    ├── claude_response_v1.json      # 現行API形式のサンプル
    ├── claude_response_tool_use.json
    ├── mcp_tools_list.json
    └── mcp_tool_result.json
```

ただし、fixtureベースのhost単体テストだけでは通信の断片化や再送を検出できない。そこで3層に分けてテストする:

1. **Adapter単体テスト（host）**
   JSON/SSE/JSON-RPC変換、異常レスポンス、未知フィールド追加をfixture差し替えで検出する
2. **モックサーバ結合テスト（host or QEMU）**
   固定IPの平文HTTPモック、TLS終端付きClaudeモック、MCPモックを用意し、SSE分割受信、429、5xx、タイムアウト、途中切断を再現する
3. **Phase別QEMUスモークテスト**
   Phase 1は固定IP平文HTTP、Phase 2はClaude単独HTTPS/SSE、Phase 3は許可済みMCPと拒否される未許可MCPの両方を毎回確認する

- **Adapterテストは最重要だが十分条件ではない**
- fixtureファイル（JSONサンプル）は実際のAPIレスポンスを記録して保存する
- `make test`ではhost上の単体テストを回し、別にQEMUスモークテストをCIジョブとして持つ
- API仕様更新時のワークフロー: 新レスポンスをfixture追加 → host単体テスト失敗確認 → Adapter修正 → モックサーバ結合テスト → QEMUスモーク通過 → リビルド

### 11.10 まとめ: 保守性設計の5原則

| # | 原則 | 実現手段 |
|---|------|---------|
| 1 | **Config/Adapter/Coreの3層分離** | API固有の知識をConfigとAdapterに閉じ込め、Coreは汎用プロトコル処理のみ |
| 2 | **テーブル駆動の設定** | エンドポイント、ヘッダ、バージョンは構造体配列で宣言的に管理 |
| 3 | **プロバイダ抽象化** | `struct llm_provider`の関数ポインタテーブルでLLM差し替え可能 |
| 4 | **ステートレスの利点活用** | 仕様変更対応 = リビルド＆再起動のみ。マイグレーション不要 |
| 5 | **段階別テスト** | fixtureによるAdapter単体、モックサーバ結合、Phase別QEMUスモークを分離して回帰検出 |

---

## 12. 他のアプローチとの比較（保守性観点を追加）

| 設計 | Sodex Agent | ChromeOS | Talos Linux | AIOS |
|------|------------|----------|-------------|------|
| **レイヤー** | ベアメタル | Linux上 | Linux上 | Python上 |
| **永続ストレージ** | なし | ローカルキャッシュあり | なし（K8s委譲） | ホストOS依存 |
| **管理インターフェース** | LLM API | GUI | gRPC API | Python API |
| **エージェント統合** | カーネルレベル | アプリレベル | なし | ミドルウェア |
| **セキュリティモデル** | Capability | POSIX + SELinux | mTLS + RBAC | ホストOS依存 |
| **最小イメージサイズ** | ~200 KB推定 | ~数GB | ~80 MB | N/A |
| **ブート時間** | ~1秒以内（推定） | ~10秒 | ~3秒 | N/A |
| **API変更への対応** | Config/Adapter修正→リビルド（数分） | アプリ更新 | API更新→再デプロイ | pip更新 |
| **仕様変更時の影響範囲** | Adapter層のみ（Core不変） | アプリ依存 | gRPC schema依存 | ライブラリ依存 |

---

## 13. GDPR/プライバシーの天然適合

ステートレス設計はGDPR Article 5のストレージ制限原則を自然に満たす:

- **データ最小化**: 必要なデータのみ一時的に処理
- **保存制限**: データは処理完了と同時に暗号的消去
- **忘れられる権利**: データを保持しないので削除要求自体が不要
- **監査**: すべてのAPI呼び出しログを外部サービスに送信（MCP経由）

**注意**: RAMへの一時保持もGDPRでは「処理」にあたるため、法的根拠は必要。

---

## 付録A: MCP Streamable HTTP リクエスト例

### ツール一覧の取得

```http
POST /mcp HTTP/1.1
Host: mcp-filesystem.example.com
Content-Type: application/json

{"jsonrpc":"2.0","id":1,"method":"tools/list","params":{}}
```

```json
{"jsonrpc":"2.0","id":1,"result":{"tools":[
  {"name":"read_file","description":"Read file contents","inputSchema":{
    "type":"object","properties":{"path":{"type":"string"}},"required":["path"]
  }},
  {"name":"write_file","description":"Write to file","inputSchema":{
    "type":"object","properties":{"path":{"type":"string"},"content":{"type":"string"}}
  }}
]}}
```

### ツールの実行

```http
POST /mcp HTTP/1.1
Host: mcp-filesystem.example.com
Content-Type: application/json

{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{
  "name":"read_file",
  "arguments":{"path":"/project/README.md"}
}}
```

```json
{"jsonrpc":"2.0","id":2,"result":{"content":[
  {"type":"text","text":"# Project README\n\nThis is the project..."}
]}}
```

## 付録B: 参考文献

### プロトコル仕様
- MCP Specification 2025-11-25: modelcontextprotocol.io/specification/2025-11-25
- MCP Transports (Streamable HTTP): modelcontextprotocol.io/specification/2025-03-26/basic/transports
- JSON-RPC 2.0: jsonrpc.org/specification
- AG-UI Protocol: docs.ag-ui.com

### ステートレスOS
- Talos Linux: talos.dev
- Flatcar Container Linux: flatcar-linux.org
- AWS Bottlerocket: aws.amazon.com/bottlerocket
- Stateless Systems (Lennart Poettering): 0pointer.net/blog/projects/stateless.html

### セキュリティ
- NIST SP 800-207 Zero Trust Architecture: csrc.nist.gov/pubs/sp/800/207/final
- Cryptographic Erasure (NIST): csrc.nist.gov/glossary/term/cryptographic_erase
- BearSSL: bearssl.org
- MCP Security Best Practices: modelcontextprotocol.io/specification/draft/basic/security_best_practices
- State of MCP Security 2025: datasciencedojo.com/blog/mcp-security-risks-and-challenges

### エージェントアーキテクチャ
- Claude Agent SDK Agent Loop: platform.claude.com/docs/en/agent-sdk/agent-loop
- AIOS (COLM 2025): arxiv.org/abs/2403.16971
- A2A Protocol: a2a-protocol.org

### 歴史的参考
- Oracle Network Computer: en.wikipedia.org/wiki/Network_Computer
- Sun Ray: en.wikipedia.org/wiki/Sun_Ray
- Plan 9 9P Protocol: en.wikipedia.org/wiki/9P_(protocol)
