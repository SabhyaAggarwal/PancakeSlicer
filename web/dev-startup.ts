import type { DevEnvironment, HtmlTagDescriptor, Plugin } from 'vite';
import { parseAst } from 'vite';

const STARTUP_MODULES = [
  '/src/bootstrap.ts', '/src/main.tsx', '/src/persist/preferences.ts', '/src/flush.ts', '/src/components/SavedPreferencesNotice.tsx',
  '/src/plater/Plater3D.tsx', '/src/components/WorkspaceRail.tsx', '/src/components/Navigator.tsx', '/src/projects/ProjectStatus.tsx',
  '/src/components/ControlHints.tsx', '/src/plater/orientPrompt.ts', '/src/profiles/catalog-startup.ts', '/src/preflight/marks.ts',
  '/src/profiles/library-hydration.ts', '/src/profiles/printer-setups.ts', '/src/components/FirstRun.tsx', '/src/components/SettingsTab.tsx',
  '/src/flow/project-recovery.ts', '/src/persist/drafts.ts', '/src/flow/project-journal.ts', '/src/persist/project-service.ts',
  '/src/persist/wire-worker.ts', '/src/flow/boot.ts',
];

const TIMESTAMP = /([?&])t=\d+&?/;
const IMPORT = /([?&])import&?/;
const strip = (url: string, re: RegExp) => url.replace(re, '$1').replace(/[?&]$/, '');

const parsed = new WeakMap<object, string[]>();
function staticImports(result: { code: string }) {
  let urls = parsed.get(result);
  if (!urls) {
    urls = [];
    for (const node of parseAst(result.code).body) {
      if ((node.type === 'ImportDeclaration' || node.type === 'ExportAllDeclaration' || node.type === 'ExportNamedDeclaration') &&
        node.source && typeof node.source.value === 'string') urls.push(node.source.value);
    }
    parsed.set(result, urls);
  }
  return urls;
}

function served(env: DevEnvironment, url: string) {
  const time = env.moduleGraph.urlToModuleMap.get(url)?.lastHMRTimestamp;
  if (!time) return url;
  const [path, query] = url.split('?');
  return `${path}?t=${time}${query ? `&${query}` : ''}`;
}

async function startupGraph(env: DevEnvironment) {
  const seen = new Set<string>();
  const order: string[] = [];
  let level = STARTUP_MODULES.map((url) => served(env, url));
  while (level.length) {
    const next: string[] = [];
    await Promise.all(level.map(async (url) => {
      if (seen.has(url)) return;
      seen.add(url);
      if (url.includes('worker_file')) return;
      const result = await env.transformRequest(strip(strip(url, TIMESTAMP), IMPORT)).catch(() => null);
      if (!result) return;
      order.push(url);
      try {
        next.push(...staticImports(result).filter((dep) => dep.startsWith('/') && !seen.has(dep)));
      } catch {  }
    }));
    level = next;
  }
  return order;
}

export function devStartup(): Plugin {
  let env: DevEnvironment | undefined;
  return {
    name: 'orca-dev-startup',
    apply: 'serve',
    config: () => ({ server: { warmup: { clientFiles: STARTUP_MODULES.map((url) => `.${url}`) } } }),
    configureServer(server) { env = server.environments.client; },
    transform: {
      order: 'post',
      handler(code, id) {
        if (env?.depsOptimizer?.isOptimizedDepFile(id)) return { code: `${code}\n`, map: { mappings: '' } };
      },
    },
    async transformIndexHtml() {
      if (!env) return [];
      const urls = await startupGraph(env);
      return urls.map((href): HtmlTagDescriptor => ({ tag: 'link', attrs: { rel: 'modulepreload', href }, injectTo: 'head' }));
    },
  };
}
