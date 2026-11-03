import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';
import { bundleReport } from './bundle-report.ts';
import { optionAssets } from './option-assets.ts';
import { mockAssets } from './mock-assets.ts';
import { orcaAssets } from './orca-assets.ts';
import { licenceNotices } from './licence-notices.ts';

export default defineConfig({
  plugins: [react(), orcaAssets(), optionAssets(), mockAssets(), bundleReport(), licenceNotices()],
  worker: { format: 'es', plugins: () => [optionAssets(false), licenceNotices(false)] },
  server: {
    port: 20120,
    strictPort: true,
    host: '127.0.0.1',
    allowedHosts: [],
    proxy: {
      '/api': { target: process.env.ORCA_API ?? 'http://127.0.0.1:20196', changeOrigin: true },
    },
  },
  build: {
    target: ['chrome109', 'edge109', 'firefox115', 'safari16.4'],
    cssTarget: ['chrome109', 'edge109', 'firefox115', 'safari16.4'],
    chunkSizeWarningLimit: 700,
    rolldownOptions: {
      output: {
        codeSplitting: {
          groups: [
            { name: 'three', test: /node_modules[\\/]three[\\/]build[\\/]three\.core\.js/ },
            { name: 'three-renderer', test: /node_modules[\\/]three[\\/]build[\\/]three\.module\.js/ },
            { name: 'react', test: /node_modules[\\/](react|react-dom|scheduler)[\\/]/ },
          ],
        },
      },
    },
  },
});
