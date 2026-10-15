import { beforeEach, describe, expect, it } from "vitest";
import {
  DELIVER_RANGE,
  GATHER_COOLDOWN_MS,
  INVENTORY_MAX,
  NODE_RESPAWN_MS,
  NODE_STOCK,
  PLAYER_SPEED,
  TOWER_X,
  TOWER_Y,
} from "../src/core/constants";
import { generateMap, Tile, tileAt } from "../src/core/map";
import type { GameEvent, RankingEntry, SelfInfo } from "../src/core/protocol";
import { type ContributionStore, sanitizeName, World } from "../src/core/world";

class MemStore implements ContributionStore {
  tower = 0;
  totals = new Map<string, number>();
  loadTowerCm() {
    return this.tower;
  }
  recordDelivery(token: string, _name: string, cm: number): SelfInfo {
    this.tower += cm;
    this.totals.set(token, (this.totals.get(token) ?? 0) + cm);
    return this.getSelf(token);
  }
  getSelf(token: string): SelfInfo {
    const v = this.totals.get(token) ?? 0;
    return { totalCm: v, todayCm: v };
  }
  topToday(): RankingEntry[] {
    return [];
  }
}

const map = generateMap();

function setup() {
  let now = 1_000_000;
  const store = new MemStore();
  const world = new World(map, store, () => now);
  const p = world.addPlayer("a", "tok", "テスト");
  return {
    world,
    store,
    p,
    advance(ms: number) {
      for (let t = 0; t < ms; t += 50) {
        now += 50;
        world.tick(50);
      }
    },
    jump(ms: number) {
      now += ms;
    },
    events(): GameEvent[] {
      return world.drainEvents();
    },
  };
}

describe("map", () => {
  it("同じシードなら同じ島になる", () => {
    const b = generateMap();
    expect(b.nodes).toEqual(map.nodes);
    expect(Array.from(b.tiles)).toEqual(Array.from(map.tiles));
  });

  it("タワーは広場の上にあり、資源は十分にある", () => {
    expect(tileAt(map, TOWER_X, TOWER_Y).tile).toBe(Tile.Plaza);
    expect(map.nodes.filter((n) => n.zone === 0 && n.kind === "tree").length).toBeGreaterThan(20);
    expect(map.nodes.filter((n) => n.zone === 0 && n.kind === "rock").length).toBeGreaterThan(10);
  });
});

describe("movement（サーバー権威）", () => {
  let s: ReturnType<typeof setup>;
  beforeEach(() => (s = setup()));

  it("出現位置は歩ける場所", () => {
    expect(s.world.canStand(s.p.x, s.p.y)).toBe(true);
  });

  it("速度上限を超えて移動しない", () => {
    const x0 = s.p.x;
    s.world.handle("a", { t: "moveTo", x: x0 + 1000, y: s.p.y });
    s.advance(1000);
    expect(s.p.x - x0).toBeLessThanOrEqual(PLAYER_SPEED + 0.01);
  });

  it("海には入れない", () => {
    s.world.handle("a", { t: "moveTo", x: 0, y: 0 });
    s.advance(30_000);
    expect(s.world.canStand(s.p.x, s.p.y)).toBe(true);
  });

  it("未解放の橋は渡れない", () => {
    // 東の森（zone 1）方向へひたすら歩く
    s.world.handle("a", { t: "dir", x: 1, y: 0 });
    s.p.y = TOWER_Y; // 橋の高さに合わせる（テスト用に直接配置）
    s.p.x = TOWER_X + 100;
    s.advance(20_000);
    expect(tileAt(map, s.p.x, s.p.y).zone).toBe(0);
  });

  it("不正な値は無視される", () => {
    const { x, y } = s.p;
    s.world.handle("a", { t: "moveTo", x: Number.NaN, y: 1 });
    s.world.handle("a", { t: "dir", x: 5, y: 0 });
    s.world.handle("a", { t: "teleport", x: 0, y: 0 });
    s.world.handle("a", null);
    s.advance(500);
    expect(s.p.x).toBe(x);
    expect(s.p.y).toBe(y);
  });
});

describe("採取と納品", () => {
  let s: ReturnType<typeof setup>;
  beforeEach(() => (s = setup()));

  const tree = map.nodes.find((n) => n.kind === "tree" && n.zone === 0)!;
  const placeAt = (x: number, y: number) => {
    s.p.x = x;
    s.p.y = y;
  };

  it("遠くからは採取できない", () => {
    placeAt(tree.x + 200, tree.y);
    s.world.handle("a", { t: "gather", nodeId: tree.id });
    expect(s.p.wood).toBe(0);
    expect(s.events()).toContainEqual(expect.objectContaining({ t: "reject", reason: "too_far" }));
  });

  it("近くで採取でき、クールダウン中は採れない", () => {
    placeAt(tree.x + 20, tree.y);
    s.world.handle("a", { t: "gather", nodeId: tree.id });
    expect(s.p.wood).toBe(1);
    s.world.handle("a", { t: "gather", nodeId: tree.id });
    expect(s.p.wood).toBe(1);
    s.jump(GATHER_COOLDOWN_MS);
    s.world.handle("a", { t: "gather", nodeId: tree.id });
    expect(s.p.wood).toBe(2);
  });

  it("資源は枯れて、時間がたつと復活する", () => {
    placeAt(tree.x + 20, tree.y);
    for (let i = 0; i < NODE_STOCK.tree + 2; i++) {
      s.world.handle("a", { t: "gather", nodeId: tree.id });
      s.jump(GATHER_COOLDOWN_MS);
    }
    expect(s.p.wood).toBe(NODE_STOCK.tree);
    expect(s.world.nodes[tree.id].stock).toBe(0);
    s.advance(NODE_RESPAWN_MS);
    expect(s.world.nodes[tree.id].stock).toBe(NODE_STOCK.tree);
  });

  it("持ち物の上限を超えない", () => {
    s.p.wood = INVENTORY_MAX;
    placeAt(tree.x + 20, tree.y);
    s.world.handle("a", { t: "gather", nodeId: tree.id });
    expect(s.p.wood).toBe(INVENTORY_MAX);
  });

  it("タワーの近くでのみ納品でき、高さが伸びて保存される", () => {
    s.p.wood = 3;
    s.p.stone = 2;
    placeAt(TOWER_X + DELIVER_RANGE + 50, TOWER_Y);
    s.world.handle("a", { t: "deliver" });
    expect(s.world.towerCm).toBe(0);

    placeAt(TOWER_X + DELIVER_RANGE - 10, TOWER_Y);
    s.world.handle("a", { t: "deliver" });
    expect(s.world.towerCm).toBe(3 * 10 + 2 * 20);
    expect(s.store.tower).toBe(70);
    expect(s.store.getSelf("tok").totalCm).toBe(70);
    expect(s.p.wood + s.p.stone).toBe(0);
  });

  it("節目を越えるとイベントが出て、橋が渡れるようになる", () => {
    s.events();
    s.world.devAddTower(100_00);
    expect(s.events()).toContainEqual({ t: "milestone", index: 0 });
    // 東の橋の上に立てる
    expect(s.world.walkable(55 * 32 + 16, 30 * 32 - 16)).toBe(true);
  });
});

describe("レート制限", () => {
  it("採取の連打は弾かれる", () => {
    const s = setup();
    for (let i = 0; i < 10; i++) s.world.handle("a", { t: "gather", nodeId: 0 });
    const rejects = s.events().filter((e) => e.t === "reject" && e.reason === "rate_limited");
    expect(rejects.length).toBeGreaterThanOrEqual(8);
  });
});

describe("sanitizeName", () => {
  it("制御文字を除き、長さを切り詰める", () => {
    expect(sanitizeName(`  あいう\u0000${String.fromCharCode(0x202e)}えお  `)).toBe("あいうえお");
    expect(sanitizeName("1234567890abcdef")).toBe("1234567890ab");
    expect(sanitizeName("   ")).toBeNull();
    expect(sanitizeName(42)).toBeNull();
  });
});
