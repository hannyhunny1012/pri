import type Matter from 'matter-js';
import { COLORS, blockMeta } from './shapes';
import { CM_PER_UNIT, PLATFORM_TOP, PLATFORM_W, WORLD_W, type Game } from './game';

export interface Camera {
  /** 画面中心に来るワールド座標 */
  cx: number;
  cy: number;
  zoom: number;
}

export class Renderer {
  readonly ctx: CanvasRenderingContext2D;
  /** CSS ピクセルでのサイズ */
  w = 0;
  h = 0;
  dpr = 1;
  /** ワールド 1 単位あたりの CSS ピクセル（zoom=1 のとき） */
  scale = 1;

  constructor(readonly canvas: HTMLCanvasElement) {
    this.ctx = canvas.getContext('2d')!;
    this.resize();
  }

  /**
   * 表示サイズに合わせて canvas の解像度を合わせ直す。
   * アプリ内ブラウザなどは読み込み後に表示サイズが変わることがあり、
   * 合わせないと引き伸ばされて縦長・ぼやけて見えるので毎フレーム確認する
   */
  resize() {
    const w = this.canvas.clientWidth;
    const h = this.canvas.clientHeight;
    const dpr = Math.min(window.devicePixelRatio || 1, 3);
    if (w === this.w && h === this.h && dpr === this.dpr) return;
    this.w = w;
    this.h = h;
    this.dpr = dpr;
    this.canvas.width = Math.max(1, Math.round(w * dpr));
    this.canvas.height = Math.max(1, Math.round(h * dpr));
    // 端数による拡大縮小のにじみを避けるため、実際の画素数から倍率を逆算する
    this.dpr = this.canvas.width / Math.max(1, w);
    this.scale = w / WORLD_W;
  }

  /** ズーム 1 のときの画面の高さ（ワールド単位） */
  get viewH() {
    return this.h / this.scale;
  }

  draw(game: Game, cam: Camera, shake = { x: 0, y: 0 }) {
    this.resize();
    const { ctx } = this;
    ctx.setTransform(this.dpr, 0, 0, this.dpr, 0, 0);
    this.drawSky(cam);

    const s = this.scale * cam.zoom;
    ctx.setTransform(
      this.dpr * s,
      0,
      0,
      this.dpr * s,
      this.dpr * (this.w / 2 - cam.cx * s + shake.x),
      this.dpr * (this.h / 2 - cam.cy * s + shake.y),
    );

    this.drawRuler(cam);
    this.drawPlatform();
    const aiming = game.phase === 'aim' && game.current;
    if (aiming) this.drawGuide(game.current!);
    for (const b of game.placed) drawBlock(ctx, b, 1);
    if (aiming) drawBlock(ctx, game.current!, 0.92);
  }

  private drawSky(cam: Camera) {
    const { ctx, w, h } = this;
    // 高く積むほど空の色が少しずつ濃くなる
    const t = Math.min(1, Math.max(0, -cam.cy / 1600));
    const top = mix('#bfe3ff', '#8fb8ff', t);
    const bottom = mix('#fff4f7', '#d9e8ff', t);
    const g = ctx.createLinearGradient(0, 0, 0, h);
    g.addColorStop(0, top);
    g.addColorStop(1, bottom);
    ctx.fillStyle = g;
    ctx.fillRect(0, 0, w, h);

    // 雲（ゆっくりパララックス）
    ctx.fillStyle = 'rgba(255,255,255,0.75)';
    const par = cam.cy * this.scale * 0.3;
    for (let i = 0; i < 6; i++) {
      const y = ((i * 197 - par) % (h + 120) + h + 120) % (h + 120) - 60;
      const x = ((i * 137) % w) + 20;
      cloud(ctx, x, y, 22 + (i % 3) * 8);
    }
  }

  private drawRuler(cam: Camera) {
    const { ctx } = this;
    const viewTop = cam.cy - this.viewH / cam.zoom / 2;
    const step = 50 / CM_PER_UNIT; // 50cm ごと
    ctx.font = '700 11px "Zen Maru Gothic", sans-serif';
    ctx.textBaseline = 'middle';
    for (let y = PLATFORM_TOP - step; y > viewTop - step; y -= step) {
      ctx.strokeStyle = 'rgba(80,90,140,0.18)';
      ctx.lineWidth = 1.5;
      ctx.setLineDash([4, 6]);
      ctx.beginPath();
      ctx.moveTo(0, y);
      ctx.lineTo(WORLD_W, y);
      ctx.stroke();
      ctx.setLineDash([]);
      ctx.fillStyle = 'rgba(80,90,140,0.55)';
      ctx.fillText(`${Math.round((PLATFORM_TOP - y) * CM_PER_UNIT)}cm`, 6, y - 8);
    }
  }

  private drawPlatform() {
    const { ctx } = this;
    const x0 = (WORLD_W - PLATFORM_W) / 2;
    // 支柱
    ctx.fillStyle = '#e8b4bc';
    roundRect(ctx, x0 + PLATFORM_W * 0.22, PLATFORM_TOP + 30, PLATFORM_W * 0.56, 2000, 10);
    ctx.fill();
    // 本体
    ctx.fillStyle = '#f7c6cf';
    roundRect(ctx, x0, PLATFORM_TOP, PLATFORM_W, 48, 8);
    ctx.fill();
    // 上面のクリーム
    ctx.fillStyle = '#fffaf0';
    roundRect(ctx, x0, PLATFORM_TOP, PLATFORM_W, 14, 7);
    ctx.fill();
    ctx.fillStyle = '#fffaf0';
    for (let i = 0; i < 8; i++) {
      ctx.beginPath();
      ctx.arc(x0 + 12.5 + i * 25, PLATFORM_TOP + 14, 7, 0, Math.PI);
      ctx.fill();
    }
  }

  private drawGuide(body: Matter.Body) {
    const { ctx } = this;
    const yFrom = body.bounds.max.y + 4;
    ctx.strokeStyle = 'rgba(255,255,255,0.9)';
    ctx.lineWidth = 2;
    ctx.setLineDash([3, 5]);
    ctx.beginPath();
    ctx.moveTo(body.position.x, yFrom);
    ctx.lineTo(body.position.x, PLATFORM_TOP);
    ctx.stroke();
    ctx.setLineDash([]);
  }
}

export function drawBlock(ctx: CanvasRenderingContext2D, body: Matter.Body, alpha: number) {
  const meta = blockMeta(body);
  const color = COLORS[meta.spec.color];
  const parts = body.parts.length > 1 ? body.parts.slice(1) : [body];
  const b = body.bounds;
  // 厚みの分だけ上下にずらして描き、物理上の位置とのずれを半分ずつにする
  const depth = 3;
  ctx.globalAlpha = alpha;
  ctx.lineJoin = 'round';

  // 厚み（下にずらした濃い色のシルエット）でおもちゃのブロックらしい立体感を出す
  ctx.save();
  ctx.translate(0, depth / 2);
  unionPath(ctx, parts);
  ctx.fillStyle = color.edge;
  ctx.strokeStyle = color.edge;
  ctx.lineWidth = 3;
  ctx.stroke();
  ctx.fill();
  ctx.restore();

  ctx.save();
  ctx.translate(0, -depth / 2);
  // 縁取り（パーツをまとめた 1 本のパスなので継ぎ目が出ない）
  unionPath(ctx, parts);
  ctx.strokeStyle = color.edge;
  ctx.lineWidth = 3;
  ctx.stroke();

  // 上向きの辺に細いつやを残しつつ、上が明るいグラデーションで塗る
  ctx.save();
  ctx.clip();
  ctx.fillStyle = 'rgba(255,255,255,0.85)';
  ctx.fill();
  ctx.translate(0, 2.6);
  const g = ctx.createLinearGradient(0, b.min.y, 0, b.max.y);
  g.addColorStop(0, color.light);
  g.addColorStop(0.6, color.fill);
  g.addColorStop(1, color.fill);
  unionPath(ctx, parts);
  ctx.fillStyle = g;
  ctx.fill();
  ctx.restore();

  // 顔
  const cos = Math.cos(body.angle);
  const sin = Math.sin(body.angle);
  const fx = body.position.x + meta.face.x * cos - meta.face.y * sin;
  const fy = body.position.y + meta.face.x * sin + meta.face.y * cos;
  const k = meta.spec.cell / 19;
  ctx.save();
  ctx.translate(fx, fy);
  ctx.rotate(body.angle);
  ctx.scale(k, k);
  ctx.fillStyle = '#3b2f3f';
  ctx.beginPath();
  ctx.ellipse(-4.5, -1, 1.7, 2.3, 0, 0, Math.PI * 2);
  ctx.ellipse(4.5, -1, 1.7, 2.3, 0, 0, Math.PI * 2);
  ctx.fill();
  ctx.strokeStyle = '#3b2f3f';
  ctx.lineWidth = 1.3;
  ctx.lineCap = 'round';
  ctx.beginPath();
  ctx.arc(0, 2, 2.2, 0.15 * Math.PI, 0.85 * Math.PI);
  ctx.stroke();
  ctx.fillStyle = 'rgba(255,120,140,0.45)';
  ctx.beginPath();
  ctx.ellipse(-7.5, 2.5, 2.4, 1.4, 0, 0, Math.PI * 2);
  ctx.ellipse(7.5, 2.5, 2.4, 1.4, 0, 0, Math.PI * 2);
  ctx.fill();
  ctx.restore();
  ctx.restore();

  ctx.globalAlpha = 1;
}

/** 全パーツを 1 本のパスにまとめる（nonzero で塗ると和集合になる） */
function unionPath(ctx: CanvasRenderingContext2D, parts: Matter.Body[]) {
  ctx.beginPath();
  for (const p of parts) {
    const v = p.vertices;
    ctx.moveTo(v[0].x, v[0].y);
    for (let i = 1; i < v.length; i++) ctx.lineTo(v[i].x, v[i].y);
    ctx.closePath();
  }
}

export function roundRect(ctx: CanvasRenderingContext2D, x: number, y: number, w: number, h: number, r: number) {
  ctx.beginPath();
  ctx.roundRect(x, y, w, h, r);
}

function cloud(ctx: CanvasRenderingContext2D, x: number, y: number, r: number) {
  ctx.beginPath();
  ctx.arc(x, y, r, 0, Math.PI * 2);
  ctx.arc(x + r * 0.9, y + r * 0.2, r * 0.75, 0, Math.PI * 2);
  ctx.arc(x - r * 0.9, y + r * 0.25, r * 0.65, 0, Math.PI * 2);
  ctx.fill();
}

function mix(a: string, b: string, t: number) {
  const pa = parseInt(a.slice(1), 16);
  const pb = parseInt(b.slice(1), 16);
  const ch = (sh: number) => Math.round(((pa >> sh) & 255) * (1 - t) + ((pb >> sh) & 255) * t);
  return `rgb(${ch(16)},${ch(8)},${ch(0)})`;
}
