import type { Plugin } from 'vite';

export function catalogWorkerStart(): Plugin {
  return {
    name: 'orca-catalog-worker-start',
    apply: 'build',
    transformIndexHtml(_html, ctx) {
      const file = Object.values(ctx.bundle ?? {}).find((output) => /(^|\/)catalog\.worker-[^/]*\.js$/.test(output.fileName))?.fileName;
      if (!file) return [];
      return [{ tag: 'script', injectTo: 'head', children:
        `try{const w=new Worker(${JSON.stringify('/' + file)},{type:'module'});w.addEventListener('error',()=>{w.failed=true});window.__orcaCatalogWorker=w}catch{}` }];
    },
  };
}
