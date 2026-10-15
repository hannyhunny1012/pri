import Phaser from "phaser";
import {
  DELIVER_RANGE,
  GATHER_COOLDOWN_MS,
  GATHER_RANGE,
  INVENTORY_MAX,
  MAP_H,
  MAP_W,
  PLAYER_RADIUS,
  TICK_MS,
  TILE,
  TOWER_RADIUS,
  TOWER_X,
  TOWER_Y,
  WORLD_H,
  WORLD_W,
} from "../core/constants";
import { generateMap, idx, type IslandMap, Tile } from "../core/map";
import { formatMeters, isZoneUnlocked, MILESTONES, reachedCount } from "../core/milestones";
import type { GameEvent, PlayerSnap, RejectReason, Snapshot } from "../core/protocol";
import { STAMPS } from "../core/stamps";
import type { Backend } from "../net/Backend";
import { PositionBuffer, ServerClock } from "../net/Interpolator";
import type { Hud } from "../ui/Hud";
import { makeTextures } from "./textures";

/** 他プレイヤーはこの分だけ過去を表示し、スナップショット間を補間する */
const INTERP_DELAY_MS = 100;
const WATER = 0x5bb8e0;

interface PlayerView {
  container: Phaser.GameObjects.Container;
  body: Phaser.GameObjects.Graphics;
  stamp: Phaser.GameObjects.Text;
  buffer: PositionBuffer;
  shownStamp: number;
}

type Pending = { kind: "gather"; nodeId: number } | { kind: "deliver" } | null;

export interface SceneDeps {
  backend: Backend;
  selfId: string;
  hud: Hud;
  /** 開発用フック（?dev のときだけ渡される） */
  devAddTower?: (cm: number) => void;
}

export class IslandScene extends Phaser.Scene {
  private deps!: SceneDeps;
  private map!: IslandMap;
  private clock = new ServerClock();
  private players = new Map<string, PlayerView>();
  private nodeSprites: Phaser.GameObjects.Image[] = [];
  private nodeStock: number[] = [];
  private bridgeLayer!: Phaser.GameObjects.Graphics;
  private bridgeLabels: Phaser.GameObjects.Text[] = [];
  private towerGfx!: Phaser.GameObjects.Graphics;
  private towerLabel!: Phaser.GameObjects.Text;
  private rangeGfx!: Phaser.GameObjects.Graphics;
  private cooldownGfx!: Phaser.GameObjects.Graphics;
  private tapMarker!: Phaser.GameObjects.Arc;
  private towerCm = -1;
  private self: PlayerSnap | null = null;
  private pending: Pending = null;
  private lastActionSent = 0;
  private keys!: Record<"up" | "down" | "left" | "right" | "w" | "a" | "s" | "d" | "space", Phaser.Input.Keyboard.Key>;
  private sentDir = { x: 0, y: 0 };
  private following = false;
  private fireworksTimer = 0;

  constructor() {
    super("island");
  }

  init(deps: SceneDeps): void {
    this.deps = deps;
  }

  create(): void {
    makeTextures(this);
    this.map = generateMap();
    this.cameras.main.setBackgroundColor(WATER);
    this.cameras.main.setBounds(-TILE * 4, -TILE * 4, WORLD_W + TILE * 8, WORLD_H + TILE * 8);

    this.drawTerrain();
    this.bridgeLayer = this.add.graphics().setDepth(1);
    this.rangeGfx = this.add.graphics().setDepth(2);
    this.createNodes();
    this.towerGfx = this.add.graphics().setDepth(TOWER_Y + 1);
    this.towerLabel = this.add
      .text(TOWER_X, TOWER_Y, "", {
        fontFamily: "system-ui, sans-serif",
        fontSize: "14px",
        fontStyle: "bold",
        color: "#ffffff",
        stroke: "#2b3a42",
        strokeThickness: 4,
      })
      .setOrigin(0.5, 1)
      .setDepth(1e6);
    this.cooldownGfx = this.add.graphics().setDepth(1e6);
    this.tapMarker = this.add.circle(0, 0, 10).setStrokeStyle(3, 0xffffff, 0.9).setDepth(3).setVisible(false);

    this.setupInput();

    const { backend } = this.deps;
    backend.onSnapshot((s) => this.onSnapshot(s));
    backend.onEvent((e) => this.onEvent(e));

    this.scale.on("resize", () => this.fitZoom());
    this.fitZoom();
  }

  // ---------------- 描画 ----------------

  private drawTerrain(): void {
    const g = this.add.graphics().setDepth(0);
    const { tiles } = this.map;
    const landAt = (tx: number, ty: number) =>
      tx >= 0 && ty >= 0 && tx < MAP_W && ty < MAP_H && tiles[idx(tx, ty)] !== Tile.Water && tiles[idx(tx, ty)] !== Tile.Bridge;

    for (let ty = 0; ty < MAP_H; ty++) {
      for (let tx = 0; tx < MAP_W; tx++) {
        const t = tiles[idx(tx, ty)];
        const x = tx * TILE;
        const y = ty * TILE;
        const odd = (tx + ty) % 2 === 0;
        if (t === Tile.Water || t === Tile.Bridge) {
          // 陸に近い水は浅瀬の色にする
          let near = false;
          for (let dy = -1; dy <= 1 && !near; dy++) for (let dx = -1; dx <= 1; dx++) if (landAt(tx + dx, ty + dy)) near = true;
          if (near) g.fillStyle(0x86d3ee).fillRect(x, y, TILE, TILE);
          continue;
        }
        if (t === Tile.Sand) g.fillStyle(odd ? 0xf3e2a6 : 0xeedb9b);
        else if (t === Tile.Plaza) g.fillStyle(odd ? 0xe9dcc0 : 0xe2d4b6);
        else g.fillStyle(odd ? 0x92d36c : 0x8acb65);
        g.fillRect(x, y, TILE, TILE);
      }
    }
    // 草地に小さな花を散らす（見た目だけ）
    const rand = new Phaser.Math.RandomDataGenerator(["flowers"]);
    for (let i = 0; i < 260; i++) {
      const tx = rand.between(0, MAP_W - 1);
      const ty = rand.between(0, MAP_H - 1);
      if (tiles[idx(tx, ty)] !== Tile.Grass) continue;
      g.fillStyle(rand.pick([0xffffff, 0xffe066, 0xff9ecb, 0xb39dff]), 0.9);
      g.fillCircle(tx * TILE + rand.between(4, 28), ty * TILE + rand.between(4, 28), 2);
    }
    // 広場の石畳の模様
    g.lineStyle(2, 0xcdbd99, 0.8);
    g.strokeCircle(TOWER_X, TOWER_Y, 3.2 * TILE);
    g.strokeCircle(TOWER_X, TOWER_Y, DELIVER_RANGE);
  }

  /** 橋と未解放エリアの霧。タワーの高さが節目を越えたら描き直す */
  private drawBridges(): void {
    const g = this.bridgeLayer;
    g.clear();
    this.bridgeLabels.forEach((t) => t.destroy());
    this.bridgeLabels = [];
    const { tiles, zones } = this.map;
    const labelPos = new Map<number, { x: number; y: number; n: number }>();

    for (let ty = 0; ty < MAP_H; ty++) {
      for (let tx = 0; tx < MAP_W; tx++) {
        const i = idx(tx, ty);
        const zone = zones[i];
        if (zone === 0) continue;
        const open = isZoneUnlocked(zone, this.towerCm);
        const x = tx * TILE;
        const y = ty * TILE;
        if (tiles[i] === Tile.Bridge) {
          if (open) {
            g.fillStyle(0xb07a45).fillRect(x, y, TILE, TILE);
            g.lineStyle(2, 0x8a5a2e).strokeRect(x + 1, y + 1, TILE - 2, TILE - 2);
          } else {
            g.fillStyle(0xb07a45, 0.25).fillRect(x + 6, y + 6, TILE - 12, TILE - 12);
          }
          const p = labelPos.get(zone) ?? { x: 0, y: 0, n: 0 };
          p.x += x + TILE / 2;
          p.y += y + TILE / 2;
          p.n++;
          labelPos.set(zone, p);
        } else if (tiles[i] !== Tile.Water && !open) {
          g.fillStyle(0xffffff, 0.45).fillRect(x, y, TILE, TILE); // 霧
        }
      }
    }
    for (const [zone, p] of labelPos) {
      if (isZoneUnlocked(zone, this.towerCm)) continue;
      const m = MILESTONES.find((mm) => mm.zone === zone)!;
      this.bridgeLabels.push(
        this.add
          .text(p.x / p.n, p.y / p.n, `🔒 ${formatMeters(m.heightCm)}m で開通`, {
            fontFamily: "system-ui, sans-serif",
            fontSize: "13px",
            color: "#ffffff",
            backgroundColor: "rgba(30,40,50,0.65)",
            padding: { x: 6, y: 3 },
          })
          .setOrigin(0.5)
          .setDepth(5),
      );
    }
  }

  private createNodes(): void {
    for (const n of this.map.nodes) {
      const img = this.add
        .image(n.x, n.y, n.kind === "tree" ? "tree" : "rock")
        .setOrigin(0.5, n.kind === "tree" ? 0.85 : 0.8)
        .setDepth(n.y);
      this.nodeSprites.push(img);
      this.nodeStock.push(-1);
    }
  }

  private updateNodes(stock: number[]): void {
    stock.forEach((s, i) => {
      if (s === this.nodeStock[i]) return;
      const prev = this.nodeStock[i];
      this.nodeStock[i] = s;
      const def = this.map.nodes[i];
      const img = this.nodeSprites[i];
      if (s === 0) img.setTexture(def.kind === "tree" ? "stump" : "rubble");
      else if (prev <= 0) {
        img.setTexture(def.kind === "tree" ? "tree" : "rock");
        if (prev === 0) this.tweens.add({ targets: img, scale: { from: 0.4, to: 1 }, duration: 400, ease: "Back.Out" });
      }
    });
  }

  /** タワーの見た目の高さ（px）。高くなるほど伸びが緩やかになる */
  private towerPx(cm: number): number {
    return 22 + 48 * Math.log10(1 + cm / 100);
  }

  private drawTower(): void {
    const g = this.towerGfx;
    g.clear();
    const h = this.towerPx(this.towerCm);
    const baseW = 70;
    const topW = 40;
    const reached = reachedCount(this.towerCm);

    if (reached >= 5) {
      // 虹のアーチ
      const colors = [0xff6b6b, 0xffa94d, 0xffe066, 0x69db7c, 0x4dabf7, 0x9775fa];
      colors.forEach((c, i) => {
        g.lineStyle(6, c, 0.55);
        g.beginPath();
        g.arc(TOWER_X, TOWER_Y + 10, 150 - i * 6, Math.PI, 0);
        g.strokePath();
      });
    }

    g.fillStyle(0x000000, 0.2).fillEllipse(TOWER_X, TOWER_Y + 4, baseW + 24, 26);
    g.fillStyle(0x9a8f7a).fillEllipse(TOWER_X, TOWER_Y, baseW + 12, 22);
    const blockH = 10;
    const n = Math.max(1, Math.floor(h / blockH));
    for (let i = 0; i < n; i++) {
      const k = i / Math.max(1, n - 1);
      const w = baseW + (topW - baseW) * k;
      const y = TOWER_Y - (i + 1) * blockH;
      const stone = i % 3 === 2;
      g.fillStyle(stone ? 0xa7adb3 : i % 2 ? 0xc68a4e : 0xb67a40);
      g.fillRect(TOWER_X - w / 2, y, w, blockH);
      g.fillStyle(0x000000, 0.12).fillRect(TOWER_X + w / 2 - 8, y, 8, blockH);
      g.fillStyle(0xffffff, 0.15).fillRect(TOWER_X - w / 2, y, w, 2);
    }
    const topY = TOWER_Y - n * blockH;
    // 旗
    g.lineStyle(3, 0x5a4632).lineBetween(TOWER_X, topY, TOWER_X, topY - 30);
    g.fillStyle(0xff6b4a).fillTriangle(TOWER_X, topY - 30, TOWER_X + 22, topY - 24, TOWER_X, topY - 18);
    this.towerLabel.setPosition(TOWER_X, topY - 34).setText(`${formatMeters(this.towerCm)}m`);
  }

  private colorFor(name: string): number {
    let h = 0;
    for (const ch of name) h = (h * 31 + ch.codePointAt(0)!) >>> 0;
    return Phaser.Display.Color.HSVToRGB((h % 360) / 360, 0.55, 0.95).color;
  }

  private createPlayer(p: PlayerSnap): PlayerView {
    const isSelf = p.id === this.deps.selfId;
    const body = this.add.graphics();
    const color = this.colorFor(p.name);
    body.fillStyle(0x000000, 0.2).fillEllipse(0, PLAYER_RADIUS, 22, 8);
    body.fillStyle(color).fillCircle(0, 0, PLAYER_RADIUS + 2);
    body.lineStyle(2, isSelf ? 0xffffff : 0x2b3a42, isSelf ? 1 : 0.4).strokeCircle(0, 0, PLAYER_RADIUS + 2);
    body.fillStyle(0x2b3a42).fillCircle(-4, -2, 2).fillCircle(4, -2, 2);
    const name = this.add
      .text(0, -PLAYER_RADIUS - 6, p.name, {
        fontFamily: "system-ui, sans-serif",
        fontSize: "12px",
        color: isSelf ? "#fff4c2" : "#ffffff",
        stroke: "#2b3a42",
        strokeThickness: 3,
      })
      .setOrigin(0.5, 1);
    const stamp = this.add.text(0, -PLAYER_RADIUS - 22, "", { fontSize: "28px" }).setOrigin(0.5, 1).setVisible(false);
    const container = this.add.container(p.x, p.y, [body, name, stamp]);
    const view: PlayerView = { container, body, stamp, buffer: new PositionBuffer(), shownStamp: -1 };
    this.players.set(p.id, view);
    if (isSelf && !this.following) {
      this.cameras.main.startFollow(container, true, 0.15, 0.15);
      this.following = true;
    }
    return view;
  }

  private fitZoom(): void {
    const short = Math.min(this.scale.width, this.scale.height);
    this.cameras.main.setZoom(short < 500 ? 0.85 : 1);
  }

  // ---------------- 通信 ----------------

  private onSnapshot(s: Snapshot): void {
    this.clock.observe(s.t, performance.now());
    if (s.towerCm !== this.towerCm) {
      const firstOrUnlock = this.towerCm < 0 || reachedCount(s.towerCm) !== reachedCount(this.towerCm);
      this.towerCm = s.towerCm;
      this.drawTower();
      if (firstOrUnlock) this.drawBridges();
      this.deps.hud.setTower(s.towerCm);
    }
    this.updateNodes(s.nodeStock);

    const seen = new Set<string>();
    for (const p of s.players) {
      seen.add(p.id);
      const view = this.players.get(p.id) ?? this.createPlayer(p);
      view.buffer.push(s.t, p.x, p.y);
      if (p.stamp !== view.shownStamp) {
        view.shownStamp = p.stamp;
        if (p.stamp >= 0) {
          view.stamp.setText(STAMPS[p.stamp]).setVisible(true);
          this.tweens.add({ targets: view.stamp, scale: { from: 0.3, to: 1 }, duration: 250, ease: "Back.Out" });
        } else view.stamp.setVisible(false);
      }
      if (p.id === this.deps.selfId) {
        this.self = p;
        this.deps.hud.setInventory(p.wood, p.stone);
      }
    }
    for (const [id, view] of this.players) {
      if (!seen.has(id)) {
        view.container.destroy();
        this.players.delete(id);
      }
    }
  }

  private onEvent(e: GameEvent): void {
    const { hud, selfId } = this.deps;
    switch (e.t) {
      case "gathered": {
        const n = this.map.nodes[e.nodeId];
        this.floatText(n.x, n.y - 30, e.item === "wood" ? "+1 🪵" : "+1 🪨");
        this.tweens.add({ targets: this.nodeSprites[e.nodeId], angle: { from: -6, to: 6 }, duration: 60, yoyo: true, repeat: 1, onComplete: (_t, [img]) => img.setAngle(0) });
        return;
      }
      case "delivered":
        this.floatText(TOWER_X, TOWER_Y - this.towerPx(this.towerCm) - 50, `+${formatMeters(e.addedCm)}m`, "#ffe066");
        if (e.playerId === selfId) hud.toast(`納品！ タワーが ${formatMeters(e.addedCm)}m 伸びた`);
        return;
      case "milestone": {
        const m = MILESTONES[e.index];
        hud.toast(`🎉 ${formatMeters(m.heightCm)}m 達成！「${m.title}」が解放されました`, true);
        this.celebrate();
        return;
      }
      case "reject":
        if (e.playerId === selfId) this.onReject(e.reason);
        return;
      case "stamp":
        return; // 表示はスナップショットの stamp で行う
    }
  }

  private onReject(reason: RejectReason): void {
    const msg: Partial<Record<RejectReason, string>> = {
      inventory_full: "持ち物がいっぱい！タワーに納品しよう",
      depleted: "もう採れない…しばらく待とう",
      empty_inventory: "まず木や石を集めよう",
      rate_limited: "ちょっと待ってね",
    };
    const text = msg[reason];
    if (text) this.deps.hud.toast(text);
    if (reason !== "cooldown" && reason !== "rate_limited") this.pending = null;
  }

  private floatText(x: number, y: number, text: string, color = "#ffffff"): void {
    const t = this.add
      .text(x, y, text, { fontFamily: "system-ui, sans-serif", fontSize: "16px", fontStyle: "bold", color, stroke: "#2b3a42", strokeThickness: 4 })
      .setOrigin(0.5)
      .setDepth(1e6 + 1);
    this.tweens.add({ targets: t, y: y - 36, alpha: 0, duration: 1100, ease: "Cubic.Out", onComplete: () => t.destroy() });
  }

  private celebrate(): void {
    for (let i = 0; i < 6; i++) {
      this.time.delayedCall(i * 250, () => this.firework());
    }
    this.cameras.main.flash(300, 255, 240, 200);
  }

  private firework(): void {
    const x = TOWER_X + Phaser.Math.Between(-220, 220);
    const y = TOWER_Y - Phaser.Math.Between(120, 260);
    const tint = Phaser.Utils.Array.GetRandom([0xff6b6b, 0xffd43b, 0x69db7c, 0x4dabf7, 0xda77f2, 0xffffff]);
    const em = this.add
      .particles(x, y, "spark", {
        speed: { min: 60, max: 200 },
        lifespan: 900,
        scale: { start: 1, end: 0 },
        alpha: { start: 1, end: 0 },
        gravityY: 120,
        tint,
        emitting: false,
      })
      .setDepth(1e6 + 2);
    em.explode(40);
    this.time.delayedCall(1200, () => em.destroy());
  }

  // ---------------- 入力 ----------------

  private setupInput(): void {
    const kb = this.input.keyboard!;
    const K = Phaser.Input.Keyboard.KeyCodes;
    this.keys = kb.addKeys({
      up: K.UP,
      down: K.DOWN,
      left: K.LEFT,
      right: K.RIGHT,
      w: K.W,
      a: K.A,
      s: K.S,
      d: K.D,
      space: K.SPACE,
    }) as typeof this.keys;
    this.keys.space.on("down", () => this.actionNearby());

    if (this.deps.devAddTower) {
      kb.on("keydown-T", () => this.deps.devAddTower!(50_00));
    }

    this.input.on("pointerdown", (ptr: Phaser.Input.Pointer) => this.onTap(ptr.worldX, ptr.worldY));
  }

  private onTap(wx: number, wy: number): void {
    const { backend } = this.deps;
    // 資源ポイント（絵は根元より上に描かれているので少し上を中心に判定）
    let best = -1;
    let bestD = 30;
    for (const n of this.map.nodes) {
      const d = Math.hypot(wx - n.x, wy - (n.y - 12));
      if (d < bestD) {
        bestD = d;
        best = n.id;
      }
    }
    if (best >= 0) {
      this.pending = { kind: "gather", nodeId: best };
      const n = this.map.nodes[best];
      this.approach(n.x, n.y, GATHER_RANGE - 16);
      return;
    }
    // タワー
    const h = this.towerPx(this.towerCm);
    if (Math.abs(wx - TOWER_X) < 50 && wy < TOWER_Y + 20 && wy > TOWER_Y - h - 40) {
      this.pending = { kind: "deliver" };
      this.approach(TOWER_X, TOWER_Y, TOWER_RADIUS + PLAYER_RADIUS + 8);
      return;
    }
    this.pending = null;
    backend.send({ t: "moveTo", x: wx, y: wy });
    this.tapMarker.setPosition(wx, wy).setVisible(true).setScale(1).setAlpha(1);
    this.tweens.add({ targets: this.tapMarker, scale: 0.3, alpha: 0, duration: 500, onComplete: () => this.tapMarker.setVisible(false) });
  }

  /** 目標地点の手前 dist の位置まで歩く（すでに近ければ動かない） */
  private approach(tx: number, ty: number, dist: number): void {
    if (!this.self) return;
    const dx = this.self.x - tx;
    const dy = this.self.y - ty;
    const d = Math.hypot(dx, dy);
    if (d <= dist + 2) return;
    this.deps.backend.send({ t: "moveTo", x: tx + (dx / d) * dist, y: ty + (dy / d) * dist });
  }

  /** スペースキー：近くの資源を採取、またはタワーに納品 */
  private actionNearby(): void {
    if (!this.self) return;
    const { x, y } = this.self;
    if (Math.hypot(x - TOWER_X, y - TOWER_Y) <= DELIVER_RANGE && this.self.wood + this.self.stone > 0) {
      this.pending = { kind: "deliver" };
      return;
    }
    let best = -1;
    let bestD = GATHER_RANGE;
    this.map.nodes.forEach((n) => {
      const d = Math.hypot(x - n.x, y - n.y);
      if (d <= bestD && this.nodeStock[n.id] > 0) {
        bestD = d;
        best = n.id;
      }
    });
    if (best >= 0) this.pending = { kind: "gather", nodeId: best };
  }

  private readKeyboard(): void {
    const k = this.keys;
    const x = (k.right.isDown || k.d.isDown ? 1 : 0) - (k.left.isDown || k.a.isDown ? 1 : 0);
    const y = (k.down.isDown || k.s.isDown ? 1 : 0) - (k.up.isDown || k.w.isDown ? 1 : 0);
    if (x !== this.sentDir.x || y !== this.sentDir.y) {
      this.sentDir = { x, y };
      this.deps.backend.send({ t: "dir", x, y });
      if (x !== 0 || y !== 0) this.pending = null;
    }
  }

  /** 採取・納品の予約を、サーバー上の位置が範囲内に入ったら送る */
  private processPending(serverNow: number): void {
    const p = this.self;
    if (!p || !this.pending) return;
    const now = performance.now();
    if (now - this.lastActionSent < 300) return; // 返事を待つ間は送らない
    const { backend, hud } = this.deps;

    if (this.pending.kind === "gather") {
      const n = this.map.nodes[this.pending.nodeId];
      if (this.nodeStock[n.id] === 0) {
        hud.toast("もう採れない…しばらく待とう");
        this.pending = null;
        return;
      }
      if (p.wood + p.stone >= INVENTORY_MAX) {
        hud.toast("持ち物がいっぱい！タワーに納品しよう");
        this.pending = null;
        return;
      }
      if (Math.hypot(p.x - n.x, p.y - n.y) > GATHER_RANGE - 2) return;
      if (serverNow < p.cooldownUntil) return;
      backend.send({ t: "gather", nodeId: n.id });
      this.lastActionSent = now;
      // 予約は残し、クールダウン明けに自動で続けて採取する
    } else {
      if (Math.hypot(p.x - TOWER_X, p.y - TOWER_Y) > DELIVER_RANGE - 2) return;
      if (p.wood + p.stone === 0) {
        hud.toast("まず木や石を集めよう");
      } else {
        backend.send({ t: "deliver" });
        this.lastActionSent = now;
      }
      this.pending = null;
    }
  }

  // ---------------- 毎フレーム ----------------

  update(_time: number, delta: number): void {
    const localNow = performance.now();
    const serverNow = this.clock.now(localNow);

    this.readKeyboard();
    this.processPending(serverNow);

    for (const [id, view] of this.players) {
      // 自分はティック1つ分だけ遅らせる（ローカル実行のため。ネットワーク版では予測に置き換える）
      const delay = id === this.deps.selfId ? TICK_MS : INTERP_DELAY_MS;
      const pos = view.buffer.sample(serverNow - delay);
      if (pos) view.container.setPosition(pos.x, pos.y).setDepth(pos.y);
    }

    this.drawSelfIndicators(serverNow);

    if (reachedCount(this.towerCm) >= 4) {
      // 2000m 以降は時々花火が上がる
      this.fireworksTimer -= delta;
      if (this.fireworksTimer <= 0) {
        this.firework();
        this.fireworksTimer = Phaser.Math.Between(1500, 4000);
      }
    }
  }

  private drawSelfIndicators(serverNow: number): void {
    const p = this.self;
    const view = p && this.players.get(p.id);
    this.cooldownGfx.clear();
    this.rangeGfx.clear();
    if (!p || !view) return;
    const { x, y } = view.container;

    if (serverNow < p.cooldownUntil) {
      const k = Phaser.Math.Clamp((p.cooldownUntil - serverNow) / GATHER_COOLDOWN_MS, 0, 1);
      this.cooldownGfx.lineStyle(4, 0xffffff, 0.9);
      this.cooldownGfx.beginPath();
      this.cooldownGfx.arc(x, y, PLAYER_RADIUS + 8, -Math.PI / 2, -Math.PI / 2 + Math.PI * 2 * k);
      this.cooldownGfx.strokePath();
    }

    // 持ち物があるときはタワーの納品範囲を強調
    if (p.wood + p.stone > 0) {
      const pulse = 0.35 + 0.25 * Math.sin(serverNow / 250);
      this.rangeGfx.lineStyle(4, 0xffe066, pulse).strokeCircle(TOWER_X, TOWER_Y, DELIVER_RANGE);
    }
    // 採取予約中の資源を示す
    if (this.pending?.kind === "gather") {
      const n = this.map.nodes[this.pending.nodeId];
      this.rangeGfx.lineStyle(2, 0xffffff, 0.8).strokeEllipse(n.x, n.y, 44, 18);
    }
  }
}
