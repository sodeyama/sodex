# SearXNG for Sodex

Sodex agentがウェブ検索を行うためのSearXNGインスタンス。

## 起動・停止

```bash
cd searxng/
docker-compose up -d     # 起動
docker-compose down      # 停止
docker-compose logs -f   # ログ確認
```

## エンドポイント

- ホスト側: `http://localhost:8080/search?q=QUERY&format=json`
- QEMU SLiRP経由: `http://10.0.2.2:8080/search?q=QUERY&format=json`

## Sodexからの使い方

```
curl http://10.0.2.2:8080/search?q=hello+world&format=json
```

## JSONレスポンス構造

```json
{
  "query": "検索文字列",
  "number_of_results": 50,
  "results": [
    {
      "title": "ページタイトル",
      "url": "https://example.com/...",
      "content": "スニペット（要約テキスト）",
      "score": 4.0,
      "engines": ["google", "bing"]
    }
  ]
}
```

## 検索エンジン

Google, Bing, DuckDuckGo, Wikipedia が有効。
settings.yml で追加・変更可能。
