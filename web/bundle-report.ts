import type { Plugin } from 'vite';

export function bundleReport(): Plugin {
  return {
    name: 'orca-bundle-report',
    generateBundle(_options, bundle) {
      const report = Object.values(bundle).filter((c) => c.type === 'chunk').map((chunk) => ({
        file: chunk.fileName,
        imports: chunk.imports,
        dynamicImports: chunk.dynamicImports,
        modules: Object.entries(chunk.modules).map(([id, info]) => ({
          id: id.replace(process.cwd(), '.'), bytes: info.renderedLength,
        })).sort((a, b) => b.bytes - a.bytes),
      }));
      this.emitFile({ type: 'asset', fileName: 'bundle-report.json', source: JSON.stringify(report, null, 2) });
    },
  };
}
