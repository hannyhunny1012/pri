import Phaser from "phaser";
import { sanitizeName } from "./core/world";
import { LocalBackend } from "./net/LocalBackend";
import { IslandScene, type SceneDeps } from "./scenes/IslandScene";
import { Hud } from "./ui/Hud";

const NAME_KEY = "mns.name";

const form = document.getElementById("join-form") as HTMLFormElement;
const input = document.getElementById("name") as HTMLInputElement;
try {
  input.value = localStorage.getItem(NAME_KEY) ?? "";
} catch {
  // localStorage が使えない環境
}

form.addEventListener("submit", async (e) => {
  e.preventDefault();
  const name = sanitizeName(input.value);
  if (!name) {
    input.focus();
    return;
  }
  try {
    localStorage.setItem(NAME_KEY, name);
  } catch {
    // 保存できなくても遊べる
  }
  input.blur();
  document.getElementById("join")!.hidden = true;
  await start(name);
});

async function start(name: string): Promise<void> {
  // 今はブラウザ内のローカルサーバー。後で Colyseus 版に差し替える
  const backend = new LocalBackend();
  const hud = new Hud((id) => backend.send({ t: "stamp", id }));
  hud.setName(name);
  backend.onRanking((r) => hud.setRanking(r));
  backend.onSelfInfo((s) => hud.setMyTotal(s.totalCm));

  const { selfId, self } = await backend.join(name);
  hud.setMyTotal(self.totalCm);

  const dev = new URLSearchParams(location.search).has("dev");
  const deps: SceneDeps = {
    backend,
    selfId,
    hud,
    devAddTower: dev ? (cm) => backend.world.devAddTower(cm) : undefined,
  };

  const game = new Phaser.Game({
    type: Phaser.AUTO,
    parent: "game",
    backgroundColor: "#5bb8e0",
    scale: { mode: Phaser.Scale.RESIZE, width: window.innerWidth, height: window.innerHeight },
    input: { activePointers: 2 },
    render: { antialias: true },
  });
  game.scene.add("island", IslandScene, true, deps);
  if (dev) Object.assign(window, { __mns: { game, backend } }); // 開発・自動テスト用
  window.addEventListener("pagehide", () => backend.leave());
}
