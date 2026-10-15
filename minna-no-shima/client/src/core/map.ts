// 島の地形と資源ポイントを決定論的に生成する。
// クライアントとサーバーが同じシードから同じ島を得るので、地形データを送る必要がない。

import {
  MAP_H,
  MAP_W,
  TILE,
  TOWER_X,
  TOWER_Y,
  type ResourceKind,
} from "./constants";

export const enum Tile {
  Water = 0,
  Sand = 1,
  Grass = 2,
  Plaza = 3,
  Bridge = 4,
}

export interface ResourceNodeDef {
  id: number;
  kind: ResourceKind;
  x: number; // px（中心）
  y: number;
  zone: number;
}

export interface IslandMap {
  tiles: Uint8Array; // MAP_W * MAP_H
  /** 各タイルが属するエリア。橋タイルは解放対象のエリアID */
  zones: Uint8Array;
  nodes: ResourceNodeDef[];
}

export const MAP_SEED = 20261015;

/** mulberry32: 小さく決定論的な乱数 */
export function rng(seed: number): () => number {
  let a = seed >>> 0;
  return () => {
    a = (a + 0x6d2b79f5) >>> 0;
    let t = a;
    t = Math.imul(t ^ (t >>> 15), t | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

interface Blob {
  zone: number;
  cx: number; // タイル座標
  cy: number;
  rx: number;
  ry: number;
  trees: number;
  rocks: number;
}

const BLOBS: Blob[] = [
  { zone: 0, cx: 36, cy: 30, rx: 20, ry: 15, trees: 40, rocks: 22 },
  { zone: 1, cx: 64, cy: 30, rx: 6, ry: 9, trees: 18, rocks: 3 }, // 東の森
  { zone: 2, cx: 36, cy: 6, rx: 11, ry: 4.5, trees: 3, rocks: 18 }, // 北の岩山
  { zone: 3, cx: 8, cy: 30, rx: 6, ry: 9, trees: 10, rocks: 10 }, // 西の小島
];

/** 本島から各エリアへの橋（タイル座標の始点→終点、幅2） */
const BRIDGES: { zone: number; from: [number, number]; to: [number, number] }[] = [
  { zone: 1, from: [50, 30], to: [62, 30] },
  { zone: 2, from: [36, 20], to: [36, 8] },
  { zone: 3, from: [22, 30], to: [10, 30] },
];

export function idx(tx: number, ty: number): number {
  return ty * MAP_W + tx;
}

export function generateMap(seed = MAP_SEED): IslandMap {
  const rand = rng(seed);
  const tiles = new Uint8Array(MAP_W * MAP_H);
  const zones = new Uint8Array(MAP_W * MAP_H);

  for (const b of BLOBS) {
    // 角度ごとに半径をゆらして自然な海岸線にする
    const phases = [rand() * Math.PI * 2, rand() * Math.PI * 2, rand() * Math.PI * 2];
    const wobble = (a: number) =>
      1 +
      0.08 * Math.sin(3 * a + phases[0]) +
      0.06 * Math.sin(5 * a + phases[1]) +
      0.04 * Math.sin(9 * a + phases[2]);
    for (let ty = 0; ty < MAP_H; ty++) {
      for (let tx = 0; tx < MAP_W; tx++) {
        const dx = (tx + 0.5 - b.cx) / b.rx;
        const dy = (ty + 0.5 - b.cy) / b.ry;
        const d = Math.hypot(dx, dy) / wobble(Math.atan2(dy, dx));
        if (d > 1) continue;
        const i = idx(tx, ty);
        const sandWidth = 1.6 / Math.min(b.rx, b.ry);
        tiles[i] = d > 1 - sandWidth ? Tile.Sand : Tile.Grass;
        zones[i] = b.zone;
      }
    }
  }

  // タワー周りの広場
  const pcx = TOWER_X / TILE;
  const pcy = TOWER_Y / TILE;
  for (let ty = 0; ty < MAP_H; ty++) {
    for (let tx = 0; tx < MAP_W; tx++) {
      if (Math.hypot(tx + 0.5 - pcx, ty + 0.5 - pcy) < 4.2) tiles[idx(tx, ty)] = Tile.Plaza;
    }
  }

  // 橋：水タイルだけを橋にする（陸地はそのまま）
  for (const br of BRIDGES) {
    const [x0, y0] = br.from;
    const [x1, y1] = br.to;
    const steps = Math.max(Math.abs(x1 - x0), Math.abs(y1 - y0));
    for (let s = 0; s <= steps; s++) {
      const x = Math.round(x0 + ((x1 - x0) * s) / steps);
      const y = Math.round(y0 + ((y1 - y0) * s) / steps);
      const horizontal = y0 === y1;
      for (const [tx, ty] of horizontal ? [[x, y - 1], [x, y]] : [[x - 1, y], [x, y]]) {
        const i = idx(tx, ty);
        if (tiles[i] === Tile.Water) {
          tiles[i] = Tile.Bridge;
          zones[i] = br.zone;
        }
      }
    }
  }

  // 資源ポイント
  const nodes: ResourceNodeDef[] = [];
  const minDist = 2.6 * TILE;
  for (const b of BLOBS) {
    const want: [ResourceKind, number][] = [
      ["tree", b.trees],
      ["rock", b.rocks],
    ];
    for (const [kind, count] of want) {
      let placed = 0;
      for (let attempt = 0; attempt < count * 200 && placed < count; attempt++) {
        const tx = Math.floor(b.cx - b.rx + rand() * b.rx * 2);
        const ty = Math.floor(b.cy - b.ry + rand() * b.ry * 2);
        if (tx < 1 || ty < 1 || tx >= MAP_W - 1 || ty >= MAP_H - 1) continue;
        const i = idx(tx, ty);
        if (tiles[i] !== Tile.Grass || zones[i] !== b.zone) continue;
        const x = (tx + 0.5) * TILE;
        const y = (ty + 0.5) * TILE;
        if (Math.hypot(x - TOWER_X, y - TOWER_Y) < 7 * TILE) continue;
        if (nodes.some((n) => Math.hypot(n.x - x, n.y - y) < minDist)) continue;
        nodes.push({ id: nodes.length, kind, x, y, zone: b.zone });
        placed++;
      }
    }
  }

  return { tiles, zones, nodes };
}

export function tileAt(map: IslandMap, px: number, py: number): { tile: Tile; zone: number } {
  const tx = Math.floor(px / TILE);
  const ty = Math.floor(py / TILE);
  if (tx < 0 || ty < 0 || tx >= MAP_W || ty >= MAP_H) return { tile: Tile.Water, zone: 0 };
  const i = idx(tx, ty);
  return { tile: map.tiles[i] as Tile, zone: map.zones[i] };
}
