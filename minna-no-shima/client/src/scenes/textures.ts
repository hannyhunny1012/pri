import Phaser from "phaser";

// 外部素材を使わず、起動時に図形からテクスチャを作る

export function makeTextures(scene: Phaser.Scene): void {
  const g = scene.make.graphics({}, false);

  // 木（40x52、根元が下端付近）
  g.clear();
  g.fillStyle(0x000000, 0.18).fillEllipse(20, 47, 28, 8);
  g.fillStyle(0x8b5a2b).fillRect(16, 30, 8, 17);
  g.fillStyle(0x2f8f46).fillCircle(20, 22, 15);
  g.fillStyle(0x3fae57).fillCircle(14, 18, 10).fillCircle(26, 17, 10);
  g.fillStyle(0x5cc86f).fillCircle(20, 11, 9);
  g.fillStyle(0xffffff, 0.25).fillCircle(16, 9, 3);
  g.generateTexture("tree", 40, 52);

  // 切り株
  g.clear();
  g.fillStyle(0x000000, 0.15).fillEllipse(20, 47, 22, 6);
  g.fillStyle(0x8b5a2b).fillRect(13, 38, 14, 9);
  g.fillStyle(0xd2a679).fillEllipse(20, 38, 14, 6);
  g.generateTexture("stump", 40, 52);

  // 岩（40x40）
  g.clear();
  g.fillStyle(0x000000, 0.18).fillEllipse(20, 35, 32, 8);
  g.fillStyle(0x7d858c);
  g.fillPoints(
    [
      { x: 5, y: 33 },
      { x: 8, y: 18 },
      { x: 17, y: 9 },
      { x: 29, y: 11 },
      { x: 36, y: 22 },
      { x: 35, y: 33 },
    ].map((p) => new Phaser.Math.Vector2(p.x, p.y)),
    true,
  );
  g.fillStyle(0x9aa3aa).fillTriangle(10, 20, 18, 11, 22, 22);
  g.fillStyle(0xbfc6cc).fillTriangle(17, 11, 28, 12, 22, 19);
  g.generateTexture("rock", 40, 40);

  // 砕けた岩
  g.clear();
  g.fillStyle(0x000000, 0.12).fillEllipse(20, 35, 24, 6);
  g.fillStyle(0x8d959c).fillCircle(13, 32, 4).fillCircle(22, 33, 3).fillCircle(28, 31, 3);
  g.generateTexture("rubble", 40, 40);

  // 花火の火花
  g.clear();
  g.fillStyle(0xffffff).fillCircle(4, 4, 4);
  g.generateTexture("spark", 8, 8);

  g.destroy();
}
