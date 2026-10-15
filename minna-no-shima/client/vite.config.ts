import { defineConfig } from "vite";

export default defineConfig({
  // 静的ホスティング（サブパス配置）でも動くように相対パスで出力
  base: "./",
  server: { host: true },
  build: { chunkSizeWarningLimit: 2000 },
});
