import Matter from 'matter-js';
import { createBlockBody, type BlockSpec } from './shapes';

const { Engine, Composite, Bodies, Body } = Matter;

/** ワールドの横幅（単位）。画面幅に合わせて拡大縮小して描く */
export const WORLD_W = 360;
/** 土台の上面の y。ワールドは y 下向き */
export const PLATFORM_TOP = 0;
export const PLATFORM_W = 200;
const PLATFORM_H = 48;
/** 最高点からブロックの下端まで、どれだけ離して落とすか */
const SPAWN_GAP = 70;
/** 1 単位 = 0.5cm として高さを cm 表示する */
export const CM_PER_UNIT = 0.5;

const STEP_MS = 1000 / 60;
const SETTLE_TIMEOUT_MS = 3500;

export type Phase = 'aim' | 'falling' | 'over' | 'done';

export interface GameEvents {
  onDrop?: () => void;
  onLand?: (impact: number) => void;
  onSettle?: (placed: number) => void;
  onCollapse?: (fallen: Matter.Body) => void;
  onComplete?: () => void;
}

export class Game {
  readonly engine = Engine.create({ positionIterations: 10, velocityIterations: 8 });
  readonly platform: Matter.Body;
  readonly placed: Matter.Body[] = [];
  readonly specs: BlockSpec[];

  phase: Phase = 'aim';
  /** 照準中のブロック（ワールドにはまだ入っていない） */
  current: Matter.Body | null = null;
  aimX = WORLD_W / 2;
  aimAngle = 0;
  /** 最後に落ち着いた時点のタワーの高さ（ワールド単位）。スコアの元 */
  settledHeight = 0;

  private acc = 0;
  private sinceDrop = 0;
  private calmFrames = 0;
  private landed = false;
  private dropped: Matter.Body | null = null;

  constructor(specs: BlockSpec[], private events: GameEvents = {}) {
    this.specs = specs;
    this.platform = Bodies.rectangle(WORLD_W / 2, PLATFORM_TOP + PLATFORM_H / 2, PLATFORM_W, PLATFORM_H, {
      isStatic: true,
      friction: 1,
      frictionStatic: 1.5,
      chamfer: { radius: 6 },
      label: 'platform',
    });
    Composite.add(this.engine.world, this.platform);

    Matter.Events.on(this.engine, 'collisionStart', (e) => {
      if (!this.dropped || this.landed) return;
      for (const pair of e.pairs) {
        const a = pair.bodyA.parent;
        const b = pair.bodyB.parent;
        if (a === this.dropped || b === this.dropped) {
          this.landed = true;
          this.events.onLand?.(this.dropped.speed);
          break;
        }
      }
    });

    this.spawn();
  }

  get index() {
    return this.placed.length;
  }
  get total() {
    return this.specs.length;
  }

  /** 現在の最高点（ワールド y。小さいほど高い） */
  topY(includeMoving = true): number {
    let top = PLATFORM_TOP;
    for (const b of this.placed) {
      if (!includeMoving && b === this.dropped) continue;
      if (b.bounds.min.y < top) top = b.bounds.min.y;
    }
    return top;
  }

  heightCm(units = this.settledHeight) {
    return Math.round(units * CM_PER_UNIT);
  }

  /** 照準中ブロックの重心の y。ブロックの下端が最高点の SPAWN_GAP 上に来る */
  spawnY() {
    const below = this.current ? this.current.bounds.max.y - this.current.position.y : 20;
    return this.topY(false) - SPAWN_GAP - below;
  }

  private spawn() {
    const spec = this.specs[this.placed.length];
    if (!spec) return;
    this.current = createBlockBody(spec);
    this.aimAngle = 0;
    this.aimX = clampAimX(this.aimX, this.current);
    this.phase = 'aim';
  }

  moveAim(dx: number) {
    if (this.phase !== 'aim' || !this.current) return;
    this.aimX = clampAimX(this.aimX + dx, this.current);
  }

  rotate() {
    if (this.phase !== 'aim' || !this.current) return;
    this.aimAngle += Math.PI / 2;
    Body.setAngle(this.current, this.aimAngle);
    this.aimX = clampAimX(this.aimX, this.current);
  }

  /** 照準中のブロックの位置・角度をプレビュー用に反映する */
  syncAim() {
    if (!this.current) return;
    Body.setAngle(this.current, this.aimAngle);
    Body.setPosition(this.current, { x: this.aimX, y: this.spawnY() });
  }

  drop() {
    if (this.phase !== 'aim' || !this.current) return;
    this.syncAim();
    const body = this.current;
    Body.setVelocity(body, { x: 0, y: 1.5 });
    Composite.add(this.engine.world, body);
    this.placed.push(body);
    this.dropped = body;
    this.current = null;
    this.landed = false;
    this.sinceDrop = 0;
    this.calmFrames = 0;
    this.phase = 'falling';
    this.events.onDrop?.();
  }

  /** dt は実時間 (ms)、timeScale はスロー演出用 */
  update(dt: number, timeScale = 1) {
    if (this.phase === 'aim') this.syncAim();
    this.acc += Math.min(dt, 100) * timeScale;
    while (this.acc >= STEP_MS) {
      this.acc -= STEP_MS;
      Engine.update(this.engine, STEP_MS);
      this.afterStep();
    }
  }

  private afterStep() {
    if (this.phase === 'done') return;

    // 土台の上面より下まで落ちたブロックがあればゲームオーバー
    if (this.phase !== 'over') {
      for (const b of this.placed) {
        if (b.bounds.min.y > PLATFORM_TOP + 4 || Math.abs(b.position.x - WORLD_W / 2) > WORLD_W) {
          this.phase = 'over';
          this.current = null;
          this.events.onCollapse?.(b);
          return;
        }
      }
    }

    if (this.phase !== 'falling') return;
    this.sinceDrop += STEP_MS;
    const calm = this.placed.every((b) => b.speed < 0.18 && Math.abs(b.angularSpeed) < 0.012);
    this.calmFrames = calm ? this.calmFrames + 1 : 0;
    const settled = this.landed && this.calmFrames >= 12;
    if (settled || (this.landed && this.sinceDrop > SETTLE_TIMEOUT_MS)) {
      this.dropped = null;
      this.settledHeight = PLATFORM_TOP - this.topY();
      this.events.onSettle?.(this.placed.length);
      if (this.placed.length >= this.specs.length) {
        this.phase = 'done';
        this.events.onComplete?.();
      } else {
        this.spawn();
      }
    }
  }
}

function clampAimX(x: number, body: Matter.Body) {
  // aimX は重心の位置なので、重心から左右の端までの距離で画面内に収める
  const left = body.position.x - body.bounds.min.x;
  const right = body.bounds.max.x - body.position.x;
  return Math.max(left + 4, Math.min(WORLD_W - right - 4, x));
}
