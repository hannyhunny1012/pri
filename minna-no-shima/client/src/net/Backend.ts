import type { ClientMessage, GameEvent, RankingEntry, SelfInfo, Snapshot } from "../core/protocol";

export interface JoinResult {
  selfId: string;
  self: SelfInfo;
}

/**
 * ゲームサーバーとの窓口。今はブラウザ内で動く LocalBackend だけだが、
 * 後で Colyseus 版（ColyseusBackend）を同じ形で差し込む。
 */
export interface Backend {
  join(name: string): Promise<JoinResult>;
  send(msg: ClientMessage): void;
  onSnapshot(cb: (s: Snapshot) => void): void;
  onEvent(cb: (e: GameEvent) => void): void;
  onRanking(cb: (r: RankingEntry[]) => void): void;
  onSelfInfo(cb: (s: SelfInfo) => void): void;
  leave(): void;
}
