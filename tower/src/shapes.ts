import Matter from 'matter-js';

const { Bodies, Body, Vertices } = Matter;

/** ブロックの色。シェア文の絵文字と 1 対 1 で対応する */
export const COLORS = [
  { fill: '#ff6b81', edge: '#c2405a', emoji: '🟥' },
  { fill: '#ffa94d', edge: '#c9741c', emoji: '🟧' },
  { fill: '#ffd43b', edge: '#c49a00', emoji: '🟨' },
  { fill: '#69db7c', edge: '#2f9e44', emoji: '🟩' },
  { fill: '#4dabf7', edge: '#1c7ed6', emoji: '🟦' },
  { fill: '#b197fc', edge: '#7048e8', emoji: '🟪' },
  { fill: '#d4a373', edge: '#9c6644', emoji: '🟫' },
] as const;

type Pt = [number, number];
type PartDef =
  | { kind: 'cells'; cells: Pt[] }
  | { kind: 'poly'; verts: Pt[] }
  | { kind: 'circle'; x: number; y: number; r: number };

interface ShapeDef {
  id: string;
  parts: PartDef[];
}

// 座標はセル単位（1 = セル 1 個分）。凹んだ形は凸パーツの組み合わせで作る
const SHAPES: ShapeDef[] = [
  { id: 'I3', parts: [{ kind: 'cells', cells: [[0, 0], [1, 0], [2, 0]] }] },
  { id: 'I4', parts: [{ kind: 'cells', cells: [[0, 0], [1, 0], [2, 0], [3, 0]] }] },
  { id: 'O', parts: [{ kind: 'cells', cells: [[0, 0], [1, 0], [0, 1], [1, 1]] }] },
  { id: 'L', parts: [{ kind: 'cells', cells: [[0, 0], [0, 1], [0, 2], [1, 2]] }] },
  { id: 'T', parts: [{ kind: 'cells', cells: [[0, 0], [1, 0], [2, 0], [1, 1]] }] },
  { id: 'S', parts: [{ kind: 'cells', cells: [[1, 0], [2, 0], [0, 1], [1, 1]] }] },
  { id: 'U', parts: [{ kind: 'cells', cells: [[0, 0], [0, 1], [1, 1], [2, 1], [2, 0]] }] },
  { id: 'plus', parts: [{ kind: 'cells', cells: [[1, 0], [0, 1], [1, 1], [2, 1], [1, 2]] }] },
  { id: 'tri', parts: [{ kind: 'poly', verts: [[0, 2], [1.25, 0], [2.5, 2]] }] },
  { id: 'trap', parts: [{ kind: 'poly', verts: [[0, 1.4], [0.7, 0], [2.3, 0], [3, 1.4]] }] },
  { id: 'hex', parts: [{ kind: 'poly', verts: hexagon(1.1) }] },
  { id: 'ball', parts: [{ kind: 'circle', x: 0, y: 0, r: 0.9 }] },
  {
    id: 'mushroom',
    parts: [
      { kind: 'poly', verts: dome(1.4, 1.1) },
      { kind: 'cells', cells: [[0, 0]] }, // 軸（下で位置調整）
    ],
  },
  {
    id: 'bone',
    parts: [
      { kind: 'poly', verts: [[0.5, 0.25], [3.5, 0.25], [3.5, 0.95], [0.5, 0.95]] },
      { kind: 'circle', x: 0.45, y: 0.6, r: 0.6 },
      { kind: 'circle', x: 3.55, y: 0.6, r: 0.6 },
    ],
  },
  {
    id: 'arrow',
    parts: [
      { kind: 'poly', verts: [[0, 0.35], [1.8, 0.35], [1.8, 1.15], [0, 1.15]] },
      { kind: 'poly', verts: [[1.8, -0.2], [3, 0.75], [1.8, 1.7]] },
    ],
  },
];

// mushroom の軸をドームの真下に置く
{
  const m = SHAPES.find((s) => s.id === 'mushroom')!;
  m.parts[1] = { kind: 'poly', verts: [[-0.45, 0], [0.45, 0], [0.45, 1.1], [-0.45, 1.1]] };
}

function hexagon(r: number): Pt[] {
  return Array.from({ length: 6 }, (_, i) => {
    const a = (Math.PI / 3) * i;
    return [Math.cos(a) * r, Math.sin(a) * r] as Pt;
  });
}

/** 底辺が y=0、上に膨らむ半円もどき */
function dome(rx: number, ry: number): Pt[] {
  const pts: Pt[] = [];
  for (let i = 0; i <= 8; i++) {
    const a = Math.PI + (Math.PI / 8) * i;
    pts.push([Math.cos(a) * rx, Math.sin(a) * ry]);
  }
  return pts;
}

export const SHAPE_COUNT = SHAPES.length;

/** 1 個のブロックの出題内容。乱数から決まり、物理ボディとは独立 */
export interface BlockSpec {
  shape: number;
  color: number;
  cell: number; // セル 1 個の大きさ（ワールド単位）
}

export function makeBlockSpec(rand: () => number, prevColor = -1): BlockSpec {
  const shape = Math.floor(rand() * SHAPES.length);
  let color = Math.floor(rand() * (COLORS.length - 1));
  if (color >= prevColor && prevColor >= 0) color++; // 直前と同じ色は避ける
  const cell = 19 + Math.floor(rand() * 6);
  return { shape, color, cell };
}

export interface BlockMeta {
  spec: BlockSpec;
  face: { x: number; y: number }; // 顔を描く位置（ボディのローカル座標）
}

const BODY_OPTS: Matter.IChamferableBodyDefinition = {
  friction: 0.9,
  frictionStatic: 1.2,
  restitution: 0.02,
  density: 0.002,
};

export function createBlockBody(spec: BlockSpec): Matter.Body {
  const def = SHAPES[spec.shape];
  const c = spec.cell;
  const parts: Matter.Body[] = [];
  for (const p of def.parts) {
    if (p.kind === 'cells') {
      for (const [cx, cy] of p.cells) {
        parts.push(Bodies.rectangle((cx + 0.5) * c, (cy + 0.5) * c, c, c, BODY_OPTS));
      }
    } else if (p.kind === 'poly') {
      const verts = p.verts.map(([x, y]) => ({ x: x * c, y: y * c }));
      const centre = Vertices.centre(verts);
      parts.push(Body.create({ ...BODY_OPTS, position: centre, vertices: verts }));
    } else {
      parts.push(Bodies.circle(p.x * c, p.y * c, p.r * c, BODY_OPTS, 20));
    }
  }
  const body = parts.length === 1 ? parts[0] : Body.create({ ...BODY_OPTS, parts });

  // 顔はいちばん大きいパーツの中心に（U 字などは重心が形の外に出るため）
  const own = body.parts.length > 1 ? body.parts.slice(1) : [body];
  const biggest = own.reduce((a, b) => (b.area > a.area ? b : a));
  const meta: BlockMeta = {
    spec,
    face: { x: biggest.position.x - body.position.x, y: biggest.position.y - body.position.y },
  };
  body.plugin = { block: meta };
  return body;
}

export function blockMeta(body: Matter.Body): BlockMeta {
  return body.plugin.block as BlockMeta;
}
