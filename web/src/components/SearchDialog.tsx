import { useEffect, useMemo, useState } from 'react';
import type { PresetType } from '../api/types';
import { loadTab, modeVisible, OPTIONS, splitKey, TABS, isHidden, type OptionMode, type TabDesc } from '../options';
import { lineMode, tabEnvironment } from '../settings-env';
import { effectiveMode, getState, setState, useStore } from '../state/store';
import { condition, optionState } from '../visibility';
import { Icon } from './Icon';
import { Modal, ModalTitle } from './Modal';

const TYPES: [PresetType, TabDesc['tab'], string, string][] = [
  ['machine', 'printer', 'Printer', 'printer'], ['filament', 'filament', 'Filament', 'filament'], ['process', 'process', 'Process', 'process'],
];
const MODE_NAME: Record<OptionMode, string> = { simple: 'Simple', advanced: 'Advanced', develop: 'Expert', developer: 'Develop' };

interface Entry {
  type: PresetType;
  key: string;
  page: string;
  group: string;
  label: string;
  tooltip: string;
  mode: OptionMode;
  path: string;
}

function buildIndex(): Entry[] {
  const out: Entry[] = [];
  for (const [type, tab] of TYPES) {
    const seen = new Set<string>();
    for (const page of TABS[tab]?.pages ?? []) for (const group of page.groups) for (const line of group.lines) for (const k of line.options) {
      const key = splitKey(k)[0], def = OPTIONS[key];
      if (!def || isHidden(key) || seen.has(key)) continue;
      seen.add(key);
      const label = (line.options.length > 1 || line.index_labels || line.indexed_columns ? def.label || line.label : line.label ?? def.label) || def.full_label || key;
      const tooltip = (line.tooltip && OPTIONS[line.tooltip]?.tooltip) || def.tooltip || '';
      const parts = [page.title, group.title, label].filter((p, i, a) => p && p !== a[i - 1]);
      out.push({ type, key, page: page.title, group: group.title, label, tooltip, mode: lineMode(line, k) ?? def.mode, path: parts.join(' : ') });
    }
  }
  return out;
}

function fuzzy(pattern: string, text: string): { score: number; hits: number[] } | null {
  const p = pattern.toLowerCase(), t = text.toLowerCase();
  const hits: number[] = [];
  let score = 0, j = 0, run = 0;
  for (let i = 0; i < t.length && j < p.length; i++) {
    if (t[i] !== p[j]) { run = 0; continue; }
    const wordStart = i === 0 || /[\s:_\-(/]/.test(t[i - 1]);
    score += 10 + (run ? 15 * run : 0) + (wordStart ? 20 : 0);
    run++;
    hits.push(i);
    j++;
  }
  if (j < p.length) return null;
  score -= Math.min(30, hits[0]) + 3 * (hits[hits.length - 1] - hits[0] + 1 - hits.length);
  return { score, hits };
}

interface Found { entry: Entry; score: number; hits: number[]; via?: 'key' | 'tooltip'; reason?: string }

function search(index: Entry[], query: string): Found[] {
  const q = query.trim();
  if (!q) return [];
  const words = q.toLowerCase().split(/\s+/);
  const found: Found[] = [];
  for (const entry of index) {
    const own = fuzzy(q, entry.label), path = fuzzy(q, entry.path);
    const offset = entry.path.length - entry.label.length;
    const label = own && (!path || own.score >= path.score) ? { score: own.score, hits: own.hits.map((h) => h + offset) } : path;
    const at = entry.label.toLowerCase().indexOf(q.toLowerCase());
    const bonus = at < 0 ? 0 : 120 + (at === 0 ? 60 : 0) - entry.label.length;
    let best: Found | null = label && (label.score > 90 || at >= 0 || q.length < 4 && label.score > 40)
      ? { entry, score: label.score + bonus, hits: label.hits } : null;
    const key = entry.key.toLowerCase();
    if (words.every((w) => key.includes(w)) && (!best || best.score < 150)) best = { entry, score: 150 + (key === q.toLowerCase() ? 100 : 0), hits: [], via: 'key' };
    if (!best && q.length >= 3 && words.every((w) => entry.tooltip.toLowerCase().includes(w))) best = { entry, score: 40, hits: [], via: 'tooltip' };
    if (best) found.push(best);
  }
  return found.sort((a, b) => b.score - a.score || a.entry.path.localeCompare(b.entry.path)).slice(0, 60);
}

function Marked({ text, hits }: { text: string; hits: number[] }) {
  if (!hits.length) return <>{text}</>;
  const set = new Set(hits);
  return <>{[...text].map((c, i) => set.has(i) ? <b key={i}>{c}</b> : c)}</>;
}

function hiddenBySettings(entry: Entry, tab: TabDesc['tab']): string | undefined {
  const s = getState();
  const slot = s.dialog?.type === 'filament' ? s.dialog.slot : 0;
  const env = tabEnvironment(s, entry.type, slot, 'developer');
  const desc = TABS[tab]!;
  const page = desc.pages.find((p) => p.title === entry.page);
  const group = page?.groups.find((g) => g.title === entry.group);
  if ((page?.condition && condition(page.condition, env) === false) || (group?.condition && condition(group.condition, env) === false)
    || optionState({ ...desc, visibility: desc.visibility.filter((r) => r.options.some((o) => splitKey(o)[0] === entry.key)) }, env)[entry.key]?.visible === false)
    return 'hidden by settings';
  return undefined;
}

function reveal(entry: Entry, mode: OptionMode) {
  const s = getState();
  const patch: Parameters<typeof setState>[0] = { presetUx: null };
  if (!modeVisible(entry.mode, mode)) {
    if (entry.mode === 'developer') patch.developerMode = true;
    else patch.uiMode = entry.mode;
  }
  if (entry.type === 'process') Object.assign(patch, { dialog: null, tab: 'prepare', processScope: 'global' });
  else if (s.dialog?.type !== entry.type) patch.dialog = { type: entry.type, slot: 0 };
  setState({ ...patch, settingsFocus: { type: entry.type, page: entry.page, key: entry.key, nonce: Date.now() } });
}

export default function SearchDialog() {
  const mode = useStore(effectiveMode);
  const [ready, setReady] = useState(false);
  const [error, setError] = useState('');
  const [query, setQuery] = useState('');
  const [active, setActive] = useState(0);
  useEffect(() => {
    Promise.all(TYPES.map(([, tab]) => loadTab(tab))).then(() => setReady(true), (e) => setError(String(e)));
  }, []);
  const index = useMemo(() => (ready ? buildIndex() : []), [ready]);
  const results = useMemo(() => search(index, query).map((f) => {
    const tab = TYPES.find(([t]) => t === f.entry.type)![1];
    return { ...f, reason: modeVisible(f.entry.mode, mode) ? hiddenBySettings(f.entry, tab) : MODE_NAME[f.entry.mode] };
  }), [index, query, mode]);
  const close = () => setState({ presetUx: null });
  const choose = (f: Found | undefined) => f && reveal(f.entry, mode);

  return (
    <Modal className="np-dialog small search-dialog" label="Search settings" onClose={close} backdropClose>
      <ModalTitle title="Search settings" onClose={close} />
      <div className="np-dialog-body">
        <input className="text search-input" data-autofocus autoFocus placeholder="Search" aria-label="Search settings"
          value={query} role="combobox" aria-expanded={results.length > 0} aria-controls="search-results" aria-activedescendant={results.length ? `search-hit-${active}` : undefined}
          onChange={(e) => { setQuery(e.target.value); setActive(0); }}
          onKeyDown={(e) => {
            if (e.key === 'ArrowDown') setActive((a) => Math.min(a + 1, results.length - 1));
            else if (e.key === 'ArrowUp') setActive((a) => Math.max(a - 1, 0));
            else if (e.key === 'Enter') choose(results[active]);
            else return;
            e.preventDefault();
          }} />
        {error && <div className="np-error" role="alert">Settings failed to load: {error}</div>}
        {!ready && !error && <div role="status">Loading settings…</div>}
        {ready && query.trim() && !results.length && <div className="hint-text">No results</div>}
        <ul className="search-results" id="search-results" role="listbox" aria-label="Matching settings">
          {results.map((f, i) => (
            <li key={`${f.entry.type}:${f.entry.key}`} id={`search-hit-${i}`} role="option" aria-selected={i === active}
              className={`search-hit ${i === active ? 'active' : ''} ${f.reason ? 'dim' : ''}`}
              title={`${f.entry.tooltip}\n\nparameter name: ${f.entry.key}`.trim()}
              onMouseEnter={() => setActive(i)} onClick={() => choose(f)}>
              <Icon name={TYPES.find(([t]) => t === f.entry.type)![3]} size={16} />
              <span className="search-path"><Marked text={f.entry.path} hits={f.hits} /></span>
              <span className="search-meta">{f.via === 'key' ? f.entry.key : f.via === 'tooltip' ? 'tooltip' : ''}{f.reason && <em> · {f.reason}</em>}</span>
            </li>
          ))}
        </ul>
      </div>
    </Modal>
  );
}
