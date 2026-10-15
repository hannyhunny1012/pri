// クライアント ⇄ サーバー間のメッセージ。クライアントは「意図」だけを送り、座標は申告しない。

import type { ItemKind, ResourceKind } from "./constants";
import type { StampId } from "./stamps";

export type ClientMessage =
  | { t: "moveTo"; x: number; y: number }
  | { t: "dir"; x: number; y: number }
  | { t: "gather"; nodeId: number }
  | { t: "deliver" }
  | { t: "stamp"; id: StampId };

export interface PlayerSnap {
  id: string;
  name: string;
  x: number;
  y: number;
  wood: number;
  stone: number;
  /** 採取クールダウンが明ける時刻（サーバー時刻） */
  cooldownUntil: number;
  stamp: StampId | -1;
  stampUntil: number;
}

export interface Snapshot {
  /** サーバー時刻（ms） */
  t: number;
  towerCm: number;
  players: PlayerSnap[];
  /** ノードIDごとの残量 */
  nodeStock: number[];
}

export interface RankingEntry {
  name: string;
  cm: number;
}

export type RejectReason =
  | "rate_limited"
  | "too_far"
  | "cooldown"
  | "depleted"
  | "inventory_full"
  | "empty_inventory"
  | "invalid";

export type GameEvent =
  | { t: "gathered"; playerId: string; nodeId: number; kind: ResourceKind; item: ItemKind }
  | {
      t: "delivered";
      playerId: string;
      name: string;
      wood: number;
      stone: number;
      addedCm: number;
      towerCm: number;
    }
  | { t: "milestone"; index: number }
  | { t: "reject"; playerId: string; reason: RejectReason }
  | { t: "stamp"; playerId: string; id: StampId };

/** 自分の累計などの個人向け情報 */
export interface SelfInfo {
  totalCm: number;
  todayCm: number;
}
