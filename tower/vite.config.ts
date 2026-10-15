import { defineConfig } from 'vite';

// 相対パスで出力するので、Vercel のルートでも GitHub Pages のサブパスでもそのまま動く
export default defineConfig({
  base: './',
  server: { host: true },
});
