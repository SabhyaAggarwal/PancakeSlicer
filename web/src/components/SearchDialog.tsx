import { useEffect, useMemo, useState } from 'react';
import type { PresetType } from '../api/types';
import { loadTab, modeVisible, OPTIONS, splitKey, TABS, isHidden, type OptionMode, type TabDesc } from '../options';
import { lineMode, tabEnvironment } from '../settings-env';
import { effectiveConfig, effectiveMode, getState, setState, useStore } from '../state/store';
import { condition, optionState } from '../visibility';
import { Icon } from './Icon';
import { Modal, ModalTitle } from './Modal';
import { display } from './PresetDialogs';

const TYPES: [PresetType, TabDesc['tab'], string, string][] = [
  ['machine', 'printer', 'Printer', 'printer'], ['filament', 'filament', 'Filament', 'filament'], ['process', 'process', 'Process', 'process'],
];
const MODE_NAME: Record<OptionMode, string> = { simple: 'Simple', advanced: 'Advanced', develop: 'Expert', developer: 'Develop' };
const GROUP_ORDER = ['', 'Simple', 'Advanced', 'Expert', 'Develop', 'In descriptions'];

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

const MODE_BOOST: Partial<Record<OptionMode, number>> = { simple: 80, advanced: 20 };
const PRIMARY: Record<string, string> = {
  layer: 'layer_height', wall: 'wall_loops', perimeter: 'wall_loops', infill: 'sparse_infill_density', density: 'sparse_infill_density',
  support: 'enable_support', supports: 'enable_support', brim: 'brim_type', raft: 'raft_layers', skirt: 'skirt_loops', seam: 'seam_position',
  ironing: 'ironing_type', top: 'top_shell_layers', bottom: 'bottom_shell_layers', temperature: 'nozzle_temperature', temp: 'nozzle_temperature',
  bed: 'curr_bed_type', retraction: 'retraction_length', fan: 'fan_max_speed', speed: 'outer_wall_speed', vase: 'spiral_mode', spiral: 'spiral_mode',
};

function search(index: Entry[], query: string): Found[] {
  const q = query.trim();
  if (!q) return [];
  const words = q.toLowerCase().split(/\s+/);
  const stem = q.length > 3 && /[^s]s$/i.test(q) ? q.slice(0, -1).toLowerCase() : '';
  const found: Found[] = [];
  for (const entry of index) {
    const own = fuzzy(q, entry.label), path = fuzzy(q, entry.path);
    const offset = entry.path.length - entry.label.length;
    const label = own && (!path || own.score >= path.score) ? { score: own.score, hits: own.hits.map((h) => h + offset) } : path;
    const lower = entry.label.toLowerCase();
    let at = lower.indexOf(q.toLowerCase());
    if (at < 0 && stem) at = lower.indexOf(stem);
    const bonus = at < 0 ? 0 : 120 + (at === 0 ? 60 : 0) - entry.label.length + (MODE_BOOST[entry.mode] ?? 0);
    const run = at < 0 ? null : Array.from({ length: lower.indexOf(q.toLowerCase()) === at ? q.length : stem.length }, (_, i) => offset + at + i);
    let best: Found | null = label && (label.score > 90 || at >= 0 || q.length < 4 && label.score > 40)
      ? { entry, score: label.score + bonus, hits: run ?? label.hits } : null;
    const key = entry.key.toLowerCase();
    if (words.every((w) => key.includes(w)) && (!best || best.score < 150)) best = { entry, score: 150 + (key === q.toLowerCase() ? 100 : 0), hits: [], via: 'key' };
    if (!best && q.length >= 3 && words.every((w) => entry.tooltip.toLowerCase().includes(w))) best = { entry, score: 40, hits: [], via: 'tooltip' };
    if (best && (PRIMARY[q.toLowerCase()] ?? PRIMARY[stem]) === entry.key) best.score += 400;
    if (best) found.push(best);
  }
  return found.sort((a, b) => b.score - a.score || a.entry.path.localeCompare(b.entry.path)).slice(0, 60);
}

function Marked({ text, hits }: { text: string; hits: number[] }) {
  const all = new Set(hits);
  const set = new Set(hits.filter((h) => all.has(h - 1) || all.has(h + 1)));
  if (!set.size) return <>{text}</>;
  return <>{[...text].map((c, i) => set.has(i) ? <b key={i}>{c}</b> : c)}</>;
}

const SearchValue = ({ text }: { text: string }) => <span className={`search-value ${/^[-+]?[\d.]/.test(text) ? 'num' : ''}`}>{text}</span>;

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
    const shown = modeVisible(f.entry.mode, mode);
    const group = f.via === 'tooltip' ? 'In descriptions' : shown ? '' : MODE_NAME[f.entry.mode];
    return { ...f, group, reason: shown ? hiddenBySettings(f.entry, tab) : f.via === 'tooltip' ? MODE_NAME[f.entry.mode] : undefined };
  }).sort((a, b) => GROUP_ORDER.indexOf(a.group) - GROUP_ORDER.indexOf(b.group)), [index, query, mode]);
  const configs = useMemo(() => {
    const s = getState();
    return Object.fromEntries(TYPES.map(([t]) => [t, effectiveConfig(s, t, t === 'filament' && s.dialog?.type === 'filament' ? s.dialog.slot : 0)]));
  }, [ready]);
  const valueOf = (e: Entry) => {
    const v = configs[e.type]?.[e.key] ?? OPTIONS[e.key]?.default;
    if (v === undefined || OPTIONS[e.key]?.multiline || /gcode|filament(_id)?$/.test(e.key)) return '';
    const text = OPTIONS[e.key]?.type.startsWith('bool') ? (display(e.key, v as never) === 'true' ? 'On' : 'Off') : display(e.key, v as never);
    const unit = OPTIONS[e.key]?.sidetext;
    return text.length > 24 ? '' : unit && !/^[a-z]/i.test(text) && !/%$/.test(text) && !text.endsWith(unit) && !/ or /.test(unit) ? `${text} ${unit}` : text;
  };
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
        {ready && query.trim() && !results.length && <div className="search-empty" role="status">No setting matches “{query.trim()}”</div>}
        <ul className="search-results" id="search-results" role="listbox" aria-label="Matching settings">
          {results.map((f, i) => [
            f.group && f.group !== results[i - 1]?.group && <li key={`group:${f.group}`} role="presentation" className="search-group">{f.group}</li>,
            <li key={`${f.entry.type}:${f.entry.key}`} id={`search-hit-${i}`} role="option" aria-selected={i === active} aria-description={f.group && f.via !== 'tooltip' ? `${f.group} setting` : undefined}
              className={`search-hit ${i === active ? 'active' : ''} ${f.reason || f.group ? 'dim' : ''}`}
              title={`${f.entry.tooltip}\n\nparameter name: ${f.entry.key}`.trim()}
              onMouseMove={() => active !== i && setActive(i)} onClick={() => choose(f)}>
              <Icon name={TYPES.find(([t]) => t === f.entry.type)![3]} size={16} />
              <span className="search-path"><Marked text={f.entry.path} hits={f.hits} /></span>
              <SearchValue text={valueOf(f.entry)} />
              {f.via === 'key' && <span className="search-meta">{f.entry.key}</span>}
              {f.reason && <span className="search-tag">{f.reason}</span>}
            </li>,
          ])}
        </ul>
      </div>
    </Modal>
  );
}
