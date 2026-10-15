// サーバー権威のシミュレーション本体。
// 今はブラウザ内で動かしているが、Phaser / DOM に依存しないので後で Colyseus の Room からそのまま使う。

import {
  CM_PER_ITEM,
  DELIVER_RANGE,
  GATHER_COOLDOWN_MS,
  GATHER_RANGE,
  INVENTORY_MAX,
  ITEM_OF,
  NAME_MAX_LEN,
  NODE_RESPAWN_MS,
  NODE_STOCK,
  PLAYER_RADIUS,
  PLAYER_SPEED,
  STAMP_SHOW_MS,
  TOWER_RADIUS,
  TOWER_X,
  TOWER_Y,
  WORLD_H,
  WORLD_W,
} from "./constants";
import { type IslandMap, Tile, tileAt } from "./map";
import { isZoneUnlocked, reachedCount } from "./milestones";
import type {
  ClientMessage,
  GameEvent,
  PlayerSnap,
  RankingEntry,
  RejectReason,
  SelfInfo,
  Snapshot,
} from "./protocol";
import { TokenBucket } from "./rateLimiter";
import { isValidStamp } from "./stamps";

/** 永続化の窓口。ブラウザ版は localStorage、サーバー版は SQLite / PostgreSQL で実装する */
export interface ContributionStore {
  loadTowerCm(): number;
  /** 納品を記録し、反映後の個人の累計を返す（タワーの高さも同じ単位で加算すること） */
  recordDelivery(token: string, name: string, addedCm: number, day: string): SelfInfo;
  getSelf(token: string, day: string): SelfInfo;
  topToday(day: string, limit: number): RankingEntry[];
}

/** 日本時間の日付（YYYY-MM-DD）。ランキングの区切りに使う */
export function dayJst(epochMs: number): string {
  return new Date(epochMs + 9 * 3600_000).toISOString().slice(0, 10);
}

export function sanitizeName(raw: unknown): string | null {
  if (typeof raw !== "string") return null;
  // 制御文字・改行・ゼロ幅文字・文字方向の制御文字を除去
  const s = raw.replace(/[\p{Cc}\p{Cf}\p{Zl}\p{Zp}]/gu, "").trim();
  const chars = Array.from(s);
  if (chars.length === 0) return null;
  return chars.slice(0, NAME_MAX_LEN).join("");
}

interface Player {
  id: string;
  token: string;
  name: string;
  x: number;
  y: number;
  targetX: number | null;
  targetY: number | null;
  dirX: number;
  dirY: number;
  wood: number;
  stone: number;
  cooldownUntil: number;
  stamp: number;
  stampUntil: number;
  limits: Record<ClientMessage["t"], TokenBucket>;
}

interface NodeState {
  stock: number;
  respawnAt: number;
}

export class World {
  readonly players = new Map<string, Player>();
  readonly nodes: NodeState[];
  towerCm: number;
  private events: GameEvent[] = [];

  constructor(
    readonly map: IslandMap,
    private readonly store: ContributionStore,
    private readonly clock: () => number = Date.now,
  ) {
    this.nodes = map.nodes.map((n) => ({ stock: NODE_STOCK[n.kind], respawnAt: 0 }));
    this.towerCm = store.loadTowerCm();
  }

  now(): number {
    return this.clock();
  }

  addPlayer(id: string, token: string, rawName: unknown): Player {
    const name = sanitizeName(rawName) ?? "ななし";
    const now = this.now();
    // 広場のまわりの歩ける場所に出現させる
    let x = TOWER_X;
    let y = TOWER_Y + TOWER_RADIUS + 40;
    for (let i = 0; i < 50; i++) {
      const a = Math.random() * Math.PI * 2;
      const r = TOWER_RADIUS + 30 + Math.random() * 60;
      const cx = TOWER_X + Math.cos(a) * r;
      const cy = TOWER_Y + Math.sin(a) * r;
      if (this.canStand(cx, cy)) {
        x = cx;
        y = cy;
        break;
      }
    }
    const p: Player = {
      id,
      token,
      name,
      x,
      y,
      targetX: null,
      targetY: null,
      dirX: 0,
      dirY: 0,
      wood: 0,
      stone: 0,
      cooldownUntil: 0,
      stamp: -1,
      stampUntil: 0,
      limits: {
        moveTo: new TokenBucket(10, 10, now),
        dir: new TokenBucket(20, 20, now),
        gather: new TokenBucket(2, 0.5, now),
        deliver: new TokenBucket(2, 0.5, now),
        stamp: new TokenBucket(3, 1, now),
      },
    };
    this.players.set(id, p);
    return p;
  }

  removePlayer(id: string): void {
    this.players.delete(id);
  }

  /** クライアントからのメッセージを検証して適用する。信用しない前提で全項目をチェック */
  handle(id: string, msg: unknown): void {
    const p = this.players.get(id);
    if (!p || typeof msg !== "object" || msg === null) return;
    const m = msg as Partial<ClientMessage> & Record<string, unknown>;
    const type = m.t;
    if (typeof type !== "string" || !(type in p.limits)) return;
    const now = this.now();
    if (!p.limits[type as ClientMessage["t"]].take(now)) {
      this.reject(p, "rate_limited");
      return;
    }

    switch (type) {
      case "moveTo": {
        const x = Number(m.x);
        const y = Number(m.y);
        if (!Number.isFinite(x) || !Number.isFinite(y)) return this.reject(p, "invalid");
        p.targetX = clamp(x, 0, WORLD_W);
        p.targetY = clamp(y, 0, WORLD_H);
        p.dirX = 0;
        p.dirY = 0;
        return;
      }
      case "dir": {
        const x = Number(m.x);
        const y = Number(m.y);
        if (![-1, 0, 1].includes(x) || ![-1, 0, 1].includes(y)) return this.reject(p, "invalid");
        p.dirX = x;
        p.dirY = y;
        if (x !== 0 || y !== 0) {
          p.targetX = null;
          p.targetY = null;
        }
        return;
      }
      case "gather":
        return this.gather(p, m.nodeId, now);
      case "deliver":
        return this.deliver(p);
      case "stamp": {
        if (!isValidStamp(m.id)) return this.reject(p, "invalid");
        p.stamp = m.id;
        p.stampUntil = now + STAMP_SHOW_MS;
        this.events.push({ t: "stamp", playerId: p.id, id: m.id });
        return;
      }
    }
  }

  private gather(p: Player, rawId: unknown, now: number): void {
    const def = typeof rawId === "number" ? this.map.nodes[rawId] : undefined;
    if (!def) return this.reject(p, "invalid");
    const node = this.nodes[def.id];
    if (now < p.cooldownUntil) return this.reject(p, "cooldown");
    if (Math.hypot(p.x - def.x, p.y - def.y) > GATHER_RANGE) return this.reject(p, "too_far");
    if (node.stock <= 0) return this.reject(p, "depleted");
    if (p.wood + p.stone >= INVENTORY_MAX) return this.reject(p, "inventory_full");

    node.stock -= 1;
    if (node.stock === 0) node.respawnAt = now + NODE_RESPAWN_MS;
    const item = ITEM_OF[def.kind];
    p[item] += 1;
    p.cooldownUntil = now + GATHER_COOLDOWN_MS;
    this.events.push({ t: "gathered", playerId: p.id, nodeId: def.id, kind: def.kind, item });
  }

  private deliver(p: Player): void {
    if (Math.hypot(p.x - TOWER_X, p.y - TOWER_Y) > DELIVER_RANGE) return this.reject(p, "too_far");
    if (p.wood + p.stone === 0) return this.reject(p, "empty_inventory");
    const addedCm = p.wood * CM_PER_ITEM.wood + p.stone * CM_PER_ITEM.stone;
    const before = reachedCount(this.towerCm);
    // ランキングの日付は実時刻で決める（シミュレーション時計は単調増加用）
    this.store.recordDelivery(p.token, p.name, addedCm, dayJst(Date.now()));
    this.towerCm += addedCm;
    this.events.push({
      t: "delivered",
      playerId: p.id,
      name: p.name,
      wood: p.wood,
      stone: p.stone,
      addedCm,
      towerCm: this.towerCm,
    });
    p.wood = 0;
    p.stone = 0;
    const after = reachedCount(this.towerCm);
    for (let i = before; i < after; i++) this.events.push({ t: "milestone", index: i });
  }

  private reject(p: Player, reason: RejectReason): void {
    this.events.push({ t: "reject", playerId: p.id, reason });
  }

  /** 1ティック進める */
  tick(dtMs: number): void {
    const now = this.now();
    const dt = Math.min(dtMs, 250) / 1000;

    for (const p of this.players.values()) {
      const maxStep = PLAYER_SPEED * dt;
      let mx = 0;
      let my = 0;
      if (p.dirX !== 0 || p.dirY !== 0) {
        const len = Math.hypot(p.dirX, p.dirY);
        mx = (p.dirX / len) * maxStep;
        my = (p.dirY / len) * maxStep;
      } else if (p.targetX !== null && p.targetY !== null) {
        const dx = p.targetX - p.x;
        const dy = p.targetY - p.y;
        const dist = Math.hypot(dx, dy);
        if (dist < 1) {
          p.targetX = p.targetY = null;
        } else {
          const step = Math.min(dist, maxStep);
          mx = (dx / dist) * step;
          my = (dy / dist) * step;
        }
      }
      if (mx === 0 && my === 0) continue;
      const ox = p.x;
      const oy = p.y;
      this.moveWithCollision(p, mx, my);
      // 目的地に向かって進めなくなったら止まる（水辺でガタガタしないように）
      if (p.targetX !== null && Math.hypot(p.x - ox, p.y - oy) < 0.05) {
        p.targetX = p.targetY = null;
      }
    }

    this.nodes.forEach((n, i) => {
      if (n.stock === 0 && now >= n.respawnAt) n.stock = NODE_STOCK[this.map.nodes[i].kind];
    });
  }

  private moveWithCollision(p: Player, dx: number, dy: number): void {
    // 軸ごとに判定して壁沿いに滑らせる
    if (this.canStand(p.x + dx, p.y)) p.x += dx;
    if (this.canStand(p.x, p.y + dy)) p.y += dy;
    // タワーからは押し出す
    const tx = p.x - TOWER_X;
    const ty = p.y - TOWER_Y;
    const d = Math.hypot(tx, ty);
    const minD = TOWER_RADIUS + PLAYER_RADIUS;
    if (d < minD) {
      const nx = d > 0 ? tx / d : 0;
      const ny = d > 0 ? ty / d : 1;
      p.x = TOWER_X + nx * minD;
      p.y = TOWER_Y + ny * minD;
    }
  }

  canStand(x: number, y: number): boolean {
    const r = PLAYER_RADIUS;
    return (
      this.walkable(x - r, y - r) &&
      this.walkable(x + r, y - r) &&
      this.walkable(x - r, y + r) &&
      this.walkable(x + r, y + r)
    );
  }

  walkable(x: number, y: number): boolean {
    const { tile, zone } = tileAt(this.map, x, y);
    if (tile === Tile.Water) return false;
    return isZoneUnlocked(zone, this.towerCm);
  }

  snapshot(): Snapshot {
    const players: PlayerSnap[] = [];
    for (const p of this.players.values()) {
      players.push({
        id: p.id,
        name: p.name,
        x: Math.round(p.x * 10) / 10,
        y: Math.round(p.y * 10) / 10,
        wood: p.wood,
        stone: p.stone,
        cooldownUntil: p.cooldownUntil,
        stamp: p.stampUntil > this.now() ? p.stamp : -1,
        stampUntil: p.stampUntil,
      });
    }
    return {
      t: this.now(),
      towerCm: this.towerCm,
      players,
      nodeStock: this.nodes.map((n) => n.stock),
    };
  }

  drainEvents(): GameEvent[] {
    const e = this.events;
    this.events = [];
    return e;
  }

  /** 開発用：タワーを直接伸ばす（節目の演出確認用）。永続化はしない */
  devAddTower(cm: number): void {
    const before = reachedCount(this.towerCm);
    this.towerCm += cm;
    const after = reachedCount(this.towerCm);
    for (let i = before; i < after; i++) this.events.push({ t: "milestone", index: i });
  }
}

function clamp(v: number, lo: number, hi: number): number {
  return v < lo ? lo : v > hi ? hi : v;
}
