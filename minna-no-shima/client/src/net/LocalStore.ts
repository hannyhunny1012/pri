import type { RankingEntry, SelfInfo } from "../core/protocol";
import type { ContributionStore } from "../core/world";

// ブラウザ単体で遊ぶ間の永続化。サーバー版では SQLite / PostgreSQL に置き換わる。

const KEY = "mns.store.v1";

interface Data {
  towerCm: number;
  totals: Record<string, { name: string; cm: number }>;
  /** day -> token -> cm */
  daily: Record<string, Record<string, number>>;
}

export class LocalStore implements ContributionStore {
  private data: Data;

  constructor() {
    this.data = { towerCm: 0, totals: {}, daily: {} };
    try {
      const raw = localStorage.getItem(KEY);
      if (raw) this.data = { ...this.data, ...JSON.parse(raw) };
    } catch {
      // 読めなければ初期状態から
    }
  }

  private save(): void {
    try {
      localStorage.setItem(KEY, JSON.stringify(this.data));
    } catch {
      // 保存できない環境（プライベートモード等）でも遊べるようにする
    }
  }

  loadTowerCm(): number {
    return this.data.towerCm;
  }

  recordDelivery(token: string, name: string, addedCm: number, day: string): SelfInfo {
    this.data.towerCm += addedCm;
    const t = (this.data.totals[token] ??= { name, cm: 0 });
    t.name = name;
    t.cm += addedCm;
    // 古い日のデータは捨てる
    for (const d of Object.keys(this.data.daily)) if (d !== day) delete this.data.daily[d];
    const today = (this.data.daily[day] ??= {});
    today[token] = (today[token] ?? 0) + addedCm;
    this.save();
    return this.getSelf(token, day);
  }

  getSelf(token: string, day: string): SelfInfo {
    return {
      totalCm: this.data.totals[token]?.cm ?? 0,
      todayCm: this.data.daily[day]?.[token] ?? 0,
    };
  }

  topToday(day: string, limit: number): RankingEntry[] {
    const today = this.data.daily[day] ?? {};
    return Object.entries(today)
      .map(([token, cm]) => ({ name: this.data.totals[token]?.name ?? "?", cm }))
      .sort((a, b) => b.cm - a.cm)
      .slice(0, limit);
  }
}
