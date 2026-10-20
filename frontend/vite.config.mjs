import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';
import { fileURLToPath } from 'node:url';

export default defineConfig(({ command }) => ({
  root: fileURLToPath(new URL('./src/app', import.meta.url)),
  base: './',
  plugins: [
    react(),
    {
      name: 'development-csp',
      transformIndexHtml(html) {
        return command === 'serve'
          ? html
              .replace("script-src 'self'", "script-src 'self' 'unsafe-inline'")
              .replace("connect-src 'self'", "connect-src 'self' ws://127.0.0.1:5173")
          : html;
      },
    },
  ],
  build: {
    outDir: fileURLToPath(new URL('./dist/app', import.meta.url)),
    emptyOutDir: true,
    target: 'es2022',
    rolldownOptions: {
      input: {
        main: fileURLToPath(new URL('./src/app/main.html', import.meta.url)),
        settings: fileURLToPath(new URL('./src/app/settings.html', import.meta.url)),
        secondscreen: fileURLToPath(new URL('./src/app/secondscreen.html', import.meta.url)),
      },
    },
  },
  server: { host: '127.0.0.1', port: 5173, strictPort: true },
}));
