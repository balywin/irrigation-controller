import { defineConfig } from 'vite';
import { svelte } from '@sveltejs/vite-plugin-svelte';
import { rmSync } from 'fs';

export default defineConfig({
  plugins: [
    svelte(),
    {
      name: 'clean-assets',
      buildStart() {
        rmSync('../data/assets', { recursive: true, force: true });
      },
    },
  ],
  build: {
    outDir: '../data',
    emptyOutDir: false,
  },
  test: {
    environment: 'node',
  },
});
