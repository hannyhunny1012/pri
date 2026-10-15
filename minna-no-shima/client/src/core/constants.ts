// ゲーム全体の定数。後でサーバーと共有する（Phaser / DOM に依存しないこと）。

export const TILE = 32;
export const MAP_W = 72; // タイル数
export const MAP_H = 56;
export const WORLD_W = MAP_W * TILE;
export const WORLD_H = MAP_H * TILE;

/** シミュレーションの1ティック（ms）。20Hz */
export const TICK_MS = 50;

export const PLAYER_SPEED = 170; // px/秒
export const PLAYER_RADIUS = 10;

export const GATHER_RANGE = 56; // 資源の中心からの距離(px)
export const GATHER_COOLDOWN_MS = 3000;
export const INVENTORY_MAX = 20;

export const TOWER_X = 36 * TILE;
export const TOWER_Y = 30 * TILE;
export const TOWER_RADIUS = 40; // 当たり判定
export const DELIVER_RANGE = 96; // タワー中心からの距離(px)

export type ResourceKind = "tree" | "rock";
export type ItemKind = "wood" | "stone";

export const ITEM_OF: Record<ResourceKind, ItemKind> = { tree: "wood", rock: "stone" };
/** 納品1個あたりのタワーの伸び（cm）。高さは整数cmで扱い誤差を避ける */
export const CM_PER_ITEM: Record<ItemKind, number> = { wood: 10, stone: 20 };

export const NODE_STOCK: Record<ResourceKind, number> = { tree: 5, rock: 4 };
export const NODE_RESPAWN_MS = 30_000;

export const STAMP_SHOW_MS = 3000;

export const NAME_MAX_LEN = 12;
