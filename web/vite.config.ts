import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';
import { demoBuild } from './demo-build.ts';
import { bundleReport } from './bundle-report.ts';
import { optionAssets } from './option-assets.ts';
import { mockAssets } from './mock-assets.ts';
import { orcaAssets } from './orca-assets.ts';
import { licenceNotices } from './licence-notices.ts';
import { bambuChecks } from './bambu-checks.ts';
import { devCompression } from './dev-compression.ts';
import { buildStartup, devStartup } from './dev-startup.ts';
import { devBuildSwitch } from './dev-build-switch.ts';
import { fileURLToPath } from 'node:url';

export default defineConfig(({ mode }) => ({
  plugins: [...(mode === 'demo' ? [demoBuild()] : []), devCompression(), devBuildSwitch(), devStartup(), buildStartup(), react(), orcaAssets(), optionAssets(), bambuChecks(), mockAssets(), bundleReport(), licenceNotices()],
  worker: { format: 'es', plugins: () => [...(mode === 'demo' ? [demoBuild()] : []), optionAssets(false), licenceNotices(false)] },
  server: {
    port: 20120,
    strictPort: true,
    host: '127.0.0.1',
    allowedHosts: [],
    fs: { allow: [fileURLToPath(new URL('.', import.meta.url)), fileURLToPath(new URL('../server/method_capabilities.json', import.meta.url))] },
    proxy: {
      '/api': { target: process.env.ORCA_API ?? 'http://127.0.0.1:20198', changeOrigin: true },
    },
  },
  build: {
    outDir: mode === 'demo' ? 'dist-demo' : 'dist',
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
}));
