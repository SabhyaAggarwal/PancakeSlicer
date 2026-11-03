import { readFileSync, writeFileSync } from 'node:fs';
import { createServer } from 'vite';

const DOC = new URL('../docs/HLSD/web-state.md', import.meta.url);
const MARKER = '<!-- generated web tables -->';
const MACHINES: [string, string, unknown[]?][] = [
  ['/src/flow/app.ts', 'app'], ['/src/flow/routes.ts', 'routing'], ['/src/flow/project.ts', 'project'],
  ['/src/flow/slice.ts', 'slice'], ['/src/flow/job.ts', 'job'], ['/src/flow/import.ts', 'importing'],
  ['/src/flow/output.ts', 'output'], ['/src/flow/transfer.ts', 'createTransfer', []], ['/src/flow/modal.ts', 'modals'],
  ['/src/flow/ask.ts', 'asking'], ['/src/flow/tools.ts', 'tools'], ['/src/flow/playback.ts', 'playback'],
  ['/src/flow/viewer.ts', 'viewer'], ['/src/flow/profile-edit.ts', 'createProfileEdit', []],
  ['/src/flow/engine-health.ts', 'engineHealth'], ['/src/flow/version-view.ts', 'versionView'], ['/src/profiles/editing.ts', 'editing', ['process', 0]],
];

const target = new EventTarget();
const memory = new Map<string, string>();
const storage = { getItem: (k: string) => memory.get(k) ?? null, setItem: (k: string, v: string) => void memory.set(k, v), removeItem: (k: string) => void memory.delete(k) };
Object.assign(globalThis, {
  window: Object.assign(target, { location: new URL('http://localhost/'), history: { state: null, pushState() {}, replaceState() {} }, matchMedia: () => ({ matches: false, addEventListener() {} }), localStorage: storage }),
  document: Object.assign(new EventTarget(), { visibilityState: 'visible', documentElement: { dataset: {}, style: {} } }),
  localStorage: storage,
});

const server = await createServer({ root: new URL('.', import.meta.url).pathname, logLevel: 'error', server: { middlewareMode: true, hmr: false } });
try {
  const { machineDiagram, machineTable } = await server.ssrLoadModule('/src/flow/mermaid.ts');
  const sections: string[] = [];
  for (const [path, name, args] of MACHINES) {
    const value = (await server.ssrLoadModule(path))[name];
    const m = args ? value(...args) : value;
    const def = 'lifecycle' in m ? m.lifecycle.def : m.machine?.def ?? m.def;
    sections.push(`### ${def.id}\n\nInitial state: \`${def.initial}\`${def.strict ? '. Strict: an event without an edge in the current state is an error.' : '.'}\n\n${machineTable(def)}\n\n\`\`\`mermaid\n${machineDiagram(def)}\n\`\`\``);
  }
  const doc = readFileSync(DOC, 'utf8'), at = doc.indexOf(MARKER);
  if (at < 0) throw new Error(`${MARKER} not found`);
  writeFileSync(DOC, `${doc.slice(0, at + MARKER.length)}\n\n## Web transition definitions\n\nThe following tables and diagrams are rendered from each actor’s actual \`.def\` by \`web/state-doc.ts\` using \`flow/mermaid.ts\`. Guard alternatives retain declaration order; internal transitions keep entry timers running. Actions patch context; entry effects run only on transitions with a destination. Events absent from a row are rejected and logged.\n\n${sections.join('\n\n')}\n`);
} finally {
  await server.close();
}
