import type { Completion, CompletionContext, CompletionResult, CompletionSection } from '@codemirror/autocomplete';
import { OPTIONS } from '../options';
import data from './placeholders.json';

interface Item { key: string; label?: string; tooltip?: string; vector?: boolean }
const SPECIFIC = 'Specific to this G-code';
const specific = new Map(data.groups.find((g) => g.title === SPECIFIC)?.items.map((i: Item) => [i.key, i]) ?? []);
const hooks = data.hooks as Record<string, string[]>;

const hookOf = (key: string) => hooks[key] ? key : hooks[key.replace(/^(filament|process)_/, '')] ? key.replace(/^(filament|process)_/, '') : key;

export function placeholdersFor(optionKey: string): Map<string, Completion> {
  const out = new Map<string, Completion>();
  const sections = new Map<string, CompletionSection>();
  const section = (name: string) => sections.get(name) ?? sections.set(name, { name, rank: sections.size }).get(name)!;
  const add = (i: Item, group: string, boost = 0) => out.set(i.key, { label: i.key, type: 'variable', detail: i.label, info: [i.label, i.tooltip].filter(Boolean).join('\n\n') || undefined, section: section(group), boost });
  for (const key of hooks[hookOf(optionKey)] ?? []) { const i = specific.get(key); if (i) add(i, SPECIFIC, 2); }
  for (const g of data.groups) if (g.title !== SPECIFIC) for (const i of g.items as Item[]) if (!out.has(i.key)) add(i, g.title, 1);
  for (const [key, def] of Object.entries(OPTIONS)) if (!out.has(key)) add({ key, label: def.full_label || def.label, tooltip: def.tooltip }, 'Settings');
  return out;
}

export function placeholderCompletion(options: Map<string, Completion>) {
  const list = [...options.values()];
  return (context: CompletionContext): CompletionResult | null => {
    const word = context.matchBefore(/[A-Za-z_]\w*/);
    const line = context.state.doc.lineAt(context.pos);
    const before = line.text.slice(0, context.pos - line.from);
    const inExpr = before.lastIndexOf('{') > before.lastIndexOf('}') && !/;/.test(before.slice(0, before.lastIndexOf('{')));
    const inLegacy = /\[[A-Za-z_]?\w*$/.test(before);
    if (!inExpr && !inLegacy) return null;
    if (!word && !context.explicit && !/[{[]$/.test(before)) return null;
    return { from: word ? word.from : context.pos, options: list, validFor: /^\w*$/ };
  };
}
