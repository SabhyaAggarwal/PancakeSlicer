import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';
import { orcaAssets } from './orca-assets.ts';

export default defineConfig({
  plugins: [react(), orcaAssets()],
  server: {
    port: 5173,
    proxy: {
      '/api': { target: process.env.ORCA_API ?? 'http://localhost:8080', changeOrigin: true },
    },
  },
  build: {
    target: 'es2022',
    chunkSizeWarningLimit: 700,
    rolldownOptions: {
      output: {
        codeSplitting: {
          groups: [
            { name: 'three', test: /node_modules[\\/]three/ },
            { name: 'react', test: /node_modules[\\/](react|react-dom|scheduler)[\\/]/ },
          ],
        },
      },
    },
  },
});
