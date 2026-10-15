/**
 * 操作:
 *  - 短いタップ（動かさずに 250ms 未満で離す）… 回転
 *  - ドラッグして離す / 長押しして離す … 落下
 * ドラッグは相対移動なので、指でブロックを隠さずに操作できる
 */
export interface InputHandlers {
  move(dxCss: number): void;
  rotate(): void;
  drop(): void;
}

const TAP_MS = 250;
const MOVE_SLOP = 8;

export function bindInput(el: HTMLElement, h: InputHandlers): () => void {
  let id: number | null = null;
  let startX = 0;
  let startY = 0;
  let lastX = 0;
  let t0 = 0;
  let dragging = false;

  const down = (e: PointerEvent) => {
    if (id !== null) return;
    id = e.pointerId;
    el.setPointerCapture(id);
    startX = lastX = e.clientX;
    startY = e.clientY;
    t0 = performance.now();
    dragging = false;
    e.preventDefault();
  };
  const move = (e: PointerEvent) => {
    if (e.pointerId !== id) return;
    if (!dragging && Math.hypot(e.clientX - startX, e.clientY - startY) > MOVE_SLOP) {
      dragging = true;
    }
    if (dragging) {
      h.move(e.clientX - lastX);
      lastX = e.clientX;
    }
  };
  const up = (e: PointerEvent) => {
    if (e.pointerId !== id) return;
    id = null;
    if (!dragging && performance.now() - t0 < TAP_MS) h.rotate();
    else h.drop();
  };
  const cancel = (e: PointerEvent) => {
    if (e.pointerId === id) id = null;
  };

  const key = (e: KeyboardEvent) => {
    if (e.key === 'ArrowLeft') h.move(-12);
    else if (e.key === 'ArrowRight') h.move(12);
    else if (e.key === 'ArrowUp' || e.key === 'r') h.rotate();
    else if (e.key === ' ' || e.key === 'ArrowDown' || e.key === 'Enter') h.drop();
    else return;
    e.preventDefault();
  };

  el.addEventListener('pointerdown', down);
  el.addEventListener('pointermove', move);
  el.addEventListener('pointerup', up);
  el.addEventListener('pointercancel', cancel);
  window.addEventListener('keydown', key);
  return () => {
    el.removeEventListener('pointerdown', down);
    el.removeEventListener('pointermove', move);
    el.removeEventListener('pointerup', up);
    el.removeEventListener('pointercancel', cancel);
    window.removeEventListener('keydown', key);
  };
}
