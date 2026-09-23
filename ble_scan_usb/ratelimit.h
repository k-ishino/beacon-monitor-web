#pragma once
// アドレス単位のレートリミッタ。
//
// .bss 上の固定長オープンアドレステーブルで、ヒープを一切使わない。BLE の
// ランダムアドレスは頻繁に回転するため、ヒープで持つと際限なく確保と解放を
// 繰り返し、NimBLE が同居しているヒープを断片化させてしまう。
//
//   2048 スロット x 8 バイト = 16 KB （RL_BITS で RAM と精度を交換できる）
//
// エントリを明示的に削除する処理は無い。RL_STALE_MS より古いスロットは黙って
// 再利用されるので、回転して二度と来ないアドレスは放っておけば片付く。
//
// スキャンコールバックでの使い方:
//     if (!rlAllow(d->getAddress().getBase()->val)) return;   // NimBLE 2.x
//     ... パケットを送出 ...
//
// RL_MIN_GAP_MS を動かすのはこのヘッダではなくスケッチ側。既定は 0（間引き
// なし）で、ble_scan_usb.ino がホストの取りこぼしを検知したときだけ上げ、
// 収まれば戻す。現在値は 1 秒ごとの stat 行に "gap" として出る。

#include <Arduino.h>
#include <string.h>

#define RL_BITS      11                 // 11 なら 2048 スロット / 16 KB
#define RL_SLOTS     (1u << RL_BITS)
#define RL_MASK      (RL_SLOTS - 1u)
#define RL_PROBE     4                  // 線形探索の深さ

// アドレスごとの最小間隔。0 は素通しで、これが既定値。測定器である以上、
// 黙ってデータを間引くことがあってはならない。ホストが取りこぼし始めたときだけ
// スケッチが引き上げ、リンクが落ち着けば戻す。0 のときも rlAllow() は
// テーブルを更新し続けるので、切り替えに移行コストは無い。
static uint16_t RL_MIN_GAP_MS = 0;      // アドレス毎の最小間隔（自動調整）
static const uint32_t RL_STALE_MS = 30000;

struct RlSlot {
  uint8_t  a[6];
  uint16_t t;        // millis() >> 1
};
static RlSlot g_rl[RL_SLOTS];
static uint32_t g_rlSuppressed = 0;

static inline bool rlEmpty(const RlSlot* s) {
  return (s->a[0] | s->a[1] | s->a[2] | s->a[3] | s->a[4] | s->a[5]) == 0;
}

// アドレス 6 バイトに対する FNV-1a。ランダムアドレスは元々よく散っているが、
// パブリックアドレスは先頭に共通の OUI を持つため、6 バイト全部を混ぜる。
static inline uint32_t rlHash(const uint8_t* a) {
  uint32_t h = 2166136261u;
  for (int i = 0; i < 6; i++) { h ^= a[i]; h *= 16777619u; }
  return h ^ (h >> RL_BITS);
}

// このパケットを送出してよければ true を返す。
static bool rlAllow(const uint8_t* addr) {
  const uint16_t now  = (uint16_t)(millis() >> 1);
  const uint16_t gap  = RL_MIN_GAP_MS >> 1;
  const uint16_t stal = (uint16_t)(RL_STALE_MS >> 1);
  uint32_t base = rlHash(addr) & RL_MASK;

  // 第 1 段: このアドレスを既に追跡しているか。
  for (uint32_t k = 0; k < RL_PROBE; k++) {
    RlSlot* s = &g_rl[(base + k) & RL_MASK];
    if (!rlEmpty(s) && memcmp(s->a, addr, 6) == 0) {
      if ((uint16_t)(now - s->t) < gap) { g_rlSuppressed++; return false; }
      s->t = now;
      return true;
    }
  }

  // 第 2 段: 空きスロットか古いスロットを確保する。初見のアドレスは必ず通す。
  for (uint32_t k = 0; k < RL_PROBE; k++) {
    RlSlot* s = &g_rl[(base + k) & RL_MASK];
    if (rlEmpty(s) || (uint16_t)(now - s->t) > stal) {
      memcpy(s->a, addr, 6);
      s->t = now;
      return true;
    }
  }

  // 第 3 段: 探索範囲が全て埋まっていて、しかも新しい。パケットを捨てるのでは
  // なく最も古いものを追い出す。未知のデバイスを取りこぼさないことを優先する。
  RlSlot* victim = &g_rl[base];
  uint16_t oldest = (uint16_t)(now - victim->t);
  for (uint32_t k = 1; k < RL_PROBE; k++) {
    RlSlot* s = &g_rl[(base + k) & RL_MASK];
    uint16_t age = (uint16_t)(now - s->t);
    if (age > oldest) { oldest = age; victim = s; }
  }
  memcpy(victim->a, addr, 6);
  victim->t = now;
  return true;
}

// stat 行に出す使用スロット数。16 KB を走査するので毎秒 1 回までに留めること。
static uint32_t rlUsed() {
  uint32_t n = 0;
  for (uint32_t i = 0; i < RL_SLOTS; i++) if (!rlEmpty(&g_rl[i])) n++;
  return n;
}
