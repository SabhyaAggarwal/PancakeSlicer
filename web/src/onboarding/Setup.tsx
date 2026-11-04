import { useEffect, useMemo, useState, type ReactNode } from 'react';
import type { SystemPreset } from '../api/types';
import { requestPreset } from '../components/PresetCombo';
import { Glyph } from '../components/Glyph';
import { getState, setState, toast, useStore } from '../state/store';
import { savePreferences } from '../saved-preferences';
import { catalogKey, profilesFor } from '../profiles/model';
import { createProfileFromBase } from '../profiles/catalog';
import { selectCompatibility } from '../profiles/compatibility';
import { archiveProfiles } from '../profiles/transactions';
import { profileKey } from '../profiles/identity';
import { nozzleOf } from '../profiles/machines';
import { discoverPrinters, saveConnection, testConnection, type ConnectionEdit, type ConnectionKind, type ConnectionTest, type Discovered } from '../printers/connections';
import { setup, TOUR_PAGES, useSetup } from './setup-flow';
import { FacetGroup, FacetsHead } from '../components/Facets';
import { Check } from '../components/Check';
import { materialRank } from '../profiles/Pickers';
import './setup.css';

const send = setup.send;
const first = (v: unknown) => String([v].flat()[0] ?? '');
const VENDOR_NAMES: Record<string, string> = { BBL: 'Bambu Lab' };
const vendorName = (id: string, name = id) => VENDOR_NAMES[id] ?? name;

interface PrinterModel { id: string; vendor: string; vendorName: string; model: string; cover?: string; nozzles: number[]; machines: Record<string, string>; presets: SystemPreset[] }
interface Choices { model: PrinterModel | null; nozzles: number[]; profiles: Set<string> | null; connection: ConnectionEdit | null; filaments: Set<string> | null }
let choices: Choices = { model: null, nozzles: [], profiles: null, connection: null, filaments: null };

interface CatalogBody { vendors: { id: string; name: string; models: { id: string; name: string; cover?: string; nozzles: string[]; machines?: Record<string, string> }[] }[] }
let catalogRead: Promise<PrinterModel[]> | null = null;
function printerCatalog(): Promise<PrinterModel[]> {
  return catalogRead ??= fetch('/api/printer-catalog').then((r) => { if (!r.ok) throw new Error(`${r.status} ${r.statusText}`); return r.json() as Promise<CatalogBody>; })
    .then((b) => b.vendors.flatMap((v) => v.models.map((m) => ({ id: `${v.id}|${m.id}`, vendor: v.id, vendorName: vendorName(v.id, v.name), model: m.name, cover: m.cover,
      nozzles: m.nozzles.map(Number).filter((n) => n > 0), machines: m.machines ?? {}, presets: [] }))))
    .catch((e) => { catalogRead = null; throw e; });
}
async function loadModel(m: PrinterModel, nozzles: number[]): Promise<SystemPreset[]> {
  const mine = () => getState().machines.filter((p) => p.vendor === m.vendor && (p.model ? p.model === m.model : /^(.*?)\s+[\d.]+\s*nozzle$/i.exec(p.name)?.[1] === m.model));
  if (!nozzles.every((n) => mine().some((p) => Math.abs(nozzleOf(p) - n) < 1e-6))) {
    const api = getState().api;
    if (!api) throw new Error('The slicing service is not connected.');
    const names = nozzles.map((n) => m.machines[String(n)] ?? Object.entries(m.machines).find(([k]) => Number(k) === n)?.[1]).filter((x): x is string => !!x);
    if (!names.length) throw new Error(`No ${m.model} preset for ${nozzles.join(', ')} mm.`);
    const cat = await api.presets(m.vendor, [], undefined, names);
    const union = (current: SystemPreset[], incoming: SystemPreset[]) => { const keys = new Set(current.map(catalogKey)); return [...current, ...incoming.filter((p) => !keys.has(catalogKey(p)))]; };
    setState((st) => ({ machines: union(st.machines, cat.machines), processes: union(st.processes, cat.processes), filaments: union(st.filaments, cat.filaments) }));
  }
  return mine();
}

const STEPS = ['printer', 'profiles', 'connect', 'filaments'] as const;
function Frame({ step, children, foot }: { step: string; children: ReactNode; foot: ReactNode }) {
  const at = STEPS.indexOf(step as (typeof STEPS)[number]);
  return (
    <div className="setup" role="dialog" aria-modal="true" aria-label="Setup">
      <div className="setup-panel">
        <header className="setup-head">
          <img src="/logo-48.png" width={24} height={24} alt="" />
          {at >= 0 && <div className="setup-dots" aria-label={`Step ${at + 1} of ${STEPS.length}`}>{STEPS.map((s, i) => <span key={s} className={i <= at ? 'on' : ''} />)}</div>}
          <button type="button" className="icon-btn setup-close" aria-label="Close setup" onClick={() => send({ type: 'CLOSE' })}><Glyph name="close" /></button>
        </header>
        <div className="setup-body">{children}</div>
        <footer className="setup-foot">{foot}</footer>
      </div>
    </div>
  );
}
const Back = () => <button type="button" className="btn-ghost" onClick={() => send({ type: 'BACK' })}>Back</button>;
const Next = ({ disabled, label = 'Next', onClick }: { disabled?: boolean; label?: string; onClick?(): void }) =>
  <button type="button" className="btn-primary" disabled={disabled} onClick={onClick ?? (() => send({ type: 'NEXT' }))}>{label}</button>;

function Welcome() {
  return <Frame step="welcome" foot={<><button type="button" className="btn-ghost" onClick={() => send({ type: 'SKIP' })}>Skip to setup</button><span className="setup-grow" /><Next label="Take the tour" /></>}>
    <div className="setup-hero"><img src="/logo-256.png" width={112} height={112} alt="" /><h1>Welcome to PancakeSlicer</h1></div>
  </Frame>;
}
interface TourFacts { 'curved-flat'?: { time?: string }; 'curved-tops'?: { time?: string } }
let tourFacts: Promise<TourFacts> | null = null;
const Shot = ({ src, caption }: { src: string; caption: string }) => (
  <figure className="setup-shot"><img src={src} alt={caption} /><figcaption>{caption}</figcaption></figure>);
type Point = string | [label: string, text: string];
const TOUR: { title: string; points: Point[]; art: (f: TourFacts) => ReactNode }[] = [
  { title: 'Curved tops', points: [],
    art: (f) => <><Shot src="/tour/curved-flat.webp" caption={`Flat layers${f['curved-flat']?.time ? ` · ${f['curved-flat'].time}` : ''}`} />
      <Shot src="/tour/curved-tops.webp" caption={`Curved tops${f['curved-tops']?.time ? ` · ${f['curved-tops'].time}` : ''}`} /></> },
  { title: 'Tune what matters. The rest is automatic.', points: [
    ['You tune', 'walls, infill, supports, ironing, how the top looks.'],
    ['Automatic', 'anything that follows from your nozzle and layer height (line widths, layer counts, support gaps), and anything your printer needs (speeds, accelerations, elephant-foot), because each printer carries its own values.'],
    ['One set of profiles', 'the same Everyday works on every nozzle, layer height and printer, with no copies to keep in sync. Values worked out for you show ✦; type over any of them. Prefer the classic way? Normal profiles still work.']],
    art: () => <Shot src="/tour/dynamic.webp" caption="Everyday: formulas worked out for the nozzle and layer height" /> },
  { title: 'Your workflow', points: [],
    art: () => <div className="setup-stack"><Shot src="/tour/take-control.webp" caption="Open in another tab" /><Shot src="/tour/projects.webp" caption="Projects, on any device" /></div> },
];
function Tour({ page }: { page: number }) {
  const t = TOUR[page];
  const [facts, setFacts] = useState<TourFacts>({});
  useEffect(() => { void (tourFacts ??= fetch('/tour/tour.json').then((r) => r.ok ? r.json() : {}).catch(() => ({}))).then(setFacts); }, []);
  return <Frame step="tour" foot={<><Back /><button type="button" className="btn-ghost" onClick={() => send({ type: 'SKIP' })}>Skip</button><span className="setup-grow" />
    <span className="setup-count">{page + 1}/{TOUR_PAGES}</span><Next /></>}>
    <div className="setup-tour">
      <div className="setup-tour-art">{t.art(facts)}</div>
      <div className="setup-tour-text"><h2>{t.title}</h2>{t.points.map((x) => typeof x === 'string' ? <p key={x}>{x}</p> : <p key={x[0]}><b>{x[0]}:</b> {x[1]}</p>)}</div>
    </div>
  </Frame>;
}

function Printer() {
  const [models, setModels] = useState<PrinterModel[] | null>(null);
  const [failed, setFailed] = useState<string | null>(null);
  useEffect(() => { void printerCatalog().then(setModels, (e) => setFailed(e instanceof Error ? e.message : String(e))); }, []);
  const vendors = useMemo(() => [...new Map((models ?? []).map((m) => [m.vendor, m.vendorName])).entries()].sort((a, b) => a[1].localeCompare(b[1])), [models]);
  const [vendor, setVendor] = useState<string | null>(choices.model?.vendor ?? 'BBL');
  const [search, setSearch] = useState('');
  const [model, setModel] = useState<PrinterModel | null>(choices.model);
  const [nozzles, setNozzles] = useState<number[]>(choices.nozzles);
  const [busy, setBusy] = useState(false);
  const q = search.trim().toLowerCase();
  const shown = (models ?? []).filter((m) => q ? `${m.vendorName} ${m.model}`.toLowerCase().includes(q) : m.vendor === vendor);
  const pick = (m: PrinterModel) => { setModel(m); setNozzles(m.nozzles.includes(0.4) ? [0.4] : m.nozzles.slice(0, 1)); };
  const toggle = (n: number) => setNozzles((list) => list.includes(n) ? list.filter((x) => x !== n) : [...list, n].sort((a, b) => a - b));
  const next = async () => {
    if (!model) return;
    setBusy(true);
    try { choices = { ...choices, model: { ...model, presets: await loadModel(model, nozzles) }, nozzles }; send({ type: 'NEXT' }); }
    catch (e) { setFailed(e instanceof Error ? e.message : String(e)); }
    finally { setBusy(false); }
  };
  return <Frame step="printer" foot={<><Back /><span className="setup-grow" />{failed && <span className="setup-test bad">{failed}</span>}
    {model && <span className="setup-count">{model.model} · {nozzles.join(', ')} mm</span>}<Next label={busy ? 'Loading…' : 'Next'} disabled={!model || !nozzles.length || busy} onClick={() => void next()} /></>}>
    <h2>Your printer</h2>
    <input className="text setup-search" type="search" placeholder="Search printers" aria-label="Search printers" value={search} onChange={(e) => setSearch(e.target.value)} />
    <div className="setup-printer">
      {!q && <div className="setup-brands" role="tablist" aria-label="Brand">{vendors.map(([id, name]) =>
        <button key={id} type="button" role="tab" aria-selected={id === vendor} className="setup-brand" onClick={() => setVendor(id)}>{name}</button>)}</div>}
      <div className="setup-models">{!models && !failed && <span className="setup-count">Loading printers…</span>}{shown.map((m) => (
        <button key={m.id} type="button" className="setup-model" aria-pressed={model?.id === m.id} onClick={() => pick(m)}>
          {m.cover ? <img src={m.cover} alt="" loading="lazy" onError={(e) => { (e.currentTarget as HTMLImageElement).style.visibility = 'hidden'; }} /> : <span className="setup-model-blank" />}
          <span>{q ? `${m.vendorName} ` : ''}{m.model}</span>
        </button>
      ))}</div>
    </div>
    {model && model.nozzles.length > 0 && <div className="setup-row"><span className="setup-k">Nozzle</span><div className="setup-chips">{model.nozzles.map((n) =>
      <button key={n} type="button" className="setup-chip" aria-pressed={nozzles.includes(n)} onClick={() => toggle(n)}>{n} mm</button>)}</div></div>}
  </Frame>;
}

function Profiles() {
  const users = useStore((s) => s.userPresets);
  const rows = useMemo(() => users.filter((p) => p.type === 'process' && !p.archived && (p.kind ?? 'profile') === 'profile'), [users]);
  const [on, setOn] = useState<Set<string>>(() => choices.profiles ?? new Set(rows.map((p) => profileKey(p))));
  const flip = (k: string) => setOn((s) => { const n = new Set(s); if (n.has(k)) n.delete(k); else n.add(k); return n; });
  const next = () => { choices = { ...choices, profiles: on }; send({ type: 'NEXT' }); };
  return <Frame step="profiles" foot={<><Back /><span className="setup-grow" /><span className="setup-count">{on.size} selected</span><Next disabled={!on.size} onClick={next} /></>}>
    <h2>Print profiles</h2>
    <div className="setup-list">{rows.map((p) => { const k = profileKey(p); return (
      <label key={k} className="setup-check"><input type="checkbox" checked={on.has(k)} onChange={() => flip(k)} /><span>{p.name}</span></label>); })}</div>
  </Frame>;
}

const KINDS: [ConnectionKind, string][] = [['bambu-lan', 'Bambu LAN'], ['moonraker', 'Moonraker'], ['octoprint', 'OctoPrint']];
function Connect() {
  const [edit, setEdit] = useState<ConnectionEdit>(choices.connection ?? { kind: 'bambu-lan', name: 'Printer' });
  const [test, setTest] = useState<ConnectionTest | 'testing' | null>(null);
  const [found, setFound] = useState<Discovered[] | 'looking' | string | null>(null);
  const discover = async () => {
    setFound('looking');
    try { const r = await discoverPrinters(); setFound(r.printers.length ? r.printers : 'No printers found on this network'); }
    catch (e) { setFound(e instanceof Error ? e.message : String(e)); }
  };
  const use = (d: Discovered) => { setEdit((e) => ({ ...e, kind: d.kind, name: d.name ?? e.name, host: d.host, url: d.url, serial: d.serial, model: d.model })); setTest(null); };
  const set = (patch: Partial<ConnectionEdit>) => { setEdit((e) => ({ ...e, ...patch })); setTest(null); };
  const field = (key: keyof ConnectionEdit, label: string, type = 'text') => <label className="setup-field"><span className="setup-k">{label}</span>
    <input className="text" type={type} value={String(edit[key] ?? '')} onChange={(e) => set({ [key]: e.target.value })} /></label>;
  const runTest = async () => { setTest('testing'); try { setTest(await testConnection('', edit)); } catch (e) { setTest({ ok: false, stage: 'error', message: e instanceof Error ? e.message : String(e) }); } };
  const filled = edit.kind === 'bambu-lan' ? !!(edit.host && edit.serial && edit.access_code) : !!edit.url;
  const next = () => { choices = { ...choices, connection: filled ? edit : null }; send({ type: 'NEXT' }); };
  return <Frame step="connect" foot={<><Back /><button type="button" className="btn-ghost" onClick={() => { choices = { ...choices, connection: null }; send({ type: 'SKIP' }); }}>Skip</button><span className="setup-grow" /><Next disabled={!filled} onClick={next} /></>}>
    <h2>Connect your printer</h2>
    <div className="setup-row">
      <button type="button" className="btn" disabled={found === 'looking'} onClick={() => void discover()}>{found === 'looking' ? 'Looking…' : 'Find printers'}</button>
      {typeof found === 'string' && found !== 'looking' && <span className="setup-count">{found}</span>}
    </div>
    {Array.isArray(found) && <div className="setup-found">{found.map((d, i) => (
      <button key={`${d.host ?? d.url}-${i}`} type="button" className="setup-chip" aria-pressed={!!d.host && d.host === edit.host || !!d.url && d.url === edit.url} onClick={() => use(d)}>
        {d.name ?? d.model ?? d.host ?? d.url} <span className="setup-n">{d.host ?? d.url}</span></button>))}</div>}
    <div className="setup-chips" role="radiogroup" aria-label="Connection">{KINDS.map(([k, l]) =>
      <button key={k} type="button" role="radio" aria-checked={edit.kind === k} className="setup-chip" onClick={() => set({ kind: k })}>{l}</button>)}</div>
    <div className="setup-form">
      {edit.kind === 'bambu-lan' ? <>{field('host', 'IP address')}{field('serial', 'Serial number')}{field('access_code', 'Access code', 'password')}</>
        : <>{field('url', 'Address')}{edit.kind === 'octoprint' && field('key', 'API key', 'password')}</>}
    </div>
    <div className="setup-row">
      <button type="button" className="btn" disabled={!filled || test === 'testing'} onClick={() => void runTest()}>{test === 'testing' ? 'Testing…' : 'Test'}</button>
      {test && test !== 'testing' && <span className={`setup-test ${test.ok ? 'ok' : 'bad'}`} role="status">{test.ok ? `Connected${test.model ? ` · ${test.model}` : ''}` : test.message}</span>}
    </div>
  </Frame>;
}

interface FilamentRow { key: string; name: string; brand: string; material: string }
const baseMaterial = (m: string) => { const b = m.toUpperCase().split(/[-+ ]/)[0].replace(/\d+$/, ''); return b || m; };
function Filaments({ error }: { error: string | null }) {
  const filaments = useStore((s) => s.filaments);
  const printer = choices.model ? (choices.model.presets.find((p) => nozzleOf(p) === choices.nozzles[0]) ?? choices.model.presets[0]) : undefined;
  const rows = useMemo<FilamentRow[]>(() => {
    const s = getState();
    const all = profilesFor(s, 'filament').map((p) => ({ key: catalogKey(p), name: p.name.replace(/\s*@.*$/, ''), brand: first(p.config.filament_vendor) || vendorName(p.vendor), material: first(p.config.filament_type) || 'Other' }));
    const fit = printer ? all.filter((r) => selectCompatibility(s, 'filament', r.key, printer.name, choices.nozzles[0]).ok) : all;
    const seen = new Set<string>();
    return (fit.length ? fit : all).filter((r) => !seen.has(r.name) && !!seen.add(r.name)).sort((a, b) => a.name.localeCompare(b.name));
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [filaments]);
  const home = choices.model ? choices.model.vendorName.split(' ')[0] : '';
  const counted = (values: string[]) => [...values.reduce((m, v) => m.set(v, (m.get(v) ?? 0) + 1), new Map<string, number>())].map(([value, count]) => ({ value, label: value, count }));
  const brandOptions = useMemo(() => counted(rows.map((r) => r.brand)).sort((a, b) => a.label.localeCompare(b.label)), [rows]);
  const materialOptions = useMemo(() => counted(rows.map((r) => baseMaterial(r.material))).sort((a, b) => materialRank(a.value) - materialRank(b.value) || a.value.localeCompare(b.value)), [rows]);
  const [brands, setBrands] = useState<string[]>(() => { const b = brandOptions.find((o) => home && o.value.startsWith(home)); return b ? [b.value] : []; });
  const [materials, setMaterials] = useState<string[]>([]);
  const [search, setSearch] = useState('');
  const [filtersOpen, setFiltersOpen] = useState(false);
  const [on, setOn] = useState<Set<string>>(() => choices.filaments ?? new Set(['PLA', 'PETG'].map((m) => {
    const of = (brandRows: FilamentRow[]) => brandRows.filter((r) => r.material === m).sort((a, b) => Number(/basic/i.test(b.name)) - Number(/basic/i.test(a.name)) || a.name.length - b.name.length)[0];
    return (home && of(rows.filter((r) => r.brand.startsWith(home)))) || of(rows.filter((r) => /^generic\b/i.test(r.name)));
  }).filter((r): r is FilamentRow => !!r).map((r) => r.key)));
  const q = search.trim().toLowerCase();
  const shown = rows.filter((r) => (!brands.length || brands.includes(r.brand)) && (!materials.length || materials.includes(baseMaterial(r.material)))
    && (!q || `${r.brand} ${r.name} ${r.material}`.toLowerCase().includes(q)));
  const picked = shown.filter((r) => on.has(r.key)).length;
  const setMany = (keys: string[], value: boolean) => setOn((s) => { const n = new Set(s); keys.forEach((k) => value ? n.add(k) : n.delete(k)); return n; });
  const apply = () => { choices = { ...choices, filaments: on }; send({ type: 'APPLY' }); void applyChoices(); };
  const active = brands.length + materials.length;
  return <Frame step="filaments" foot={<><Back /><span className="setup-grow" />{error && <span className="setup-test bad">{error}</span>}<span className="setup-count">{on.size} selected</span><Next label="Done" disabled={!on.size} onClick={apply} /></>}>
    <h2>Your filaments</h2>
    <div className={`setup-filaments${filtersOpen ? ' filters-open' : ''}`}>
      <aside className="facets setup-facets" aria-label="Filters">
        <FacetsHead active={active} onClear={() => { setBrands([]); setMaterials([]); }} />
        <FacetGroup label="Brand" options={brandOptions} value={brands} onChange={setBrands} />
        <FacetGroup label="Material" options={materialOptions} value={materials} onChange={setMaterials} />
        <button type="button" className="btn setup-show" onClick={() => { setFiltersOpen(false); requestAnimationFrame(() => document.querySelector<HTMLElement>('.setup-filters-btn')?.focus()); }}>Show {shown.length}</button>
      </aside>
      <section className="setup-results" aria-label="Filaments">
        <div className="setup-results-tools">
          <button type="button" className="btn setup-filters-btn" onClick={() => setFiltersOpen(true)}><Glyph name="filter" />{active ? `Filters · ${active}` : 'Filters'}</button>
          <input className="text setup-search" type="search" placeholder="Search filaments" aria-label="Search filaments" value={search} onChange={(e) => setSearch(e.target.value)} />
        </div>
        <label className="setup-check setup-all"><Check checked={picked === 0 ? false : picked === shown.length ? true : 'indeterminate'} disabled={!shown.length}
          onCheckedChange={() => setMany(shown.map((r) => r.key), picked < shown.length)} /><span>Select all shown</span><span className="setup-n">{shown.length}</span></label>
        <div className="setup-grid">{shown.map((r) => (
          <label key={r.key} className="setup-check"><Check checked={on.has(r.key)} onCheckedChange={(v) => setMany([r.key], v)} /><span>{r.name}</span><span className="setup-n">{r.material}</span></label>
        ))}{!shown.length && <span className="setup-count">No filaments match</span>}</div>
      </section>
    </div>
  </Frame>;
}

async function applyChoices() {
  try {
    const { model, nozzles, profiles, connection, filaments } = choices;
    let printerKey: string | null = null;
    if (model) for (const n of nozzles) {
      const preset = model.presets.find((p) => Math.abs(nozzleOf(p) - n) < 1e-6);
      if (!preset) continue;
      const key = await createProfileFromBase('machine', catalogKey(preset));
      if (!printerKey) { printerKey = key; requestPreset('machine', 0, key); }
    }
    if (profiles) {
      const s = getState();
      const drop = s.userPresets.filter((p) => p.type === 'process' && !p.archived && (p.kind ?? 'profile') === 'profile' && !profiles.has(profileKey(p))).map((p) => profileKey(p));
      const keep = [...profiles][0];
      const current = s.userPresets.find((p) => p.type === 'process' && (p.name === s.process || profileKey(p) === s.process));
      if (keep && current && drop.includes(profileKey(current))) requestPreset('process', 0, keep);
      if (drop.length) await archiveProfiles('process', drop);
    }
    if (connection && printerKey) await saveConnection(printerKey, { ...connection, name: model?.model ?? 'Printer' }).catch((e) => toast(`Connection not saved: ${e instanceof Error ? e.message : e}`));
    let firstFilament: string | null = null;
    const base = model?.presets.find((p) => Math.abs(nozzleOf(p) - nozzles[0]) < 1e-6)?.name;
    const names = (p: SystemPreset) => [p.config.compatible_printers].flat().map(String);
    const now = getState(), stem = (name: string) => name.replace(/\s*@.*$/, '');
    const picked = new Set([...(filaments ?? [])].map((key) => stem(profilesFor(now, 'filament').find((p) => catalogKey(p) === key)?.name ?? key)));
    for (const name of picked) {
      const variants = profilesFor(getState(), 'filament').filter((p) => stem(p.name) === name);
      const fit = (base && variants.find((p) => names(p).includes(base))) || variants.find((p) => !names(p).filter(Boolean).length && selectCompatibility(getState(), 'filament', catalogKey(p)).ok);
      if (!fit) continue;
      const made = await createProfileFromBase('filament', catalogKey(fit));
      firstFilament ??= made;
    }
    if (firstFilament) requestPreset('filament', 0, firstFilament);
    send({ type: 'APPLIED' });
  } catch (e) { send({ type: 'FAILED', error: e instanceof Error ? e.message : String(e) }); }
}

function Done() {
  const finish = () => { savePreferences({ 'orca.setup': '1' }); setState({ tab: 'prepare' }); send({ type: 'CLOSE' }); };
  return <Frame step="done" foot={<><Back /><span className="setup-grow" /><Next label="Start printing" onClick={finish} /></>}>
    <div className="setup-hero"><img src="/logo-256.png" width={96} height={96} alt="" /><h1>Ready</h1></div>
  </Frame>;
}

export default function Setup() {
  const { value, context } = useSetup();
  useEffect(() => {
    if (value === 'closed') return;
    const before = document.activeElement as HTMLElement | null;
    const panel = () => document.querySelector<HTMLElement>('.setup');
    const behind = [...(panel()?.parentElement?.children ?? [])].filter((el): el is HTMLElement => el instanceof HTMLElement && !el.classList.contains('setup') && !el.inert);
    behind.forEach((el) => { el.inert = true; });
    const trap = (e: KeyboardEvent) => {
      const root = panel();
      if (e.key !== 'Tab' || !root) return;
      const all = [...root.querySelectorAll<HTMLElement>('button:not(:disabled),input:not(:disabled),[tabindex]:not([tabindex="-1"]),a[href]')].filter((el) => el.offsetParent !== null);
      if (!all.length) return;
      const firstEl = all[0], lastEl = all[all.length - 1], at = document.activeElement;
      if (!root.contains(at)) { e.preventDefault(); firstEl.focus(); }
      else if (e.shiftKey && at === firstEl) { e.preventDefault(); lastEl.focus(); }
      else if (!e.shiftKey && at === lastEl) { e.preventDefault(); firstEl.focus(); }
    };
    document.addEventListener('keydown', trap, true);
    return () => { document.removeEventListener('keydown', trap, true); behind.forEach((el) => { el.inert = false; }); before?.focus?.(); };
  }, [value === 'closed']);
  useEffect(() => {
    if (value === 'closed') return;
    const root = document.querySelector<HTMLElement>('.setup');
    const visible = (el: HTMLElement) => el.offsetParent !== null;
    ([...(root?.querySelectorAll<HTMLElement>('.setup-body input, .setup-body button') ?? [])].find(visible) ?? root?.querySelector<HTMLElement>('.setup-foot .btn-primary'))?.focus();
  }, [value, context.page]);
  useEffect(() => {
    if (value === 'closed') return;
    const onKey = (e: KeyboardEvent) => { if (e.key === 'Escape' && value !== 'applying') send({ type: 'CLOSE' }); };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, [value]);
  useEffect(() => { if (value === 'closed') choices = { model: null, nozzles: [], profiles: null, connection: null, filaments: null }; }, [value]);
  switch (value) {
    case 'closed': return null;
    case 'welcome': return <Welcome />;
    case 'tour': return <Tour page={context.page} />;
    case 'printer': return <Printer />;
    case 'profiles': return <Profiles />;
    case 'connect': return <Connect />;
    case 'filaments': return <Filaments error={context.error} />;
    case 'applying': return <Frame step="filaments" foot={<span className="setup-count">Setting up…</span>}><div className="setup-hero"><span className="setup-spin" /></div></Frame>;
    case 'done': return <Done />;
  }
}
