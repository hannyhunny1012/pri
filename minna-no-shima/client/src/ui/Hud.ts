import { INVENTORY_MAX } from "../core/constants";
import { formatMeters, MILESTONES, nextMilestone } from "../core/milestones";
import type { RankingEntry } from "../core/protocol";
import { STAMPS } from "../core/stamps";

const $ = <T extends HTMLElement>(id: string) => document.getElementById(id) as T;

export class Hud {
  private towerEl = $("tower-m");
  private towerBox = this.towerEl.parentElement!;
  private nextBar = $("next-bar");
  private nextLabel = $("next-label");
  private rankingEl = $<HTMLOListElement>("ranking");
  private woodEl = $("inv-wood");
  private stoneEl = $("inv-stone");
  private invBox = this.woodEl.closest(".inv") as HTMLElement;
  private myTotalEl = $("my-total");
  private toasts = $("toasts");
  private shareLink = $<HTMLAnchorElement>("share");
  private lastTowerCm = -1;
  private myName = "";
  private myTotalCm = 0;
  private towerCm = 0;

  constructor(onStamp: (id: number) => void) {
    $("hud").hidden = false;
    $("inv-max").textContent = String(INVENTORY_MAX);
    const bar = $("stamps");
    STAMPS.forEach((s, i) => {
      const b = document.createElement("button");
      b.type = "button";
      b.textContent = s;
      b.setAttribute("aria-label", `スタンプ ${s}`);
      b.addEventListener("click", () => onStamp(i));
      bar.appendChild(b);
    });
    // スマホの小さい画面ではランキングを閉じた状態で始める
    if (window.innerWidth < 520) ($("ranking-box") as HTMLDetailsElement).open = false;
  }

  setName(name: string): void {
    this.myName = name;
  }

  setTower(cm: number): void {
    this.towerCm = cm;
    if (cm === this.lastTowerCm) return;
    this.updateShareLink();
    const grew = this.lastTowerCm >= 0 && cm > this.lastTowerCm;
    this.lastTowerCm = cm;
    this.towerEl.textContent = formatMeters(cm);
    if (grew) {
      this.towerBox.classList.remove("bump");
      void this.towerBox.offsetWidth; // アニメーションを再始動
      this.towerBox.classList.add("bump");
    }
    const next = nextMilestone(cm);
    if (next) {
      const prevIdx = MILESTONES.indexOf(next) - 1;
      const from = prevIdx >= 0 ? MILESTONES[prevIdx].heightCm : 0;
      const pct = ((cm - from) / (next.heightCm - from)) * 100;
      this.nextBar.style.width = `${Math.max(0, Math.min(100, pct))}%`;
      this.nextLabel.textContent = `次の節目 ${formatMeters(next.heightCm)}m：${next.title}`;
    } else {
      this.nextBar.style.width = "100%";
      this.nextLabel.textContent = "すべての節目を達成！";
    }
  }

  setInventory(wood: number, stone: number): void {
    this.woodEl.textContent = String(wood);
    this.stoneEl.textContent = String(stone);
    this.invBox.classList.toggle("full", wood + stone >= INVENTORY_MAX);
  }

  setMyTotal(cm: number): void {
    this.myTotalCm = cm;
    this.myTotalEl.textContent = formatMeters(cm);
    this.updateShareLink();
  }

  setRanking(list: RankingEntry[]): void {
    this.rankingEl.replaceChildren();
    if (list.length === 0) {
      const li = document.createElement("li");
      li.className = "empty";
      li.textContent = "まだ誰も納品していません";
      this.rankingEl.appendChild(li);
      return;
    }
    for (const r of list) {
      const li = document.createElement("li");
      if (r.name === this.myName) li.classList.add("me");
      const n = document.createElement("span");
      n.className = "n";
      n.textContent = r.name; // textContent なので名前に HTML が入っても安全
      const v = document.createElement("span");
      v.textContent = `${formatMeters(r.cm)}m`;
      li.append(n, v);
      this.rankingEl.appendChild(li);
    }
  }

  toast(text: string, big = false): void {
    const el = document.createElement("div");
    el.className = big ? "toast big" : "toast";
    el.textContent = text;
    this.toasts.appendChild(el);
    while (this.toasts.children.length > 4) this.toasts.firstElementChild!.remove();
    el.addEventListener("animationend", () => el.remove());
  }

  /** window.open が使えない環境もあるので、普通のリンクの href を更新しておく */
  private updateShareLink(): void {
    const text =
      `🏝️ みんなの島で「みんなのタワー」に ${formatMeters(this.myTotalCm)}m 貢献しました！` +
      `\n現在のタワーは ${formatMeters(this.towerCm)}m 🗼\n#みんなの島`;
    const url = new URL("https://x.com/intent/post");
    url.searchParams.set("text", text);
    const shareUrl = import.meta.env.VITE_SHARE_URL ?? location.origin + location.pathname;
    if (shareUrl) url.searchParams.set("url", shareUrl);
    this.shareLink.href = url.toString();
  }
}
