# 自己進化カーネルのコード管理 — 永続化と外部同期

**執筆日**: 2026-03-20
**前提**: [self-modifying-kernel-architecture.md](self-modifying-kernel-architecture.md) の3層カーネル設計
**対象**: Sodex — LLMが自律的にカーネルコードを修正するOSのコード管理戦略

---

## 問題の本質

LLMがカーネル層のコードを自律的に修正する場合、3つの要求がある:

1. **永続化**: 再起動してもコード変更が失われない
2. **バージョン管理**: なぜ、いつ、何を変えたかの履歴
3. **外部同期**: ネットワーク上のリポジトリにコードを保存（障害復旧、人間のレビュー、複数インスタンス間の共有）

従来のOSでは開発者がgitを使う。しかしSodexでは**OS自身が自分のコードを管理する**。git CLIは使えない。ではどうするか。

---

## 2. 選択肢の評価

### 2.1 Git Smart HTTP Protocol（カーネルからgit push）

gitの内部プロトコルを直接実装してGitHubにpushする方法。

```
必要な実装:
├── pkt-line エンコード/デコード
├── packfile生成
│   ├── zlib圧縮（各オブジェクト）
│   ├── 可変長整数エンコード
│   └── デルタ圧縮（オプション）
├── SHA-1ハッシュ
├── ref negotiation
└── HTTP POST (multipart)
```

**評価**: ❌ **非現実的**

packfile生成だけで数千行の実装が必要。zlib圧縮ライブラリも必要。Sodexのfreestanding環境では投資対効果が悪すぎる。

### 2.2 GitHub REST API（HTTPSでcommit作成）

GitHubのREST APIを使い、**gitバイナリなしで**コミットを作成する方法。

```
必要なAPIコール（最小3リクエスト）:

① GET  /repos/{owner}/{repo}/git/ref/heads/{branch}
   → 現在のcommit SHAを取得

② POST /repos/{owner}/{repo}/git/trees
   {
     "base_tree": "{parent_tree_sha}",
     "tree": [{
       "path": "src/sched_module.c",
       "mode": "100644",
       "type": "blob",
       "content": "/* 新しいスケジューラコード */\n..."
     }]
   }
   → tree SHA取得（blobは自動生成される）

③ POST /repos/{owner}/{repo}/git/commits
   {
     "message": "[auto] LLM: スケジューラをCFS風に最適化",
     "tree": "{tree_sha}",
     "parents": ["{parent_commit_sha}"]
   }
   → commit SHA取得

④ PATCH /repos/{owner}/{repo}/git/refs/heads/{branch}
   { "sha": "{commit_sha}" }
   → ブランチ更新
```

さらに単一ファイル更新なら**1リクエスト**で完結:
```
PUT /repos/{owner}/{repo}/contents/{path}
{
  "message": "commit message",
  "content": "{base64エンコードされた内容}",
  "sha": "{既存ファイルのblob sha}"
}
```

**Sodexの既存資産との適合性**:

| 必要な機能 | Sodexの現状 | 追加作業 |
|-----------|------------|---------|
| HTTPS POST/GET/PATCH | ✅ http_client.c + tls_client.c | なし |
| JSON生成 | ⚠️ snprintfで生成可能 | テンプレート関数 ~100行 |
| JSON解析（sha抽出） | ⚠️ 部分的 | 簡易抽出関数 ~80行 |
| Base64エンコード | ❌ なし | ~60行 |
| Bearer Token認証ヘッダ | ✅ カスタムヘッダ対応済み | なし |
| DNS解決 (api.github.com) | ✅ dns.c | なし |

**評価**: ✅ **最も現実的。推定追加コード量 ~500-800行**

### 2.3 ローカルContent-Addressable Storage + 定期同期

Git風のオブジェクトモデルを簡略化してext3上に構築し、定期的にGitHub APIで同期する方法。

```
ext3ファイルシステム上の構造:

/versions/
├── objects/
│   ├── a1b2c3...  (SHA-1 → ファイル内容)
│   ├── d4e5f6...
│   └── ...
├── commits/
│   ├── 001.json   {"parent": "...", "tree": {...}, "msg": "...", "time": "..."}
│   ├── 002.json
│   └── ...
├── HEAD           最新コミット番号
└── sync_state     最後にGitHubに同期したコミット番号
```

**評価**: ⭐ **ローカル履歴として有用。GitHub APIと組み合わせるのが最善**

### 2.4 A/Bスロット方式（組み込みOTAから着想）

```
ext3上のレイアウト:

/kernel/
├── slot_a/
│   ├── modules/        ← 現在稼働中のKernel Servicesモジュール
│   │   ├── sched.o
│   │   ├── fs.o
│   │   └── net.o
│   └── manifest.json   ← モジュール一覧、バージョン、SHA
├── slot_b/
│   ├── modules/        ← 次バージョン（LLMが書き込み中）
│   │   └── ...
│   └── manifest.json
├── active_slot         ← "a" or "b"
└── history/
    ├── 001.diff        ← 変更差分ログ
    └── ...
```

**評価**: ✅ **ホットリロード+ロールバックと自然に統合できる**

### 2.5 Fossil SCM風（SQLiteベース）

SQLiteデータベース1ファイルにすべての履歴を格納する方法。

**評価**: ❌ **SQLiteの移植が必要で、コストが大きすぎる**

### 2.6 単純なHTTP PUT（S3/R2互換ストレージ）

```
PUT https://storage.example.com/sodex/snapshots/2026-03-20T12:34:56/sched_module.c
Authorization: Bearer {token}
Content-Type: application/octet-stream

{ファイル内容}
```

**評価**: ⚠️ **最もシンプルだが、バージョン管理なし。バックアップとしてなら有効**

---

## 3. 推奨アーキテクチャ: 3層コード管理

各層の修正に適したコード管理方法が異なる。

```
┌─────────────────────────────────────────────────────────┐
│ Layer 3: GitHub同期（外部永続化）                         │
│                                                         │
│ GitHub REST API経由でcommit & push                      │
│ ・LLMが生成したコードのレビュー可能な履歴                  │
│ ・人間が介入してrevert/修正できる                          │
│ ・複数Sodexインスタンス間の共有                            │
│ ・障害復旧のバックアップ                                   │
│                                                         │
│ 頻度: モジュールロード成功後 / 定期バッチ（低頻度でOK）     │
├─────────────────────────────────────────────────────────┤
│ Layer 2: ローカルバージョン管理（ext3上）                  │
│                                                         │
│ A/Bスロット + append-onlyコミットログ                     │
│ ・即座のロールバック                                       │
│ ・ネットワーク不要（オフライン動作可能）                    │
│ ・軽量で高速                                              │
│                                                         │
│ 頻度: モジュール修正のたびに記録                           │
├─────────────────────────────────────────────────────────┤
│ Layer 1: メモリ内バージョン（Core Kernel管理）             │
│                                                         │
│ 現モジュール + 1つ前のモジュールをRAMに保持                │
│ ・ゼロコストのロールバック（ポインタ切り替えのみ）           │
│ ・ディスクI/O不要                                         │
│                                                         │
│ 頻度: 常時（ホットリロードの一部）                          │
└─────────────────────────────────────────────────────────┘
```

---

## 4. GitHub REST API統合の設計

### 4.1 コンポーネント配置

GitHub APIクライアントは**ユーザーランドに配置**する。理由:

1. HTTPSスタック（BearSSL）が既にユーザーランドのlibagentにある
2. ネットワークI/Oの非決定的レイテンシをカーネルから分離
3. GitHub APIのJSON処理が複雑になりうる（カーネルに入れたくない）
4. 既存のhttp_client.c / tls_client.cをそのまま活用

```
アーキテクチャ:

Core Kernel                    Userland
┌──────────────┐              ┌──────────────────────┐
│ module_loader │              │ github_sync daemon   │
│              │              │                      │
│ モジュール    │   syscall    │ ① ext3からソース読み込み│
│ ロード/       │ ◄──────────► │ ② base64エンコード    │
│ アンロード    │   通知       │ ③ GitHub API呼び出し  │
│              │              │ ④ 結果をログ           │
│ A/Bスロット   │              │                      │
│ 管理          │              │ http_client.c        │
│              │              │ tls_client.c         │
│ commit_log    │              │ dns.c                │
│ (ext3上)      │              │                      │
└──────────────┘              └──────────────────────┘
```

### 4.2 同期フロー

```
[Core Kernel: モジュールホットリロード成功]
  │
  │ syscall: notify_module_updated("sched_module", version=3, slot="b")
  │
  ▼
[github_sync daemon]
  │
  │ ① ext3から更新されたソースファイルを読み取り
  │    /kernel/slot_b/src/sched_module.c
  │    /kernel/slot_b/manifest.json
  │
  │ ② コミットログから変更理由を取得
  │    /kernel/history/003.json
  │    → "LLM: コンテキストスイッチ頻度を30%削減。RRからCFS風に変更"
  │
  │ ③ GitHub API: 現在のref取得
  │    GET /repos/sodeyama/sodex-evolution/git/ref/heads/auto-evolve
  │    → parent_sha = "abc123..."
  │
  │ ④ GitHub API: tree作成（変更ファイルをinline content付きで）
  │    POST /repos/sodeyama/sodex-evolution/git/trees
  │    {
  │      "base_tree": "...",
  │      "tree": [
  │        {"path": "kernel-services/sched_module.c",
  │         "mode": "100644", "type": "blob",
  │         "content": "/* CFS-like scheduler ... */\n..."},
  │        {"path": "manifest.json",
  │         "mode": "100644", "type": "blob",
  │         "content": "{\"version\": 3, ...}"}
  │      ]
  │    }
  │    → tree_sha = "def456..."
  │
  │ ⑤ GitHub API: commit作成
  │    POST /repos/sodeyama/sodex-evolution/git/commits
  │    {
  │      "message": "[auto-evolve] sched_module v3: CFS風スケジューラに最適化\n\n...",
  │      "tree": "def456...",
  │      "parents": ["abc123..."],
  │      "author": {
  │        "name": "Sodex Kernel Agent",
  │        "email": "sodex@autonomous.os",
  │        "date": "2026-03-20T12:34:56Z"
  │      }
  │    }
  │    → commit_sha = "789abc..."
  │
  │ ⑥ GitHub API: ref更新
  │    PATCH /repos/sodeyama/sodex-evolution/git/refs/heads/auto-evolve
  │    {"sha": "789abc..."}
  │
  │ ⑦ ローカルsync_stateを更新
  │
  ▼
[完了: GitHubに同期済み]
```

### 4.3 GitHub上のリポジトリ構造

LLMの自律修正は**専用リポジトリ（またはブランチ）**に分離する:

```
sodeyama/sodex-evolution (GitHub)
├── kernel-services/
│   ├── sched_module.c
│   ├── fs_module.c
│   ├── net_module/
│   │   ├── uip.c
│   │   └── ne2000.c
│   └── syscall_handlers.c
├── manifest.json            ← 全モジュールのバージョン・SHA
├── evolution-log.json       ← 変更履歴（理由、メトリクス）
└── README.md                ← 自動生成: 現在の進化状態サマリー
```

**ブランチ戦略**:
- `auto-evolve`: LLMが自律的にpushするブランチ
- `main`: 人間がレビューしてマージするブランチ
- LLMは`main`には直接pushしない

これにより人間は:
- `auto-evolve`ブランチでLLMの変更履歴を追跡
- GitHub上でdiffを確認
- 問題があればrevertまたは修正をmainにマージ
- Pull Requestを自動作成することも可能

### 4.4 コミットメッセージの規約

LLMが生成するコミットメッセージに構造を持たせる:

```
[auto-evolve] {module_name} v{version}: {1行サマリー}

## 変更理由
{LLMが分析した問題と改善動機}

## 変更内容
{具体的な変更の説明}

## メトリクス
- 変更前: {測定値}
- 変更後: {測定値}
- 改善率: {%}

## ロールバック
前バージョン: v{version-1}
ロールバック方法: module_rollback("{module_name}")

---
Generated by: Sodex Kernel Agent (Claude API)
Timestamp: {ISO 8601}
Kernel uptime: {seconds}
```

---

## 5. ローカルバージョン管理の設計

### 5.1 A/Bスロット + コミットログ

```c
// kernel_versioning.h (Core Kernel)

#define MAX_MODULES     16
#define MAX_COMMIT_LOG  64
#define MODULE_NAME_MAX 32

struct module_version {
    char name[MODULE_NAME_MAX];
    u_int32_t version;
    u_int32_t code_size;       // バイト数
    u_int32_t load_addr;       // ロードされた仮想アドレス
    u_int32_t checksum;        // CRC32
};

struct commit_entry {
    u_int32_t id;              // 連番
    u_int32_t timestamp;       // PIT tickからの経過時間
    char module_name[MODULE_NAME_MAX];
    u_int32_t old_version;
    u_int32_t new_version;
    u_int32_t old_checksum;
    u_int32_t new_checksum;
    u_int8_t  synced;          // GitHubに同期済みか
    char message[128];         // 変更理由
};

struct slot_manifest {
    char active_slot;          // 'a' or 'b'
    u_int32_t module_count;
    struct module_version modules[MAX_MODULES];
};
```

### 5.2 ロールバック連鎖

```
時系列:

v1 (初期) ──→ v2 (LLM修正) ──→ v3 (LLM修正) ──→ v4 (LLM修正)
                                                    ↑ 現在稼働中
                                                ↑ RAMに保持（即時ロールバック可能）
                                            ↑ ext3のスロットに保存（ディスクから復元可能）
```

RAMには現在版と1つ前のみ保持。それ以前はext3のコミットログ+ソースから復元。

---

## 6. もう一つの低コスト選択肢: 共有メモリ経由のホストファイルシステム

QEMU環境特有のアプローチとして、**ホストOSとの直接的なファイル共有**がある。

### 6.1 QEMUの9pfs (virtio-9p) パススルー

```bash
# QEMU起動時に追加
qemu-system-i386 ... \
  -virtio-9p-pci,fsdev=code,mount_tag=hostcode \
  -fsdev local,id=code,path=/path/to/sodex-evolution,security_model=mapped
```

ゲストOS（Sodex）からホストのディレクトリに直接書き込み。ホスト側で通常のgit操作が可能。

**課題**: 9pfsドライバの実装が必要（virtioデバイス検出 + 9Pプロトコル）。実装コスト大。

### 6.2 シリアルポート経由のファイル転送

現在のシリアル通信を拡張して、ホスト側のスクリプトがファイルを受け取りgit操作する。

```
Sodex (ゲスト)                     ホストOS
┌──────────────┐                  ┌──────────────────────┐
│ Core Kernel   │  シリアル        │ host_sync.py          │
│ module更新    │ ─────────────►  │                       │
│ → ソース送信  │  COM1            │ ① ファイル受信         │
│              │                  │ ② git add/commit/push │
│              │  ◄─────────────  │ ③ 結果返送             │
│              │  ACK/NACK        │                       │
└──────────────┘                  └──────────────────────┘
```

**プロトコル**:
```
フレーム:
┌────────┬─────┬─────────────┬────────┬──────────┬──────────┐
│ 0xCAFE │ CMD │ PATH_LEN(2B)│ PATH   │ DATA_LEN │ DATA     │
│ magic  │ 1B  │             │ UTF-8  │ 4B       │ raw      │
├────────┼─────┼─────────────┼────────┼──────────┼──────────┤
│        │     │             │        │          │ CRC32    │
└────────┴─────┴─────────────┴────────┴──────────┴──────────┘

CMD:
  0x10 = FILE_WRITE   (ファイル内容をホストに書き込み)
  0x11 = FILE_READ    (ホストからファイル読み込み)
  0x20 = GIT_COMMIT   (ホスト側でgit commit)
  0x21 = GIT_PUSH     (ホスト側でgit push)
  0x22 = GIT_STATUS   (ホスト側のgit status取得)
  0xF0 = ACK
  0xF1 = NACK
```

**評価**: ⭐⭐ **最も低コストで即実装可能**

利点:
- Sodex側の追加実装は最小限（シリアル送信 + 簡易プロトコル ~200行）
- ホスト側はPythonスクリプト（gitコマンドをそのまま使える）
- ネットワーク障害の影響を受けない
- GitHub APIの複雑さをホスト側に委譲

欠点:
- QEMU環境に依存（実機では使えない）
- ホスト側スクリプトの起動が必要

---

## 7. 推奨: ハイブリッド戦略

段階的に実装し、環境に応じて選択する:

```
┌────────────────────────────────────────────────────┐
│           コード管理の全体像                          │
│                                                    │
│  ┌──────────────────────┐                          │
│  │ Core Kernel          │                          │
│  │ ├ A/Bスロット管理     │ ← Phase 0: 最初に実装     │
│  │ ├ コミットログ(ext3)  │                          │
│  │ └ RAM内ロールバック   │                          │
│  └──────────────────────┘                          │
│           │                                        │
│           │ モジュール更新通知                        │
│           ▼                                        │
│  ┌──────────────────────┐                          │
│  │ 同期レイヤー（選択式）│                          │
│  │                      │                          │
│  │ Option A: シリアル    │ ← Phase 1: QEMU開発環境  │
│  │   → ホストPythonが    │                          │
│  │     git commit/push  │                          │
│  │                      │                          │
│  │ Option B: GitHub API  │ ← Phase 2: 自律動作      │
│  │   → ユーザーランドの   │    （ネットワーク直接）    │
│  │     github_sync      │                          │
│  │                      │                          │
│  │ Option C: HTTP PUT    │ ← Phase 3: S3互換        │
│  │   → 任意のストレージ  │    （汎用バックアップ）    │
│  └──────────────────────┘                          │
│                                                    │
│  同期先: sodeyama/sodex-evolution:auto-evolve       │
│  人間レビュー: auto-evolve → main へPR              │
└────────────────────────────────────────────────────┘
```

### 実装優先度

| Phase | 内容 | 推定コード量 | 依存 |
|-------|------|------------|------|
| **Phase 0** | A/Bスロット + ext3コミットログ | ~300行 (C, Core Kernel) | なし |
| **Phase 1** | シリアル経由ファイル転送 + ホストPythonスクリプト | ~200行 (C) + ~150行 (Python) | Phase 0 |
| **Phase 2** | GitHub REST APIクライアント | ~500行 (C, Userland) | Phase 0, base64, JSON |
| **Phase 3** | HTTP PUT汎用ストレージ同期 | ~100行 (C, Userland) | Phase 0 |

**Phase 1を最初に実装すべき理由**: 開発中はQEMU上で動作するため、シリアル経由でホスト側のgitをそのまま使えるのが最も効率的。GitHub API実装（Phase 2）は、カーネルの自己修正が安定してから取り組めばよい。

---

## 8. セキュリティ考慮事項

### 8.1 認証トークンの管理

GitHub Personal Access Token (PAT) をどこに保存するか:

| 方法 | セキュリティ | 実装コスト |
|------|------------|-----------|
| ext3上の設定ファイル（平文） | ❌ ディスクに平文 | 最低 |
| シリアル経由でブート時に注入 | ⭕ ディスクに残らない | 低 |
| 環境変数的メモリ領域 | ⭕ RAM上のみ | 低 |
| ホスト側Pythonが管理（Phase 1） | ✅ ゲストに渡さない | 最低 |

**推奨**: Phase 1ではホスト側Pythonがトークンを管理（Sodexはトークンに触れない）。Phase 2ではブート時のシリアル注入。

### 8.2 不正なpush防止

LLMが悪意あるコードをGitHubにpushするリスク:

- **ブランチ保護**: `auto-evolve`ブランチのみpush可能。`main`にはPR必須
- **GitHub Actionsで自動チェック**: pushされたコードに対してCIでビルド検証
- **レート制限**: 1時間あたりのcommit数上限をgithub_syncに設定
- **人間レビューゲート**: `auto-evolve` → `main`のマージは人間が承認

---

## 9. 未解決課題

### 9.1 コンフリクト解決

複数のSodexインスタンスが同じブランチにpushしようとした場合:
- GitHub APIのref更新は`force: false`なら競合検出可能
- 解法: インスタンスごとにブランチを分離 (`auto-evolve-instance-1`, `auto-evolve-instance-2`)

### 9.2 大きなバイナリの扱い

コンパイル済み`.o`ファイルもGitHubに保存すべきか:
- **ソースのみ保存**を推奨。`.o`はソースから再生成可能
- バイナリはA/Bスロットのext3ローカルに保持すれば十分

### 9.3 ネットワーク障害時の挙動

GitHub APIへのpushが失敗した場合:
- ローカルコミットログに`synced = 0`として記録
- 次回同期時にまとめてpush（バッチ同期）
- カーネルの自己修正自体はネットワーク非依存で継続

---

## 参考文献

### Git/GitHub API
- [GitHub REST API - Git Database](https://docs.github.com/en/rest/git)
- [Git Smart HTTP Protocol](https://git-scm.com/docs/http-protocol)
- [git-commit-push-via-github-api (実装例)](https://github.com/azu/git-commit-push-via-github-api)

### 組み込みOTA更新
- [Comprehensive Embedded OTA Guide](https://witekio.com/blog/ota-update-solutions-the-ultimate-guide/)
- [OTA for Embedded Linux Devices](https://interrupt.memfault.com/blog/ota-for-embedded-linux-devices)
- [Exploring Open Source Dual A/B Update Solutions (FOSDEM 2025)](https://archive.fosdem.org/2025/events/attachments/fosdem-2025-6299-exploring-open-source-dual-a-b-update-solutions-for-embedded-linux/slides/237879/leon-anav_pyytRpX.pdf)

### バージョン管理システム設計
- [Fossil SCM Technical Overview](https://fossil-scm.org/home/doc/tip/www/tech_overview.wiki)
- [Fossil Design Philosophy](https://www.fossil-scm.org/home/doc/trunk/www/theory1.wiki)

### 先行Sodexドキュメント
- [self-modifying-kernel-architecture.md](self-modifying-kernel-architecture.md) — 3層カーネルアーキテクチャ
- [agent-os-fusion-thesis.md](agent-os-fusion-thesis.md) — エージェント融合のあるべき姿
