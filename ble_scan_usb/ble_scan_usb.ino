// ==== SF Version: Ver.1.01SF (作成日: 2026-09-22) ====
// このコメント行以外、ロジックは元プロジェクト(HLDC新井氏 作)から無改造で移植。
// 版数はこのファイルと README.md の変更履歴表を対で更新すること。
// ==========================================================
//
// ESP32-C3 の BLE アドバタイジングスキャナ。ネイティブ USB CDC へ NDJSON を吐く。
//
// WiFi は使わない。1 アドバタイジングパケットにつき JSON 1 個、改行区切り。ただし
// レートリミッタが効いている間は間引かれる（後述の "gap"）。
// 無線を取り合う相手がいないので、スキャンウィンドウをインターバルと同じ幅に
// 置ける（duty 100%）。WiFi を上げているスキャナより桁違いに拾える。
//
// ライブラリ: NimBLE-Arduino 2.x (h2zero)
// ボード:     ESP32C3 Dev Module
//   USB CDC On Boot ........ Enabled     <- 必須。Serial がネイティブ USB になる
//   Core Debug Level ....... None        <- ブートログをストリームに混ぜない
//   JTAG Adapter ........... Disabled
//
// ケーブルは C3 の*ネイティブ* USB ポート（GPIO18/19）に挿すこと。USB-UART
// ブリッジ側ではない。VID 0x303A / PID 0x1001 として列挙される。
//
// 行フォーマット:
//   {"a":"aa:bb:cc:dd:ee:ff","t":1,"r":-62,"m":123456,"p":"0201060909…"}
//   {"_":"stat","pps":312,"drop":0,"sup":0,"gap":0,"slots":74,"up":45,"act":1}
//   {"_":"hello","fw":"blescan-usb 1.2","build":"8459188","run":42,
//    "act":1,"itv":60,"win":60,"gap":0}
//
// "build" は CI がコンパイルしたコミット、"run" はワークフローの実行番号。
// モニタはこれを公開中のビルドと突き合わせ、古ければ書き込みを促す。手元
// ビルドは "dev"/0 と名乗る。
//
// "gap" はボードが現在適用しているアドレス毎の最小間隔。ホストが追いつかなく
// なるまでは 0（loop() の自動調整ブロックを参照）。0 でない間はストリームが
// 間引かれるので、adv 行から数えたデバイス毎のパケット数は実際を下回る。
// "pps" はフィルタの手前で数えているので影響を受けない。
//
// ホストから送る 1 文字コマンド:
//   a = アクティブスキャン（SCAN_RSP も要求）  p = パッシブスキャン
//   ? = hello を再送                           r = カウンタをリセット（無応答）
//
// モニタは接続した瞬間に 'r' を送る。それ以前の取りこぼしは「誰も CDC を
// 読んでいなかった」というだけで、リンクが今追いつけるかどうかについて何も
// 語らない。resetCounters() を参照。

#include <NimBLEDevice.h>
#include "ratelimit.h"

// CI がコンパイル直前にこのスケッチの隣へ build_info.h を生成し、ビルド元の
// コミットを埋め込む。このファイルは追跡していないので、手元の Arduino IDE でも
// そのままコンパイルが通る。その場合は "dev" と名乗り、モニタは「古い」ではなく
// 「比較できない」として扱う。
#if defined(__has_include)
#  if __has_include("build_info.h")
#    include "build_info.h"
#  endif
#endif
#ifndef FW_BUILD
#  define FW_BUILD "dev"
#endif
#ifndef FW_RUN
#  define FW_RUN 0
#endif

static const uint16_t SCAN_INTERVAL_MS = 60;
static const uint16_t SCAN_WINDOW_MS   = 60;   // インターバルと同値 = 連続スキャン
static bool           g_active         = true;

// ホストが読まなくなったとき、ブロックせずにパケットを捨てるための余裕。
static const int TX_HEADROOM = 340;

static const char HEXC[] = "0123456789abcdef";
static volatile uint32_t g_pkts = 0, g_drop = 0;
static uint32_t g_pktsPrev = 0, g_dropPrev = 0, g_lastStat = 0;
static uint8_t  g_clean = 0;          // 取りこぼしの無かった連続秒数

// ホストが名乗り出たときに呼ぶ。それまでボードは誰も汲み出していない CDC へ
// 向けてスキャンしており、送信バッファは埋まりっぱなしで全パケットが捨てられて
// いる。この取りこぼしは想定どおりで、何の意味も持たない。放置すると `drop` が
// 永久に非ゼロのまま残り、さらに悪いことにレートリミッタが上限 500ms まで
// 上がってしまう。そこから 0 に戻るにはリンクが綺麗な状態で約 50 秒かかる。
// 読み手がいないときに間引いても何も助からない。
static void resetCounters() {
  g_pkts = 0; g_pktsPrev = 0;
  g_drop = 0; g_dropPrev = 0;
  g_rlSuppressed = 0;
  RL_MIN_GAP_MS = 0;
  g_clean = 0;
}

static void hello() {
  Serial.printf("{\"_\":\"hello\",\"fw\":\"blescan-usb 1.2\","
                "\"build\":\"%s\",\"run\":%lu,\"act\":%d,"
                "\"itv\":%u,\"win\":%u,\"gap\":%u}\n",
                FW_BUILD, (unsigned long)FW_RUN,
                g_active ? 1 : 0, SCAN_INTERVAL_MS, SCAN_WINDOW_MS,
                RL_MIN_GAP_MS);
}

class ScanCB : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice* d) override {
    g_pkts++;                                              // フィルタ前の真のレート

    // アドレス毎に間引く。下の 1Hz 処理が有効にするまでは gap 0 で素通し。
    if (!rlAllow(d->getAddress().getBase()->val)) return;

    if (Serial.availableForWrite() < TX_HEADROOM) { g_drop++; return; }

    const std::vector<uint8_t>& pl = d->getPayload();
    char buf[600];
    int n = snprintf(buf, sizeof(buf),
                     "{\"a\":\"%s\",\"t\":%u,\"r\":%d,\"m\":%lu,\"p\":\"",
                     d->getAddress().toString().c_str(),
                     (unsigned)d->getAddress().getType(),
                     (int)d->getRSSI(),
                     (unsigned long)millis());
    if (n < 0) return;
    for (size_t i = 0; i < pl.size() && n < (int)sizeof(buf) - 4; i++) {
      buf[n++] = HEXC[pl[i] >> 4];
      buf[n++] = HEXC[pl[i] & 0x0f];
    }
    buf[n++] = '"';
    buf[n++] = '}';
    buf[n++] = '\n';
    Serial.write((const uint8_t*)buf, n);
  }
};
static ScanCB g_cb;

static void startScan() {
  NimBLEScan* s = NimBLEDevice::getScan();
  s->stop();
  s->setScanCallbacks(&g_cb, false);   // false = 重複も報告させる
  s->setActiveScan(g_active);
  s->setInterval(SCAN_INTERVAL_MS);    // NimBLE 2.x はミリ秒指定
  s->setWindow(SCAN_WINDOW_MS);        //（1.x は 0.625ms 単位だった）
  s->setDuplicateFilter(false);
  s->setMaxResults(0);                 // ライブラリ内部にバッファさせない
  s->start(0, false, true);            // 0 = 無期限
}

void setup() {
  Serial.setTxBufferSize(8192);        // begin() より前に呼ぶ必要がある
  Serial.begin(115200);                // USB CDC ではボーレートは無視される
  Serial.setTxTimeoutMs(0);            // 読み手がいなくてもブロックしない

  NimBLEDevice::init("");
  // NimBLE 2.x は dBm を直接取る。1.x / IDF の enum ESP_PWR_LVL_P9 は値が 7 な
  // ので、そのまま渡すと +9 ではなく +7 dBm を要求することになっていた。これは
  // 無害ではない。アクティブスキャンは SCAN_REQ を送信するため、送信電力が
  // 「どこまで遠いデバイスが SCAN_RSP を返してくれるか」を決める。
  NimBLEDevice::setPower(9);
  startScan();

  delay(300);
  hello();
}

void loop() {
  while (Serial.available()) {
    switch (Serial.read()) {
      case 'a': g_active = true;  startScan(); hello(); break;
      case 'p': g_active = false; startScan(); hello(); break;
      case '?': hello(); break;
      case 'r': resetCounters(); break;   // 無応答。続く a/p が hello を返す
    }
  }

  uint32_t now = millis();
  if (now - g_lastStat >= 1000) {
    uint32_t p = g_pkts, dr = g_drop;

    // アドレス毎の gap を自動調整する。リンクが追いついているかを知っているのは
    // ボードだけなので、ホストに問い合わせず自分で決める。実際にパケットを
    // 落とすまでは 0（間引きなし）、落とし始めたら 2 倍ずつ絞り、収まれば戻す。
    if (dr != g_dropPrev) {
      g_clean = 0;
      uint16_t g = RL_MIN_GAP_MS ? (uint16_t)(RL_MIN_GAP_MS * 2) : 25;
      RL_MIN_GAP_MS = g > 500 ? 500 : g;
    } else if (RL_MIN_GAP_MS && ++g_clean >= 5) {
      g_clean = 0;
      RL_MIN_GAP_MS = (RL_MIN_GAP_MS <= 25) ? 0 : (uint16_t)(RL_MIN_GAP_MS / 2);
    }

    Serial.printf("{\"_\":\"stat\",\"pps\":%lu,\"drop\":%lu,\"sup\":%lu,"
                  "\"gap\":%u,\"slots\":%lu,\"up\":%lu,\"act\":%d}\n",
                  (unsigned long)(p - g_pktsPrev), (unsigned long)dr,
                  (unsigned long)g_rlSuppressed, RL_MIN_GAP_MS,
                  (unsigned long)rlUsed(),
                  (unsigned long)(now / 1000), g_active ? 1 : 0);
    g_pktsPrev = p;
    g_dropPrev = dr;
    g_lastStat = now;
  }
  delay(5);
}
