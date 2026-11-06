import type { Plugin } from 'vite';


export function demoBuild(): Plugin {
  return {
    name: 'pancake-static-demo',
    transformIndexHtml: {
      order: 'pre',
      handler: (html) => html.replace(/\s*<link rel="preload" href="\/api\/[^>]+>/g, '')
        .replace('/src/bootstrap.ts', '/src/demo/bootstrap.ts'),
    },
    transform(code, id) {
      if (/src\/(api\/catalog|preview\/loader)\.worker\.ts$/.test(id))
        return `import { installDemoTransport } from '../demo/transport';\ninstallDemoTransport();\n${code}`;
    },
  };
}
