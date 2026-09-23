# BLE iBeacon Monitor (Web版)

ESP32-C3 で BLE のアドバタイジングパケットを受信し、USB CDC でホスト PC に送って
ブラウザで **iBeacon のみに絞り込んで** 表示します。

元プロジェクト [blescanner-esp32c3]（HLDC新井氏 作、汎用 BLE モニタ）から独立させ、
Windows 版 `BeaconMonitor`（Ver.1.5）の画面構成・機能をこの Web アプリに移植したものです。

```
[ESP32-C3]  BLE アドバタイズを受信
     |      USB CDC (USB Serial/JTAG) — NDJSON、1 行 1 パケット
     v
[Mac / Win] Web Serial API — iBeacon モニタ（/）
```

**前提**: ボードは WiFi に接続しない。ホスト PC は macOS または Windows のデスクトップ。
ホスト PC にツールをインストールせず、ブラウザだけで書き込み・観測を完結させる
（Arduino IDE は使わない）。

**公開先**: https://k-ishino.github.io/beacon-monitor-web/

| | |
|---|---|
| **このファイル** | 仕様・手順・変更履歴 |
| **設計の背景（元プロジェクト）** | 元リポジトリの `CLAUDE.md` を参照 |

---

## バージョン管理ルール

このアプリのバージョンは `Ver.X.XXSF` の形式で表記します（**SF = StoneField の略**）。

- 初版: Ver.1.00SF
- 以降、変更のたびに `1.01SF` → `1.02SF` → … と増分する
- 版数はこの README の「変更履歴」表・**README のファイル名**・ファームウェアのソース
  コード冒頭コメントの三箇所を対で更新する
- README のファイル名は `README_VerX.XXSF.md` とし、対象プログラムの版数と一致させる
- 仕様や挙動を変更した場合は、新しいセクションを作らず **この README に追記する形**で
  記録する(変更点は下記の変更履歴表、詳細は各セクション本文を直接更新)

### 変更履歴

| バージョン | 作成日時 | 内容 |
|---|---|---|
| Ver.1.00SF | 2026-09-22 | 初版。新リポジトリの立ち上げ、仕様書の作成、バージョン管理ルールの制定 |
| Ver.1.01SF | 2026-09-22 | 元プロジェクト作者の表記を「HLDC新井氏」に修正、SF の意味を「StoneField」に修正。README ファイル名に版数を付与。ファームウェア(`ble_scan_usb.ino`/`ratelimit.h`)を無改造で移植し、ソース冒頭に版数コメントを追加 |
| Ver.1.02SF | 2026-09-22 | `web/index.html` を新規作成し、iBeacon 専用モニタとして実装(手順3完了)。UUID フィルタ(登録10件)、UUID/Major/Minor/RSSI/最終検出/コメント/BD Address 列、上書き更新、ソート、UUID 色分け、コメント編集(localStorage)、CSV ログ(File System Access API)、スリープ防止(Wake Lock API)を実装 |
| Ver.1.03SF | 2026-09-22 | 動作確認後のフィードバックを反映。①元プロジェクトからRSSI下限スライダー・表示の一時停止/再開・一覧クリアを復活。②ヘッダ表記を元プロジェクトと統一(「デバイス」「pkt/s」「パケット」)。③ログ保存・スリープ防止をチェックボックス化し既定OFFに統一。④アクティブスキャンの選択を廃止しパッシブ固定(iBeaconの受信内容に影響しないため)。⑤登録UUIDフィルタのON/OFFチェックボックスを追加。⑥コメントのダブルクリック編集が反応しない不具合を修正(受信のたびに表全体を再描画していたのが原因。描画を250ms間隔にまとめる方式に変更) |
| Ver.1.04SF | 2026-09-22 | ①「表示を停止」「一覧をクリア」を操作バーの右端(間を空けて)へ移動。②RSSI下限・登録UUIDのみ表示・ログ保存・スリープ防止の各項目を白線の枠で囲んで整理。③UUID列のソート不具合を修正(見出しの`data-k`とデータ側のプロパティ名`uuidStr`が一致しておらず、UUID全32桁を比較できていなかった) |
| Ver.1.05SF | 2026-09-22 | ①RSSI下限・登録UUIDのみ表示・ログ保存・スリープ防止の枠線の色を、ボタンと同じ灰色(`--rule`)に統一。②「ボードに接続」「切断」「表示を停止」「一覧をクリア」のフォント指定を統一(font-size/font-familyを明示指定し、ブラウザ差異の余地をなくした) |
| Ver.1.06SF | 2026-09-22 | コメントの書き出し/読み込み機能を追加。「コメント書き出し」で `iBeacon_monitor_comment.txt`(BD Address とコメントのタブ区切りテキスト)としてダウンロードでき、「コメント読み込み」で別PC上の同ファイルを読み込んで `localStorage` へ反映できる(別PCへのコメント持ち出しに対応) |
| Ver.1.07SF | 2026-09-22 | **重大バグ修正。** Ver.1.06SF で追加したコメント書き出し/読み込みのイベント登録が、ボタン取得用の `$` 関数の定義より前の行に置かれており、起動直後に `ReferenceError` で止まっていた。これにより以降の全スクリプト(接続ボタンの処理を含む)が一切実行されず、「ボードに接続」を押しても無反応になっていた。該当のイベント登録を `$` 定義後の位置へ移動して解消 |
| Ver.1.08SF | 2026-09-23 | ①CSVログ処理を全面改修。従来は受信1件ごとにファイルを開閉しており、書き込み失敗時に `writable.close()` へ到達せず `*.crswap` 一時ファイルが閉じられないまま残る不具合があった。行をいったんメモリに溜め、1秒ごとに1回だけ開閉する方式に変更し、`finally` で必ず close するようにした。ログ保存フォルダを選ぶたびに、残っている `*.crswap` を自動で掃除する処理も追加。②`showDirectoryPicker` に `{mode:"readwrite"}` を明示指定。③BD Address の大文字/小文字を区別せず同一デバイスとして扱うよう、コメントの内部キーを正規化(表示・編集・TXT書き出し/読み込みすべてに適用)。④コメントTXTの書式(`#`始まりはコメント行、空行は無視、BD Addressの大小文字は不問)を書き出しファイル冒頭に明記。⑤操作バー上の「コメント書き出し」「コメント読み込み」を囲んでいた枠を外し、他の区切りと同じ縦線に変更 |
| Ver.1.09SF | 2026-09-23 | GitHub Pages で https://k-ishino.github.io/beacon-monitor-web/ として公開。①`web/setup.html`・`tools/esp-web-tools/` を元プロジェクトから移植(ブラウザから直接ファームウェアを書き込むページ)。②`.github/workflows/build.yml` を新規作成。arduino-cli でのビルド・esp-web-tools のバンドルは元プロジェクトと同じ内容とし、デプロイ先だけ Cloudflare Workers から `actions/upload-pages-artifact` + `actions/deploy-pages` による GitHub Pages 公開に変更(Cloudflare アカウント・Secrets 設定が一切不要になった)。③`web/index.html` に、setup.html とシリアルポートを譲り合うための `BroadcastChannel` 連携を追加(元プロジェクトの仕組みをそのまま移植)。④`.gitignore` を追加(CI生成物・ビルド成果物を除外) |

---

## ディレクトリ

```
ble_scan_usb/     ble_scan_usb.ino, ratelimit.h      元プロジェクトから無改造で移植 ※移植済み
web/              index.html                         iBeacon 専用モニタ ※実装済み
                  setup.html                          書き込みページ(ESP Web Tools) ※移植済み
tools/esp-web-tools/                                  esp-web-tools をバンドルする依存の固定 ※移植済み
.github/workflows/build.yml                          ビルド → GitHub Pages へデプロイ ※実装済み
.gitignore                                            CI生成物を除外 ※作成済み
```

元プロジェクトとの主な違いは配信先です。元は Cloudflare Workers でしたが、本リポジトリは
**GitHub Actions + GitHub Pages**(`actions/upload-pages-artifact` + `actions/deploy-pages`)
で完結させます(Cloudflare アカウント・Secrets 設定が不要)。ビルド内容(arduino-cli による
ファームウェアビルド、esp-web-tools のバンドル)自体は元プロジェクトと同じ仕組みを踏襲します。

## ボード設定

「ESP32C3 Dev Module」（元プロジェクトと同一。ファームウェアは変更しないため設定も変わらない）

| 項目 | 値 | 理由 |
|---|---|---|
| USB CDC On Boot | **Enabled** | `Serial` をネイティブ USB にする。必須 |
| Core Debug Level | **None** | ブートログを NDJSON ストリームに混ぜない |
| JTAG Adapter | Disabled | |

## ファームウェア（移植済み・Ver.1.01SF で移植）

`ble_scan_usb/ble_scan_usb.ino`、`ble_scan_usb/ratelimit.h` を **無改造**で移植しました。

- 変更点は `ble_scan_usb.ino` 冒頭に追加した版数コメント(`SF Version: Ver.1.01SF`)のみ
- 動作ロジック・シリアルプロトコルは一切変更していません
- ファームウェア自身が `hello` で名乗る `"fw":"blescan-usb 1.2"` は元プロジェクトの
  内部バージョン表記（モニタの版チェック用）であり、**上記の SF バージョンとは別物**です。
  混同しないよう、この README では明確に区別して扱います

## シリアルプロトコル

ファームウェアは無改造で移植したため、プロトコルは元プロジェクトと完全に同一です
（NDJSON、1 行 1 レコード、`a`/`t`/`r`/`m`/`p` のアドバタイジングパケット、
`_:"stat"` の統計、`_:"hello"` の版情報、コマンド `a`/`p`/`?`/`r`）。詳細は元プロジェクトの
README を参照してください。本リポジトリ側での変更点は **Web モニタ側の解析・表示ロジックのみ**です。

## モニタ（iBeacon 専用・実装済み Ver.1.03SF）

`web/index.html`。Windows 版 `BeaconMonitor`（Ver.1.5）と、元プロジェクト(HLDC新井氏 作)の
汎用モニタ双方の機能を取り込んだ、iBeacon 専用の単一ファイル Web モニタです。

実装済み:
- 登録済み UUID(10件、末尾3バイトはワイルドカード)によるフィルタ表示。リストは
  `TARGET_UUID_PREFIXES` としてソース冒頭にハードコードし、Windows 版と同一内容
- **登録UUIDのみ表示** チェックボックス(既定 ON)。OFF にすると、登録外の UUID でも
  iBeacon 形式であれば表示する(新しいビーコンの UUID を調べる用途)
- **RSSI 下限スライダー**(元プロジェクトから移植。既定 -100 = 絞り込みなし)
- BD Address 単位での行の上書き更新
- UUID / Major / Minor / RSSI / 最終検出時刻 / コメント / BD Address 列
- 列見出しクリックによる昇順/降順ソート
- UUID ごとの行色分け(出現順に8色を自動割当)
- コメント編集(セルをダブルクリック → `localStorage` に永続化、次回起動時も引き継ぐ)
- **表示を停止/再開**(元プロジェクトから移植。受信・記録は継続したまま画面更新のみ止める。
  コメント編集時など、行が動くと困る場面で使う)
- **一覧をクリア**(元プロジェクトから移植)
- ログ保存チェックボックス(既定 OFF) → 日付別 CSV(`beacon_log_YYYYMMDD.csv`、
  **File System Access API** で保存先フォルダを一度選択し、以後追記。UTF-8 BOM 付き)
- スリープ防止チェックボックス(既定 OFF、Screen Wake Lock API)
- ヘッダ表記は元プロジェクトと統一: 「デバイス」(検出中の台数)/「pkt/s」(1秒あたりの
  一致件数)/「パケット」(累計一致件数)

**アクティブスキャンの選択は廃止しました。** iBeacon の UUID/Major/Minor はすべて一次
アドバタイズパケットに収まっており SCAN_RSP を使わないため、アクティブ/パッシブの違いは
このアプリが受信する内容に影響しません。常にパッシブスキャンで接続します。

**スリープ防止(Wake Lock API)の制約について。** ブラウザのタブが**表に出ている間だけ**、
画面・システムの自動スリープ(アイドルによる)を防ぎます。タブを裏に回す・ウィンドウを
最小化する・PCを手動でスリープさせる(ノートPCの蓋を閉じる等)といった操作には対抗できません。
Windows 版の `SetThreadExecutionState` のように「最小化していても効く」ものではない点に
ご留意ください。この制約を踏まえた上で今回は機能として残していますが、不要であれば削除も
可能です。

Windows 版との相違点(簡略化した箇所):
- **行の色**: Windows 版と同じ8色構成ですが、ダークテーマに合わせて明度を落とした配色に
  調整しています(色そのものはWindows版の淡色とは異なります)
- **設定の永続化**: Windows 版は `.ini` にウィンドウ位置・列幅・起動時ログモードを保存しますが、
  本版ではブラウザの制約もあり未実装です。コメントのみ `localStorage` で永続化しています
  (必要であれば追加実装します)
- **コメント編集**: Windows 版のダイアログに合わせ `prompt()` を使用したシンプルな実装です

**不具合修正(Ver.1.03SF)。** コメント欄のダブルクリック編集が開かない不具合がありました。
原因は、iBeacon を受信するたびに表全体を作り直していたため、ダブルクリックの1回目と2回目の
間に行の DOM 要素が入れ替わり、ブラウザが同一要素へのダブルクリックと認識できなかったことです。
元プロジェクトに倣い、画面の再描画を 250ms 間隔にまとめる方式に変更し解消しました。

**データの保存場所(コメント / CSVログ)について。**

| データ | 保存場所 | ファイル名 | 別PCへのコピー |
|---|---|---|---|
| コメント | ブラウザの `localStorage`(既定)。加えて **書き出し/読み込み機能で TXT ファイル化可能(Ver.1.06SF〜)** | `iBeacon_monitor_comment.txt`(書き出し時に生成) | **可能**(書き出したファイルを別PCで「コメント読み込み」すれば反映される) |
| CSVログ | 「ログ保存」チェックON時に選んだフォルダの直下 | `beacon_log_YYYYMMDD.csv`(日付ごとに自動で新規作成) | **可能**。ただの CSV テキストファイルなので、コピーして Excel 等でそのまま開ける |

コメントは既定では `localStorage`(そのPC・そのブラウザ内)に保存されますが、「コメント書き出し」ボタンで
`iBeacon_monitor_comment.txt`(BD Address とコメントをタブ区切りにしたテキストファイル)としてダウンロード
できます。これを別PCに持っていき「コメント読み込み」ボタンで同ファイルを選択すると、そのPCの
`localStorage` にコメントが反映されます(同じ BD Address のコメントは上書き、それ以外は保持)。
CSV ログには現状コメント列を含めていません。

## 書き込みページ（移植済み Ver.1.09SF）

`web/setup.html`（元プロジェクトから移植、リンク先のみ本リポジトリに変更）。ESP Web Tools を
使い、Arduino IDE を使わずブラウザだけでファームウェアを書き込みます。ファームウェア自体は
無改造のため、書き込み対象の `.bin` はビルドパイプラインで元プロジェクトと同じ手順により生成
します。

シリアルポートは1ページしか掴めないため、モニタ(`index.html`)とセットアップページ
(`setup.html`)の間で `BroadcastChannel` を使ってポートを譲り合う仕組みを、元プロジェクトから
モニタ側にも移植しました。setup.html を開くとモニタは自動で切断し、setup.html を閉じる/離れる
とモニタが自動で再接続します。

## 配信（GitHub Actions + GitHub Pages、実装済み）

```
arduino-cli build → merge_bin → manifest.json / build.json
  → esp-web-tools をバンドル
  → index.html / setup.html / esp-web-tools.js
    / firmware-merged.bin / manifest.json / build.json を集約
  → actions/upload-pages-artifact → actions/deploy-pages で GitHub Pages へデプロイ
```

Cloudflare を使わない点のみ元プロジェクトと異なり、ビルドの中身は同一です。Secrets の設定は
不要です(GitHub Pages への発行権限は `permissions: pages: write / id-token: write` により
ワークフロー自身に付与されます)。

**重要**: このワークフローを有効にするには、リポジトリの Settings → Pages → Source を
「Deploy from a branch」から**「GitHub Actions」に変更**する必要があります(手動アップロードで
最初に公開した際の設定のままだと、このワークフローの成果物が反映されません)。切り替え後は、
最初に手動アップロードした直下の `index.html` / `README*.md` は使われなくなるため、削除しても
構いません(残しておいても実害はありません)。

## 対応ブラウザ

| | Web Serial | File System Access |
|---|---|---|
| Chrome / Edge（デスクトップ） | ○ | ○ |
| Firefox（デスクトップ） | ○ | × |
| Safari | × | × |

File System Access API は Chrome/Edge のみの対応のため、本アプリは
**Chrome または Edge（デスクトップ）を推奨環境**とします。

## 今後の作業（Ver.1.09SF 時点）

1. ~~ファームウェア(`ino`/`ratelimit.h`)の無改造移植~~ → **完了(Ver.1.01SF)**
2. ~~`web/index.html` の iBeacon 専用改修~~ → **完了(Ver.1.02SF〜)**
3. ~~`web/setup.html`・`tools/esp-web-tools/` の移植~~ → **完了(本版)**
4. ~~`.github/workflows/build.yml` を GitHub Pages 向けに調整~~ → **完了(本版)**
5. リポジトリ Settings → Pages → Source を「GitHub Actions」に切り替え、ワークフローを
   実行して実際にファームウェア書き込みまで通しで確認する
6. 実機での UUID フィルタ・CSV ログ・コメント永続化・書き込み(setup.html)の通し動作確認
