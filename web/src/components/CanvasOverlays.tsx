import { MenuButton, type MenuEntry } from './Menu';
import { Glyph } from './Glyph';
import { Check } from './Check';
import { Range } from './Range';
import { slicingText } from '../api/progress';
import { accessNotice, accessSnapshot, saveAccessToken, subscribeAccess } from '../api/access';
import { Suspense, lazy, useEffect, useLayoutEffect, useMemo, useRef, useState, useSyncExternalStore, type ReactElement } from 'react';
import { createPortal } from 'react-dom';
import { addInstance, arrangeAll, cancelSlice, download, fileUse, openFileDialog, outputKind, removeInstance, toolbarOperation, outsideVolumeWarning, canBoolean, toggleBoolean, toggleSvg, toggleText } from '../actions';
import { setCompareWanted } from '../compare';
import { toggleNonplanarView, useNonplanarView, usePatchPreview, type PreviewFact } from '../patch-preview';
import { applyRecommended, pendingRecommendations, plateResults, type PlateResult } from '../recommend';
import { STYLE_NAMES } from '../style-names';
import { effectiveConfig, getState, mapPaint, printerPreset, setState, useStore, type Gizmo } from '../state/store';
import { Icon, IconButton } from './Icon';
import { Caret } from './Caret';
import { canPerformCut, cutBlocked, initialCut, setCut, toggleCut } from '../plater/cut';
import { InTopLayer } from './Modal';
import { useTier } from '../breakpoints';
import { SliceFailed } from './Notices';
import './prepare.css';

const OrientConfirm = lazy(() => import('./OrientConfirm').then((m) => ({ default: m.OrientConfirm })));
const RemapFilaments = lazy(() => import('./RemapFilaments'));
import { addPlate, canAddPlate } from '../plater/plates';
import { arrangeSettings, resetArrangeSettings, setArrangeSettings, subscribeArrangeSettings } from '../plater/arrangeSettings';
import { ANGLE, EXTRUDERS_LIMIT, GAP_AREA, HEIGHT, RADIUS, canPaint, gapFill, isPaint, paintOf, PAINT_STEP, PAINT_TOOLS, paintTool, setPaintTool, subscribePaintTool, togglePaint, type PaintKind } from '../plater/paint';
import { filamentColor } from '../plater/filamentColor';
import { canEditLayers } from './LayerHeight';
import { Num } from './Num';
import { setAssembly, useAssembly } from '../plater/assembly';

const PAINT_GIZMOS: [PaintKind, string, string][] = [
  ['support', 'toolbar_support', 'Support Painting (L)'],
  ['seam', 'toolbar_seam', 'Seam painting (P)'],
  ['fuzzy', 'toolbar_fuzzy_skin_paint', 'Paint-on fuzzy skin (H)'],
  ['color', 'mmu_segmentation', 'Color Painting (N)'],
];

const MOD = /Mac|iPhone|iPad/.test(navigator.platform) ? 'Cmd' : 'Ctrl';

export function CanvasToolbar() {
  const gizmo = useStore((s) => s.gizmo);
  const hasObjects = useStore((s) => s.objects.length > 0);
  const meshPlates = useStore(canAddPlate);
  const sel = useStore((s) => s.selected);
  const instances = useStore((s) => s.objects.find((o) => o.id === s.selected?.object)?.instances.length ?? 0);
  const project = useStore((s) => !!s.project);
  const toggle = (g: Gizmo) => setState({ gizmo: gizmo === g ? 'none' : g });
  const brimEars = useStore((s) => !!s.features.brim_ears);
  const booleanOk = useStore(canBoolean);
  const text = useStore((s) => !!s.features.emboss_text);
  const assemblyOn = useAssembly().on;
  const svg = useStore((s) => !!s.features.emboss_svg);
  const native = useStore((s) => !!s.objects.find((o) => o.id === s.selected?.object)?.native);
  const volumes = useStore((s) => !!s.objects.find((o) => o.id === s.selected?.object)?.volumes?.length);
  const locked = project ? ' — instances cannot be added or removed from an opened project' : '';
  const placeholder = useStore((s) => s.objects.find((o) => o.id === s.selected?.object)?.project?.placeholder);
  const edit = !!sel && !placeholder && !sel.all;
  const splitParts = useStore((s) => !!s.features.volumes && !s.objects.find((o) => o.id === s.selected?.object)?.volumes?.length);
  const cutBlock = useStore(cutBlocked);
  const paint = useStore(canPaint);
  const layers = useStore(canEditLayers);
  const preview = !!usePatchPreview();
  const nonplanarView = useNonplanarView();
  const [arranging, setArranging] = useState(false);
  const arrangeAt = useRef<HTMLSpanElement>(null);
  const rare: MenuEntry[] = [
    {label:'Auto orient',action:()=>toolbarOperation('orient'),disabled:!hasObjects || project},
    {label: 'Add plate', action: addPlate, disabled: !meshPlates},
    {label: 'Add instance', action: addInstance, disabled: !edit || project, tip: locked || undefined},
    {label: 'Remove instance', action: removeInstance, disabled: instances < 2 || project, tip: locked || undefined},
    {label: 'Split to objects', action: () => toolbarOperation('split'), disabled: !edit || project},
    {label: 'Split to parts', action: () => toolbarOperation('split-parts'), disabled: !edit || project || !splitParts},
    'sep',
    {label: 'Lay on face', shortcut: 'F', action: () => toggle('flatten'), disabled: !edit, checked: gizmo === 'flatten'},
    {label: 'Cut', shortcut: 'C', action: toggleCut, disabled: !!cutBlock && gizmo !== 'cut', tip: cutBlock ?? undefined},
    {label: 'Measure', shortcut: 'U', action: () => toggle('measure'), disabled: !sel || !!placeholder, checked: gizmo === 'measure'},
    {label: 'Variable layer height', action: () => toggle('layers'), disabled: !layers && gizmo !== 'layers'},
    {label: 'Assembly view', action: () => setAssembly({on: !assemblyOn}), disabled: !hasObjects && !assemblyOn},
    {label: 'Mesh boolean', shortcut: 'B', action: toggleBoolean, disabled: !booleanOk && gizmo !== 'boolean'},
    'sep',
    ...PAINT_GIZMOS.map(([kind, _icon, label]): MenuEntry => ({label, action: () => togglePaint(kind), disabled: !paint && gizmo !== kind, checked: gizmo === kind})),
    ...(brimEars ? [{label: 'Brim ears', action: () => toggle('brimears'), disabled: !sel || !!placeholder || !!native || volumes} as MenuEntry] : []),
    ...(text ? [{label: 'Emboss text', shortcut: 'T', action: toggleText, disabled: !!sel?.all || !!placeholder || !!native} as MenuEntry] : []),
    ...(svg ? [{label: 'SVG', action: toggleSvg, disabled: !!sel?.all || !!placeholder || !!native} as MenuEntry] : []),
    ...(preview ? [{label: 'Non-planar surfaces', action: toggleNonplanarView, checked: nonplanarView} as MenuEntry] : []),
  ];
  const needSelection = !hasObjects ? 'add an object' : sel?.all ? 'select one object' : 'select an object';
  return <div className="canvas-toolbar">
    <ToolButton icon="toolbar_move" name="Move" keys="M" active={gizmo === 'move'} unavailable={!edit && needSelection} onClick={() => toggle('move')}/>
    <ToolButton icon="toolbar_rotate" name="Rotate" keys="R" active={gizmo === 'rotate'} unavailable={!edit && needSelection} onClick={() => toggle('rotate')}/>
    <ToolButton icon="toolbar_scale" name="Scale" keys="S" active={gizmo === 'scale'} unavailable={!edit && needSelection} onClick={() => toggle('scale')}/>
    <span className="tb-sep"/>
    <span ref={arrangeAt} style={{display: 'inline-flex'}}><ToolButton icon="toolbar_arrange" name="Arrange" keys="A" active={arranging} unavailable={!hasObjects && !arranging && 'add an object'} onClick={() => setArranging(!arranging)}/></span>
    {arranging && <ArrangeOptions at={arrangeAt.current}/>}
    <ToolButton icon="toolbar_orient" name="Auto orient" keys="Q" unavailable={!hasObjects ? 'add an object' : project && 'not in an opened 3MF project'} onClick={e => toolbarOperation('orient', e.shiftKey)}/>
    <ToolButton icon="toolbar_open" name="Add model" keys={`${MOD}+I`} onClick={openFileDialog}/>
    <MenuButton className="btn-ghost canvas-more" label="Model tools" items={rare}>Tools<Caret/></MenuButton>
  </div>;
}

function ToolButton({ icon, name, keys, active = false, unavailable, onClick }: { icon: string; name: string; keys: string; active?: boolean; unavailable?: string | false; onClick(e: React.MouseEvent<HTMLButtonElement>): void }) {
  return <button type="button" className={`icon-btn tb-tool-btn${active ? ' active' : ''}`} aria-label={`${name} · ${keys}${unavailable ? ` (${unavailable})` : ''}`}
    aria-pressed={active} aria-disabled={unavailable ? true : undefined} onClick={(e) => { if (!unavailable) onClick(e); }}>
    <Icon name={icon} size={24} />
  </button>;
}

function ArrangeOptions({ at }: { at: HTMLElement | null }) {
  const a = useSyncExternalStore(subscribeArrangeSettings, arrangeSettings);
  const hasObjects = useStore((s) => s.objects.length > 0);
  const canMulti = useStore((s) => !!s.features.arrange_materials);
  const r = at?.getBoundingClientRect();
  return createPortal(
    <div className="manip-panel" role="dialog" aria-label="Arrange options" style={{ position: 'fixed', top: (r?.bottom ?? 40) + 8, left: r?.left ?? 0, zIndex: 5 }}>
      <div className="manip-grid" style={{ gridTemplateColumns: '70px 140px 70px' }}>
        <span>Spacing</span>
        <Range min={0} max={100} step={0.05} value={a.distance} onValueChange={(next) => setArrangeSettings({ distance: next })} />
        <Num value={a.distance} step={0.05} onCommit={(v) => setArrangeSettings({ distance: v })} />
        <span style={{ gridColumn: '1 / -1' }}>0 means auto spacing.</span>
        <span style={{ gridColumn: '1 / -1' }} className="menu-sep" />
        <label className="select-row" style={{ gridColumn: '1 / -1' }}>
          <Check checked={a.rotation} onCheckedChange={(next) => setArrangeSettings({ rotation: next })} />Auto rotate for arrangement
        </label>
        {canMulti && (
          <label className="select-row" style={{ gridColumn: '1 / -1' }}>
            <Check checked={a.multiMaterials} onCheckedChange={(next) => setArrangeSettings({ multiMaterials: next })} />Allow multiple materials on same plate
          </label>
        )}
        <span style={{ gridColumn: '1 / -1' }} className="menu-sep" />
      </div>
      <div style={{ display: 'flex', gap: 8, marginTop: 8 }}>
        <button className="btn primary" disabled={!hasObjects} onClick={() => arrangeAll()}>Arrange</button>
        <button className="btn" onClick={resetArrangeSettings}>Reset</button>
      </div>
    </div>,
    document.body,
  );
}

const cleanError = (msg = '') => msg.replace(/^(error:\s*)?(validate:\s*)?/i, '');

export function AccessNotification() {
  const notice = useSyncExternalStore(subscribeAccess, accessSnapshot);
  const [value, setValue] = useState('');
  if (!notice) return null;
  return (
    <div className={`notification ${notice.status === 429 || notice.status === 503 ? 'warning' : 'failed'}`} role="alert">
      <button className="notif-close icon-btn" aria-label="Close" onClick={() => accessNotice(null)}><Glyph name="close" size={14} /></button>
      <div className="notif-title">{notice.message}</div>
      {notice.status === 401 && (
        <form className="access-token-form" onSubmit={(event) => {
          event.preventDefault();
          saveAccessToken(value);
          setValue('');
          accessNotice(null);
        }}>
          <input type="password" aria-label="Access token" autoComplete="off" value={value} onChange={(event) => setValue(event.target.value)} />
          <button type="submit">Save</button>
          <div className="notif-body">Save the token, then try the action again.</div>
        </form>
      )}
      {notice.status === 507 && <div className="notif-body">Ask your administrator to free space, then try again.</div>}
    </div>
  );
}

function previewFact(facts: PreviewFact[]) {
  if (facts.length === 1) return facts[0].ledge ? 'No support under ledges' : facts[0].curved ? `Curved top ${facts[0].curved}%` : 'Flat top, prints as Standard';
  if (facts.some((f) => f.ledge)) return 'No support under ledges';
  const curved = facts.filter((f) => f.curved > 0).length;
  return curved ? `Curved tops on ${curved} of ${facts.length}` : 'Flat tops, prints as Standard';
}

function plateFact(results: PlateResult[]) {
  if (results.length === 1 || results[0].style === STYLE_NAMES.oneshot) return results[0].fact;
  if (results.some((r) => r.fact.startsWith('No support'))) return 'No support under ledges';
  const curved = results.filter((r) => r.curved).length;
  return curved ? `Curved tops on ${curved} of ${results.length}` : 'Flat tops, prints as Standard';
}

const TRANSIENT_MS = 8000;

export function SliceNotification() {
  const notice = useSyncExternalStore(subscribeAccess, accessSnapshot);
  const plateIndex = useStore((s) => s.plateIndex);
  const job = useStore((s) => s.job);
  const outside = useStore(outsideVolumeWarning);
  const [closed, setClosed] = useState<Set<string>>(new Set());
  const [expired, setExpired] = useState<Set<string>>(new Set());
  const brief = job.state === 'canceled' ? `canceled` : null;
  const done = job.state === 'done' && !job.stale && !job.exported ? `done:${job.id}` : null;
  const machine = useStore(printerPreset);
  const exportLabel = useStore(outputKind) === 'gcode.3mf' ? 'Export plate sliced file' : 'Export G-code file';
  const exported = job.exported ? `exported:${job.id}:${job.exported}` : null;
  useEffect(() => {
    const timers = ([[brief, 3000]] as const)
      .filter(([k]) => k)
      .map(([k, ms]) => setTimeout(() => setExpired((e) => new Set(e).add(k!)), ms));
    return () => timers.forEach(clearTimeout);
  }, [brief]);
  useEffect(() => setClosed(new Set()), [job.id]);
  const stack = useRef<HTMLDivElement>(null);
  const newest = useRef<Element>(undefined);
  useLayoutEffect(() => {
    const last = stack.current ? [...stack.current.querySelectorAll('.notification')].at(-1) : undefined;
    if (last !== newest.current && stack.current) stack.current.scrollTop = stack.current.scrollHeight;
    newest.current = last;
  });
  const busy = job.state === 'running' || job.state === 'submitting';
  const close = (k: string) => (
    <button className="notif-close icon-btn" aria-label="Close" onClick={() => setClosed((c) => new Set(c).add(k))}>
      <Glyph name="close" size={14} />
    </button>
  );
  const warnings = job.state === 'done' && !job.stale ? (job.warnings ?? []) : [];
  const sliced = useStore(plateResults);
  const patchPreview = usePatchPreview();
  const fresh = job.state === 'done' && !job.stale;
  const results = done ? sliced : [];
  const suggestIds = useStore((s) => pendingRecommendations(s).map((o) => o.id).join(','));
  const plateObjects = useStore((s) => s.objects);
  const orientPrompts = useStore((s) => s.orientPrompts);
  const orientConfirm = useStore((s) => s.orientConfirm !== null);
  const suggest = useMemo(() => plateObjects.filter((o) => suggestIds.split(',').includes(String(o.id))), [plateObjects, suggestIds]);
  const facts = !fresh && !busy && patchPreview ? patchPreview.facts.filter((f) => !suggestIds.split(',').includes(String(f.objectId))) : [];
  const factsKey = `np:${facts.map((f) => `${f.objectId}:${f.curved}`).join(',')}`;
  const exportLink = (label: string) => <button type="button" className="notif-link" onClick={download}>{label}</button>;
  const canSend = useStore((s) => s.printerConnections.enabled);
  const sendLink = canSend && <>{' · '}<button type="button" className="notif-link" onClick={() => setState({ appDialog: 'send' })}>Send</button></>;
  const [held, setHeld] = useState(false);
  const transient = [exported, done && (results.length ? `${done}:r` : done), facts.length ? factsKey : null,
    ...Object.keys(orientPrompts).map((id) => `orient${id}`)].filter((k): k is string => !!k && !expired.has(k));
  useEffect(() => {
    if (held) return;
    const timers = transient.map((k) => setTimeout(() => setExpired((e) => new Set(e).add(k)), TRANSIENT_MS));
    return () => timers.forEach(clearTimeout);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [transient.join('|'), held]);
  const compareLink = <button type="button" className="notif-link" onClick={() => (setCompareWanted(true), setState({ tab: 'preview' }))}>Compare</button>;
  const tier = useTier();
  const [tipsOpen, setTipsOpen] = useState(false);
  const tips = [
    ...Object.keys(orientPrompts).map((id) => {
      const o = plateObjects.find((x) => String(x.id) === id);
      return o && !expired.has(`orient${id}`) && (
        <div key={`orient${id}`} className="notification done">
          <button className="notif-close icon-btn" aria-label="Close" onClick={() => dismissOrientPrompt(o.id)}>
            <Glyph name="close" size={14} />
          </button>
          <div className="notif-title">{o.name}</div>
          <div className="notif-body">Stand this part up: less support · <button type="button" className="notif-link" onClick={() => void import('../plater/orientPrompt').then((m) => m.confirmOrientPrompt(o.id))}>Apply</button></div>
        </div>
      );
    }),
    exported && !expired.has(exported) && !closed.has(exported) && (
      <div key="exported" className="notification done">
        {close(exported)}
        <div className="notif-title">Exported successfully</div>
        <div className="notif-body">{job.exported}{'\n'}{fileUse(machine)}</div>
      </div>
    ),
    brief && !expired.has(brief) && (
      <div key="brief" className="notification done">
        <div className="notif-title">Slicing Canceled</div>
      </div>
    ),
    facts.length > 0 && !closed.has(factsKey) && !expired.has(factsKey) && (
      <div key="facts" className="notification done">
        {close(factsKey)}
        <div className="notif-title">{facts.length === 1 ? `${facts[0].name}: Auto` : 'Auto'}</div>
        <div className="notif-body">{previewFact(facts)}</div>
      </div>
    ),
    done && results.length > 0 && !closed.has(`${done}:r`) && !expired.has(`${done}:r`) && (
      <div key="results" className="notification done">
        {close(`${done}:r`)}
        <div className="notif-title">{results.length === 1 ? `${results[0].label}: ${results[0].style}` : results.every((r) => r.style === results[0].style) ? results[0].style : 'Auto'}</div>
        <div className="notif-body">
          {plateFact(results)}
          {results.some((r) => r.curved) && <> · {compareLink}</>}
          {' · '}{exportLink('Export')}{sendLink}
        </div>
      </div>
    ),
    done && !results.length && !closed.has(done) && !expired.has(done) && (
      <div key="done" className="notification done">
        {close(done)}
        <div className="notif-title"><Icon name="notification_slicing_complete" size={16} /> Slice complete</div>
        <div className="notif-body">
          <button type="button" className="notif-link" onClick={download}>{exportLabel}</button>{sendLink}
        </div>
      </div>
    ),
  ].filter((el): el is ReactElement => !!el);
  useEffect(() => { if (tips.length < 2) setTipsOpen(false); }, [tips.length]);
  return (
    <div className="notifications" ref={stack} onPointerEnter={() => setHeld(true)} onPointerLeave={() => setHeld(false)}>
      <InTopLayer>
        <div className="notif-group">
          <AccessNotification />
          {job.state === 'failed' && notice?.message !== cleanError(job.error) && !/Access token required|Server storage full|Server busy\./.test(job.error ?? '') && !closed.has('error') && (
            <SliceFailed error={cleanError(job.error)} onClose={() => setClosed((c) => new Set(c).add('error'))} />
          )}
        </div>
      </InTopLayer>
      {outside && (
        <div className="notification warning" role="alert">
          <div className="notif-title">Object outside the print volume</div>
          <div className="notif-body">{outside}</div>
        </div>
      )}
      {suggest.length > 0 && !closed.has(`pip${suggestIds}`) && (
        <div className="notification warning">
          {close(`pip${suggestIds}`)}
          <div className="notif-title">{suggest.length === 1 ? suggest[0].name : `${suggest.length} objects`}: Standard suggested</div>
          <div className="notif-body">Print-in-place gaps · <button type="button" className="notif-link" onClick={() => applyRecommended(suggest)}>Use Standard</button></div>
        </div>
      )}
      {orientConfirm && <Suspense fallback={null}><OrientConfirm /></Suspense>}
      {warnings.map((w, i) =>
        closed.has(`w${i}`) ? null : (
          <div key={`w${i}`} className="notification warning">
            {close(`w${i}`)}
            <div className="notif-title">Warning:</div>
            <div className="notif-body">{w}</div>
          </div>
        ),
      )}
      {busy && (
        <div className="notification running">
          <div className="notif-text">
            {slicingText(job, plateIndex)}
            <button className="notif-cancel icon-btn" aria-label="Cancel slicing" onClick={cancelSlice}>
              <Icon name="notification_cancel" size={16} />
            </button>
          </div>
          <div className="progress-row">
            <div className="progress" role="progressbar" aria-label={`Slicing plate ${plateIndex}`} aria-valuemin={0} aria-valuemax={100} aria-valuenow={Math.round(job.pct)}>
              <div className="progress-fill" style={{ width: `${job.pct}%` }} />
            </div>
          </div>
        </div>
      )}
      {tips.length > 1 && tier === 'phone' && (
        <button type="button" className="notif-chip" aria-expanded={tipsOpen} onClick={() => setTipsOpen(!tipsOpen)}>{tips.length} tips</button>
      )}
      {(tips.length < 2 || tier !== 'phone' || tipsOpen) && tips}
    </div>
  );
}

export { Toast } from './Notices';

const CutJoints = lazy(() => import('./CutJoints'));

export function CutPanel() {
  const cut = useStore((s) => (s.gizmo === 'cut' ? s.cut : null));
  const can = useStore(canPerformCut);
  const joints = useStore((s) => !!s.features.cut_connectors && !s.objects.find((o) => o.instances.some((i) => i.id === s.selected?.instance))?.project);
  if (!cut) return null;
  if (cut.editing) return <div className="manip-panel"><Suspense fallback={null}><CutJoints /></Suspense></div>;
  const joined = cut.mode === 'dovetail' || !!cut.connectors?.length;
  const initial = initialCut(getState());
  const part = (label: string, colour: string, keep: boolean, onKeep: (v: boolean) => void, place: boolean, onPlace?: (v: boolean) => void) => (
    <>
      <span style={{ display: 'flex', alignItems: 'center', gap: 4 }}>
        <span style={{ width: 10, height: 10, background: colour, flex: 'none' }} />{label}
      </span>
      <label className="select-row" style={{ whiteSpace: 'nowrap' }}><Check checked={keep} disabled={joined} onCheckedChange={(next) => onKeep(next)} />Keep</label>
      <label className="select-row" style={{ whiteSpace: 'nowrap' }}>
        <Check checked={keep && place} disabled={!keep || !onPlace} onCheckedChange={(next) => onPlace?.(next)} />Place on cut
      </label>
    </>
  );
  return (
    <div className="manip-panel">
      <div className="manip-title">Cut</div>
      <div className="manip-grid">
        <span>Cut position</span>
        <Num value={cut.z} onCommit={(z) => setCut({ z })} />
        <span>mm</span>
        <span />
        <span>Rotation</span>
        <Num value={cut.rotX} onCommit={(rotX) => setCut({ rotX })} />
        <Num value={cut.rotY} onCommit={(rotY) => setCut({ rotY })} />
        <span>°</span>
      </div>
      {joints && <Suspense fallback={null}><CutJoints /></Suspense>}
      <div className="manip-grid">
        <span style={{ gridColumn: '1 / -1' }} className="menu-sep" />
        <span style={{ gridColumn: '1 / -1' }}>After cut:</span>
      </div>
      <label className="select-row" style={{ whiteSpace: 'nowrap' }}>
        <Check checked={!!cut.toParts} disabled={joined} onCheckedChange={(next) => setCut({ toParts: next })} />Cut to parts
      </label>
      {!cut.toParts && (
        <div style={{ display: 'grid', gridTemplateColumns: 'auto auto auto', gap: '0 12px', alignItems: 'center' }}>
          {part('Upper part', '#00ffff', cut.keepUpper, (keepUpper) => setCut({ keepUpper }), true)}
          {part('Lower part', '#ff00ff', cut.keepLower, (keepLower) => setCut({ keepLower }), cut.placeLower, (placeLower) => setCut({ placeLower }))}
          {cut.keepUpper && <label className="select-row" style={{ gridColumn: '2 / span 2', whiteSpace: 'nowrap' }}>
            <Check checked={!!cut.flipUpper} onCheckedChange={(next) => setCut({ flipUpper: next })} />Flip upper part upside down
          </label>}
        </div>
      )}
      <div style={{ display: 'flex', gap: 8, marginTop: 8 }}>
        <button className="btn" onClick={() => void import('../plater/cutJoints').then((m) => setCut(m.flipped(getState(), cut)))}>Flip</button>
        <button className="btn" disabled={!initial || (cut.z === initial.z && !cut.rotX && !cut.rotY && !cut.connectors?.length)}
          onClick={() => initial && setCut({ z: initial.z, rotX: 0, rotY: 0, connectors: [] })}>Reset</button>
        <span style={{ flex: 1 }} />
        <button className="btn primary" disabled={!can} onClick={() => void import('../plater/cutPerform').then((m) => m.performCut())}>Perform cut</button>
        <button className="btn" onClick={() => setState({ gizmo: 'none', cut: null })}>Cancel</button>
      </div>
    </div>
  );
}

const PAINT_TITLE: Record<PaintKind, string> = { support: 'Support Painting', seam: 'Seam painting', fuzzy: 'Paint-on fuzzy skin', color: 'Color Painting' };

export function PaintPanel() {
  const kind = useStore((s) => (isPaint(s.gizmo) ? s.gizmo : null));
  const object = useStore((s) => s.objects.find((o) => o.id === s.selected?.object));
  const t = useSyncExternalStore(subscribePaintTool, paintTool);
  const threshold = useStore((s) => object && kind === 'support' ? Number({ ...effectiveConfig(s, 'process'), ...object.settings }.support_threshold_angle) || 0 : 0);
  useEffect(() => { if (kind === 'support') setPaintTool({ highlight: threshold }); }, [kind, object?.id, threshold]);
  const filaments = useStore((s) => s.filamentSlots.map((_, i) => filamentColor(s, i + 1)).join(' ')).split(' ').filter(Boolean);
  useEffect(() => { if (kind && !PAINT_TOOLS[kind].some(([id]) => id === paintTool().tool)) setPaintTool({ tool: 'circle' }); }, [kind, t.tool]);
  useEffect(() => { if (t.filament > filaments.length) setPaintTool({ filament: 1 }); }, [t.filament, filaments.length]);
  if (!kind || !object) return null;
  const tools = PAINT_TOOLS[kind];
  const tool = tools.some(([id]) => id === t.tool) ? t.tool : 'circle';
  const fill = tool === 'fill', gap = tool === 'gap', height = tool === 'height', triangle = tool === 'triangle';
  const painted = !!paintOf(object, kind) || !!object.volumes?.some((v) => v.type === 'part' && paintOf(v, kind));
  return (
    <div className="manip-panel">
      <div className="manip-title">{PAINT_TITLE[kind]}</div>
      <div className="manip-grid" style={{ gridTemplateColumns: 'auto auto auto' }}>
        {kind === 'color' && (
          <>
            <span>Filaments</span>
            <span style={{ display: 'flex', gap: 4, flexWrap: 'wrap', gridColumn: 'span 2' }}>
              {filaments.slice(0, EXTRUDERS_LIMIT).map((c, i) => (
                <button key={i} type="button" className={`paint-filament ${t.filament === i + 1 ? 'active' : ''}`} style={{ background: c }}
                  aria-label={`Filament ${i + 1}`} aria-pressed={t.filament === i + 1} onClick={() => setPaintTool({ filament: i + 1 })}>{i + 1}</button>
              ))}
            </span>
          </>
        )}
        {kind === 'color' && filaments.length > 1 && <span style={{ gridColumn: '1 / -1' }}><Suspense fallback={null}><RemapFilaments object={object} colours={filaments} /></Suspense></span>}
        <span>Tool type</span>
        <span style={{ display: 'flex', gap: 2, gridColumn: 'span 2' }}>
          {tools.map(([id, icon, title]) => <IconButton key={id} icon={icon} title={title} size={20} active={tool === id} onClick={() => setPaintTool({ tool: id })} />)}
        </span>
        {gap ? (
          <>
            <span>Gap area</span>
            <Range min={GAP_AREA.min} max={GAP_AREA.max} step={GAP_AREA.step} value={t.gapArea} onValueChange={(next) => setPaintTool({ gapArea: next })} />
            <Num value={t.gapArea} step={GAP_AREA.step} onCommit={(v) => setPaintTool({ gapArea: v })} />
            <span />
            <button className="btn" style={{ gridColumn: 'span 2', justifySelf: 'start' }} disabled={!(t.gapArea > 0) || !painted}
              onClick={() => mapPaint(object.id, kind, PAINT_STEP[kind], (x, scale) => gapFill(x.geometry, paintOf(x, kind), t.gapArea / (scale * scale)))}>Apply</button>
          </>
        ) : triangle ? null : height ? (
          <>
            <span>Height range</span>
            <Range min={HEIGHT.min} max={HEIGHT.max} step={HEIGHT.step} value={t.height} onValueChange={(next) => setPaintTool({ height: next })} />
            <Num value={t.height} step={HEIGHT.step} onCommit={(v) => setPaintTool({ height: v })} />
          </>
        ) : fill && kind === 'color' && !t.edges ? null : (
          <>
            <span>{fill ? 'Smart fill angle' : 'Brush size'}</span>
            <Range min={fill ? ANGLE.min : RADIUS.min} max={fill ? ANGLE.max : RADIUS.max} step={fill ? ANGLE.step : RADIUS.step}
              value={fill ? t.angle : t.radius} onValueChange={(next) => setPaintTool(fill ? { angle: next } : { radius: next })} />
            <Num value={fill ? t.angle : t.radius} step={fill ? ANGLE.step : RADIUS.step} onCommit={(v) => setPaintTool(fill ? { angle: v } : { radius: v })} />
          </>
        )}
        {kind === 'color' && fill && (
          <label className="select-row" style={{ gridColumn: '1 / -1' }}>
            <Check checked={t.edges} onCheckedChange={(next) => setPaintTool({ edges: next })} />Edge detection
          </label>
        )}
        {kind === 'support' && !gap && (
          <label className="select-row" style={{ gridColumn: '1 / -1' }}>
            <Check checked={t.overhangsOnly} onCheckedChange={(next) => setPaintTool({ overhangsOnly: next })} />On highlighted overhangs only
          </label>
        )}
        {kind === 'support' && (
          <>
            <span style={{ gridColumn: '1 / -1' }} className="menu-sep" />
            <span>Highlight overhangs</span>
            <Range min={0} max={90} step={1} value={t.highlight} aria-label="Highlight overhangs" onValueChange={(next) => setPaintTool({ highlight: next })} />
            <Num value={t.highlight} step={1} onCommit={(v) => setPaintTool({ highlight: v })} />
          </>
        )}
      </div>
      <div style={{ display: 'flex', gap: 8, marginTop: 8 }}>
        <button className="btn" disabled={!painted} onClick={() => mapPaint(object.id, kind, PAINT_STEP[kind], () => null)}>Erase all</button>
        <button className="btn primary" onClick={() => setState({ gizmo: 'none' })}>Done</button>
      </div>
    </div>
  );
}

function dismissOrientPrompt(id: number) {
  setState((st) => {
    const orientPrompts = { ...st.orientPrompts };
    delete orientPrompts[id];
    return { orientPrompts, orientDismissed: [...st.orientDismissed, id] };
  });
}
