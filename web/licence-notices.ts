// Minified chunks drop licence comments, but MIT-style licences require the notice in every copy.
// Ship the licence of each npm package that reaches a main or worker chunk, plus the licence
import { existsSync, readdirSync, readFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import type { Plugin } from 'vite';

const bundled = new Map<string, { dir: string; files: Set<string> }>();

function licenceText(dir: string): string | undefined {
  const file = readdirSync(dir).find((f) => /^(licen[cs]e|copying)(\.|$)/i.test(f));
  if (file) return readFileSync(join(dir, file), 'utf8').trim();
  const readme = readdirSync(dir).find((f) => /^readme(\.|$)/i.test(f));
  const section = readme && /^#+\s*licen[cs]e\s*$([\s\S]*?)(?=^#+\s|(?![\s\S]))/im.exec(readFileSync(join(dir, readme), 'utf8'));
  return section ? section[1].trim() : undefined;
}

function embeddedNotices(files: Set<string>): string[] {
  const blocks = new Set<string>();
  for (const file of files) {
    for (const [block] of readFileSync(file, 'utf8').matchAll(/\/\*[\s\S]*?\*\//g)) {
      if (/@license|copyright/i.test(block)) blocks.add(block.replace(/^[ \t]+/gm, ''));
    }
  }
  return [...blocks];
}

export function licenceNotices(emit = true): Plugin {
  return {
    name: 'orca-licence-notices',
    generateBundle(_options, bundle) {
      for (const chunk of Object.values(bundle)) {
        if (chunk.type !== 'chunk') continue;
        for (const id of Object.keys(chunk.modules)) {
          const file = id.replace(/^\0/, '').replace(/\?.*$/, '');
          const match = /^(.*[\\/]node_modules[\\/])((?:@[^\\/]+[\\/])?[^\\/]+)[\\/]/.exec(file);
          if (!match) continue;
          const name = match[2].replace('\\', '/');
          if (!bundled.has(name)) bundled.set(name, { dir: match[1] + match[2], files: new Set() });
          if (existsSync(file)) bundled.get(name)!.files.add(file);
        }
      }
      if (!emit) return;
      for (const [, { dir }] of bundled) {
        const { dependencies = {} } = JSON.parse(readFileSync(join(dir, 'package.json'), 'utf8')) as { dependencies?: Record<string, string> };
        for (const name of Object.keys(dependencies)) {
          if (bundled.has(name)) continue;
          let parent = dir;
          while (!existsSync(join(parent, 'node_modules', name, 'package.json')) && dirname(parent) !== parent) parent = dirname(parent);
          const resolved = join(parent, 'node_modules', name);
          if (!existsSync(join(resolved, 'package.json'))) this.error(`Cannot resolve bundled dependency ${name} from ${dir}`);
          bundled.set(name, { dir: resolved, files: new Set() });
        }
      }
      const notices = [...bundled].sort(([a], [b]) => a.localeCompare(b)).map(([name, { dir, files }]) => {
        const meta = JSON.parse(readFileSync(join(dir, 'package.json'), 'utf8')) as { version: string; license: string };
        const text = licenceText(dir);
        if (!text) this.error(`${name}: no licence text to ship with the bundle`);
        const embedded = embeddedNotices(files);
        return `${name} ${meta.version} (${meta.license})\n\n${text}\n`
          + (embedded.length ? `\nNotices in the bundled files:\n\n${embedded.join('\n\n')}\n` : '');
      });
      this.emitFile({
        type: 'asset', fileName: 'third-party-licences.txt',
        source: 'Third-party software in this web application. The application itself is AGPL-3.0;\n'
          + 'see About for its Corresponding Source.\n\n' + notices.join('\n' + '-'.repeat(72) + '\n\n'),
      });
    },
  };
}
