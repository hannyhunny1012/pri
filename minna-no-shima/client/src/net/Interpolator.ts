// サーバーから届く位置を少し遅らせて表示し、2つのスナップショットの間を線形補間する。

interface Sample {
  t: number;
  x: number;
  y: number;
}

export class PositionBuffer {
  private samples: Sample[] = [];

  push(t: number, x: number, y: number): void {
    const last = this.samples[this.samples.length - 1];
    if (last && t <= last.t) return;
    this.samples.push({ t, x, y });
    if (this.samples.length > 20) this.samples.shift();
  }

  sample(renderT: number): { x: number; y: number } | null {
    const s = this.samples;
    if (s.length === 0) return null;
    if (renderT <= s[0].t) return { x: s[0].x, y: s[0].y };
    for (let i = s.length - 1; i > 0; i--) {
      const a = s[i - 1];
      const b = s[i];
      if (renderT >= a.t && renderT <= b.t) {
        const k = (renderT - a.t) / (b.t - a.t);
        return { x: a.x + (b.x - a.x) * k, y: a.y + (b.y - a.y) * k };
      }
    }
    const last = s[s.length - 1];
    return { x: last.x, y: last.y };
  }
}

/** サーバー時刻とローカル時刻のずれを推定する */
export class ServerClock {
  private offset: number | null = null;

  observe(serverT: number, localT: number): void {
    const o = serverT - localT;
    // 遅れて届いたパケットに引きずられないよう、ゆっくり追従
    this.offset = this.offset === null ? o : this.offset * 0.9 + o * 0.1;
  }

  now(localT: number): number {
    return localT + (this.offset ?? 0);
  }
}
