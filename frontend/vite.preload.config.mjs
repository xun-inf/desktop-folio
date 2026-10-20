import { defineConfig } from 'vite';
import { fileURLToPath } from 'node:url';
export default defineConfig({
  build: {
    outDir: fileURLToPath(new URL('./dist/preload', import.meta.url)),
    emptyOutDir: true,
    target: 'es2022',
    minify: false,
    lib: {
      entry: fileURLToPath(new URL('./src/preload/preload.ts', import.meta.url)),
      formats: ['cjs'],
      fileName: () => 'preload.cjs',
    },
    rolldownOptions: { external: ['electron'], output: { exports: 'none' } },
  },
});
