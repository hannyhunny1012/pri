// ゲーム全体（Phaser 込み）を1つの HTML ファイルにまとめる。
// サーバーなしでどこにでも置けるので、スマホでの試遊やプレビュー共有に使う。
// 使い方: npm run build:single  → dist-single/minna-no-shima.html

import { build } from "esbuild";
import { mkdir, readFile, writeFile } from "node:fs/promises";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const root = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const shareUrl = process.env.SHARE_URL ?? "";

const js = await build({
  entryPoints: [resolve(root, "src/main.ts")],
  bundle: true,
  format: "iife",
  target: "es2020",
  minify: true,
  write: false,
  define: {
    "import.meta.env.VITE_SHARE_URL": JSON.stringify(shareUrl),
  },
  legalComments: "none",
});
const css = await readFile(resolve(root, "src/style.css"), "utf8");
const html = await readFile(resolve(root, "index.html"), "utf8");
const body = html
  .slice(html.indexOf("<body>") + "<body>".length, html.indexOf("</body>"))
  .replace(/<script[^>]*src="\.\/src\/main\.ts"[^>]*><\/script>/, "")
  .trim();

// スクリプト中の "</script" で HTML が途切れないようにする
const code = js.outputFiles[0].text.replace(/<\/script/gi, "<\\/script");

const out = `<title>みんなの島</title>
<meta name="theme-color" content="#7cc6e8">
<style>
${css}
</style>
${body}
<script>
${code}
</script>
`;
await mkdir(resolve(root, "dist-single"), { recursive: true });
const outPath = resolve(root, "dist-single/minna-no-shima.html");
await writeFile(outPath, out);
console.log(`wrote ${outPath} (${(out.length / 1024).toFixed(0)} KB)`);
