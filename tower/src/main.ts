import { Game, PLATFORM_TOP } from './game';
import { bindInput } from './input';
import { Renderer, type Camera } from './render';
import { makeBlockSpec, type BlockSpec } from './shapes';

const BLOCK_COUNT = 20;

const canvas = document.getElementById('stage') as HTMLCanvasElement;
const hudCount = document.getElementById('hud-count')!;
const hudHeight = document.getElementById('hud-height')!;
const hint = document.getElementById('hint')!;
const overlay = document.getElementById('overlay')!;

const renderer = new Renderer(canvas);
window.addEventListener('resize', () => renderer.resize());

let game: Game;
const cam: Camera = { cx: 180, cy: 0, zoom: 1 };

function randomSpecs(): BlockSpec[] {
  const specs: BlockSpec[] = [];
  for (let i = 0; i < BLOCK_COUNT; i++) specs.push(makeBlockSpec(Math.random, specs[i - 1]?.color));
  return specs;
}

function start() {
  overlay.hidden = true;
  game = new Game(randomSpecs(), {
    onSettle: updateHud,
    onCollapse: () => setTimeout(() => showEnd('くずれた！'), 1200),
    onComplete: () => setTimeout(() => showEnd('完走！'), 600),
  });
  updateHud();
  cam.cy = targetCamY();
}

function updateHud() {
  hudCount.textContent = `${game.index} / ${game.total}`;
  hudHeight.innerHTML = `${game.heightCm()}<small>cm</small>`;
}

function showEnd(title: string) {
  overlay.innerHTML = `
    <div class="card">
      <h2>${title}</h2>
      <p>${game.heightCm()}cm（${game.index}/${game.total}個）</p>
      <button class="btn" id="retry">もう一回</button>
    </div>`;
  overlay.hidden = false;
  document.getElementById('retry')!.addEventListener('click', start);
}

/** 落とす位置が画面上部 1/4 あたりに来るようにしつつ、土台より下は映しすぎない */
function targetCamY() {
  const vh = renderer.viewH;
  const fromSpawn = game.spawnY() + vh * 0.25;
  const floorLimit = PLATFORM_TOP + 150 - vh / 2;
  return Math.min(fromSpawn, floorLimit);
}

bindInput(canvas, {
  move: (dx) => game.moveAim(dx / renderer.scale),
  rotate: () => game.rotate(),
  drop: () => {
    game.drop();
    hint.style.opacity = '0';
  },
});

let last = performance.now();
function frame(now: number) {
  const dt = now - last;
  last = now;
  game.update(dt);
  if (game.phase === 'aim' || game.phase === 'falling') {
    cam.cy += (targetCamY() - cam.cy) * Math.min(1, dt / 250);
  }
  renderer.draw(game, cam);
  requestAnimationFrame(frame);
}

start();
requestAnimationFrame(frame);

if (import.meta.env.DEV) (window as unknown as { __tower: unknown }).__tower = { get game() { return game; } };
