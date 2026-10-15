import { TICK_MS } from "../core/constants";
import { generateMap } from "../core/map";
import type { ClientMessage, GameEvent, RankingEntry, SelfInfo, Snapshot } from "../core/protocol";
import { dayJst, World } from "../core/world";
import type { Backend, JoinResult } from "./Backend";
import { LocalStore } from "./LocalStore";

const TOKEN_KEY = "mns.token";
const RANKING_INTERVAL_MS = 5000;

function playerToken(): string {
  try {
    let t = localStorage.getItem(TOKEN_KEY);
    if (!t) {
      t = crypto.randomUUID();
      localStorage.setItem(TOKEN_KEY, t);
    }
    return t;
  } catch {
    return crypto.randomUUID();
  }
}

/**
 * ブラウザ内で World を20Hzで回す「ローカルサーバー」。
 * 送受信はすべてメッセージ／スナップショット経由にしてあり、ネットワーク版と同じ流れになる。
 */
export class LocalBackend implements Backend {
  private readonly store = new LocalStore();
  readonly world = new World(generateMap(), this.store, () => performance.now());
  private readonly selfId = "local";
  private token = playerToken();
  private timer: number | undefined;
  private rankingTimer: number | undefined;
  private snapCbs: ((s: Snapshot) => void)[] = [];
  private eventCbs: ((e: GameEvent) => void)[] = [];
  private rankingCbs: ((r: RankingEntry[]) => void)[] = [];
  private selfCbs: ((s: SelfInfo) => void)[] = [];
  private last = 0;

  async join(name: string): Promise<JoinResult> {
    this.world.addPlayer(this.selfId, this.token, name);
    this.last = performance.now();
    this.timer = window.setInterval(() => this.step(), TICK_MS);
    this.rankingTimer = window.setInterval(() => this.pushRanking(), RANKING_INTERVAL_MS);
    queueMicrotask(() => this.pushRanking());
    return { selfId: this.selfId, self: this.store.getSelf(this.token, this.today()) };
  }

  private today(): string {
    return dayJst(Date.now());
  }

  private step(): void {
    const now = performance.now();
    this.world.tick(now - this.last);
    this.last = now;
    const snap = this.world.snapshot();
    for (const cb of this.snapCbs) cb(snap);
    for (const ev of this.world.drainEvents()) {
      for (const cb of this.eventCbs) cb(ev);
      if (ev.t === "delivered") {
        const self = this.store.getSelf(this.token, this.today());
        for (const cb of this.selfCbs) cb(self);
        this.pushRanking();
      }
    }
  }

  private pushRanking(): void {
    const r = this.store.topToday(this.today(), 10);
    for (const cb of this.rankingCbs) cb(r);
  }

  send(msg: ClientMessage): void {
    // ネットワーク越しと同じく、構造化クローンした値だけを渡す
    this.world.handle(this.selfId, structuredClone(msg));
  }

  onSnapshot(cb: (s: Snapshot) => void): void {
    this.snapCbs.push(cb);
  }
  onEvent(cb: (e: GameEvent) => void): void {
    this.eventCbs.push(cb);
  }
  onRanking(cb: (r: RankingEntry[]) => void): void {
    this.rankingCbs.push(cb);
  }
  onSelfInfo(cb: (s: SelfInfo) => void): void {
    this.selfCbs.push(cb);
  }

  leave(): void {
    window.clearInterval(this.timer);
    window.clearInterval(this.rankingTimer);
    this.world.removePlayer(this.selfId);
  }
}
