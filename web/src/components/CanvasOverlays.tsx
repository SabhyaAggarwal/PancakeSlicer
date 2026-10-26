import { slicingText } from '../api/progress';
import { accessNotice, accessSnapshot, saveAccessToken, subscribeAccess } from '../api/access';
import { Suspense, lazy, useEffect, useLayoutEffect, useMemo, useRef, useState, useSyncExternalStore } from 'react';
import { createPortal } from 'react-dom';
import * as THREE from 'three';
import { addInstance, arrangeAll, cancelSlice, download, fileUse, openFileDialog, outputKind, removeInstance, toolbarOperation, outsideVolumeWarning, canBoolean, toggleBoolean, toggleSvg, toggleText } from '../actions';
import { setCompareWanted } from '../compare';
import { toggleNonplanarView, useNonplanarView, usePatchPreview, type PreviewFact } from '../patch-preview';
import { applyRecommended, pendingRecommendations, plateResults, type PlateResult } from '../recommend';
import { STYLE_NAMES } from '../style-names';
import { effectiveConfig, getState, mapPaint, printerPreset, setState, updateInstance, updateObject, updateVolume, useStore, type Gizmo } from '../state/store';
import { instanceMatrix, volumeBox } from '../plater/geometry';
import { Icon, IconButton } from './Icon';
import { canPerformCut, cutBlocked, initialCut, setCut, toggleCut } from '../plater/cut';
import { InTopLayer } from './Modal';

const OrientConfirm = lazy(() => import('./OrientConfirm').then((m) => ({ default: m.OrientConfirm })));
const RemapFilaments = lazy(() => import('./RemapFilaments'));
import { addPlate, canAddPlate } from '../plater/plates';
import { arrangeSettings, resetArrangeSettings, setArrangeSettings, subscribeArrangeSettings } from '../plater/arrangeSettings';
import { ANGLE, EXTRUDERS_LIMIT, GAP_AREA, HEIGHT, RADIUS, canPaint, gapFill, isPaint, paintOf, PAINT_STEP, paintTool, setPaintTool, subscribePaintTool, togglePaint, type BrushTool, type PaintKind } from '../plater/paint';
import { filamentColor } from '../plater/filamentColor';
import { canEditLayers } from './LayerHeight';
import { setAssembly, useAssembly } from '../plater/assembly';

const LATER_GIZMOS: [icon: string, tooltip: string][] = [
  ['toolbar_texture_displacement', 'Texture displacement'],
  ['toolbar_text', 'Emboss (T)'],
  ['svg_part', 'SVG'],
  ['toolbar_assembly', 'Assemble (Y)'],
  ['toolbar_brimears', 'Brim Ears (E)'],
];

const PAINT_GIZMOS: [PaintKind, string, string][] = [
  ['support', 'toolbar_support', 'Support Painting (L)'],
  ['seam', 'toolbar_seam', 'Seam painting (P)'],
  ['fuzzy', 'toolbar_fuzzy_skin_paint', 'Paint-on fuzzy skin (H)'],
  ['color', 'mmu_segmentation', 'Color Painting (N)'],
];

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
  return (
    <div className="canvas-toolbar">
      <IconButton size={24} icon="toolbar_open" title="Add (Ctrl+I)" onClick={openFileDialog} />
      <IconButton size={24} icon="toolbar_add_plate" title="Add plate" onClick={addPlate} disabled={!meshPlates} />
      <IconButton size={24} icon="toolbar_orient" title={"Auto orient all/selected objects (Q)\nAuto orient all objects on current plate (Shift+Q)"} onClick={(e) => toolbarOperation('orient', e.shiftKey)} disabled={!hasObjects || project} />
      <span ref={arrangeAt} style={{ display: 'inline-flex' }}>
        <IconButton size={24} icon="toolbar_arrange" title={`Arrange all objects (A)\nArrange objects on selected plates (Shift+A)`} active={arranging} onClick={() => setArranging(!arranging)} disabled={!hasObjects && !arranging} />
      </span>
      {arranging && <ArrangeOptions at={arrangeAt.current} />}
      <span className="tb-sep" />
      <IconButton size={24} icon="instance_add" title={`Add instance (+)${locked}`} onClick={addInstance} disabled={!edit || project} />
      <IconButton size={24} icon="instance_remove" title={`Remove instance (-)${locked}`} onClick={removeInstance} disabled={instances < 2 || project} />
      <IconButton size={24} icon="split_objects" title="Split to objects" onClick={() => toolbarOperation('split')} disabled={!edit || project} />
      <IconButton size={24} icon="split_parts" title="Split to parts" onClick={() => toolbarOperation('split-parts')} disabled={!edit || project || !splitParts} />
      <IconButton size={24} icon="toolbar_variable_layer_height" title="Variable layer height" active={gizmo === 'layers'} onClick={() => toggle('layers')} disabled={!layers && gizmo !== 'layers'} />
      <IconButton size={24} icon="toolbar_assemble" title="Assembly View" active={assemblyOn} onClick={() => setAssembly({ on: !assemblyOn })} disabled={!hasObjects && !assemblyOn} />
      <span className="tb-sep" />
      <IconButton size={24} icon="toolbar_move" title="Move (M)" active={gizmo === 'move'} onClick={() => toggle('move')} disabled={!edit} />
      <IconButton size={24} icon="toolbar_rotate" title="Rotate (R)" active={gizmo === 'rotate'} onClick={() => toggle('rotate')} disabled={!edit} />
      <IconButton size={24} icon="toolbar_scale" title="Scale (S)" active={gizmo === 'scale'} onClick={() => toggle('scale')} disabled={!edit} />
      <IconButton size={24} icon="toolbar_flatten" title="Lay on Face (F)" active={gizmo === 'flatten'} onClick={() => toggle('flatten')} disabled={!edit} />
      <IconButton size={24} icon="toolbar_cut" title={cutBlock ? `Cut (C) — ${cutBlock}` : 'Cut (C)'} active={gizmo === 'cut'} onClick={toggleCut} disabled={!!cutBlock && gizmo !== 'cut'} />
      <IconButton size={24} icon="toolbar_measure" title="Measure (U)" active={gizmo === 'measure'} onClick={() => toggle('measure')} disabled={!sel || !!placeholder} />
      <IconButton size={24} icon="toolbar_meshboolean" title="Mesh Boolean (B)" active={gizmo === 'boolean'} onClick={toggleBoolean} disabled={!booleanOk && gizmo !== 'boolean'} />
      {PAINT_GIZMOS.map(([kind, icon, title]) => (
        <IconButton key={kind} size={24} icon={icon} title={title} active={gizmo === kind} onClick={() => togglePaint(kind)} disabled={!paint && gizmo !== kind} />
      ))}
      {LATER_GIZMOS.map(([icon, title]) => (icon === 'toolbar_brimears' && brimEars
        ? <IconButton key={icon} size={24} icon={icon} title={title} active={gizmo === 'brimears'} onClick={() => toggle('brimears')} disabled={!sel || !!placeholder || !!native || volumes} />
        : icon === 'toolbar_text' && text
          ? <IconButton key={icon} size={24} icon={icon} title={title} active={gizmo === 'text'} onClick={toggleText} disabled={!!sel?.all || !!placeholder || !!native} />
          : icon === 'svg_part' && svg
            ? <IconButton key={icon} size={24} icon={icon} title={title} active={gizmo === 'svg'} onClick={toggleSvg} disabled={!!sel?.all || !!placeholder || !!native} />
            : <IconButton key={icon} size={24} icon={icon} title={title} disabled />
      ))}
      {preview && (
        <>
          <span className="tb-sep" />
          <IconButton size={24} icon="param_nonplanar" title="Non-planar surfaces" active={nonplanarView} onClick={toggleNonplanarView} />
        </>
      )}
    </div>
  );
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
        <input type="range" min={0} max={100} step={0.05} value={a.distance} onChange={(e) => setArrangeSettings({ distance: Number(e.target.value) })} />
        <Num value={a.distance} step={0.05} onCommit={(v) => setArrangeSettings({ distance: v })} />
        <span style={{ gridColumn: '1 / -1' }}>0 means auto spacing.</span>
        <span style={{ gridColumn: '1 / -1' }} className="menu-sep" />
        <label className="select-row" style={{ gridColumn: '1 / -1' }}>
          <input type="checkbox" checked={a.rotation} onChange={(e) => setArrangeSettings({ rotation: e.target.checked })} />Auto rotate for arrangement
        </label>
        {canMulti && (
          <label className="select-row" style={{ gridColumn: '1 / -1' }}>
            <input type="checkbox" checked={a.multiMaterials} onChange={(e) => setArrangeSettings({ multiMaterials: e.target.checked })} />Allow multiple materials on same plate
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

function Num({ value, onCommit, step = 1 }: { value: number; onCommit: (v: number) => void; step?: number }) {
  const [text, setText] = useState(value.toFixed(2));
  useEffect(() => setText(value.toFixed(2)), [value]);
  return (
    <input
      className="text num manip-num"
      value={text}
      step={step}
      onChange={(e) => setText(e.target.value)}
      onBlur={() => Number.isFinite(Number(text)) && onCommit(Number(text))}
      onKeyDown={(e) => e.key === 'Enter' && (e.target as HTMLInputElement).blur()}
    />
  );
}

export function ManipulationPanel({ docked = false }: { docked?: boolean }) {
  const gizmo = useStore((s) => s.gizmo);
  const o = useStore((s) => s.objects.find((ob) => ob.id === s.selected?.object));
  const instId = useStore((s) => s.selected?.instance);
  const uniform = useStore((s) => s.uniformScale);
  const volumeId = useStore((s) => s.selected?.volume);
  const inst = o?.instances.find((i) => i.id === instId);
  if (!o || !inst || (docked ? gizmo !== 'none' : gizmo === 'none') || gizmo === 'flatten' || gizmo === 'cut' || gizmo === 'measure' || isPaint(gizmo) || gizmo === 'layers' || gizmo === 'brimears' || gizmo === 'boolean' || gizmo === 'text' || gizmo === 'svg' || o.project?.placeholder) return null;
  const v = o.volumes?.find((vv) => vv.id === volumeId);
  if (v) {
    const box = volumeBox(o, inst, v);
    const centre = box.getCenter(new THREE.Vector3()), size = box.getSize(new THREE.Vector3());
    const change = (m: THREE.Matrix4, name: string) => updateVolume(o.id, v.id,
      { matrix: instanceMatrix(o, inst).invert().multiply(m).multiply(instanceMatrix(o, inst)).multiply(new THREE.Matrix4().fromArray(v.matrix)).elements }, name);
    const move = (axis: number, value: number) => change(new THREE.Matrix4().makeTranslation(...[0, 1, 2].map((k) => (k === axis ? value - centre.getComponent(k) : 0)) as [number, number, number]), 'Move');
    const resize = (axis: number, value: number) => {
      const f = [0, 1, 2].map((k) => (uniform || k === axis ? Math.max(0.01, value) / Math.max(1e-6, size.getComponent(axis)) : 1));
      change(new THREE.Matrix4().makeTranslation(centre.x, centre.y, centre.z).multiply(new THREE.Matrix4().makeScale(f[0], f[1], f[2]))
        .multiply(new THREE.Matrix4().makeTranslation(-centre.x, -centre.y, -centre.z)), 'Scale');
    };
    return (
      <div className={`manip-panel${docked ? ' docked' : ''}`}>
        <div className="manip-title">{v.name}</div>
        <div className="manip-grid">
          <span />
          <span className="axis x">X</span>
          <span className="axis y">Y</span>
          <span className="axis z">Z</span>
          <span>Position</span>
          {[0, 1, 2].map((a) => <Num key={a} value={centre.getComponent(a)} onCommit={(x) => move(a, x)} />)}
          <span>Size mm</span>
          {[0, 1, 2].map((a) => <Num key={a} value={size.getComponent(a)} onCommit={(x) => resize(a, x)} />)}
          {(gizmo === 'scale' || docked) && (
            <label className="manip-lock">
              <input type="checkbox" checked={uniform} onChange={(e) => setState({ uniformScale: e.target.checked })} />
              Uniform scale
            </label>
          )}
        </div>
      </div>
    );
  }
  const mesh = o.geometry.boundingBox!.getSize(new THREE.Vector3());
  const rescale = (axis: number, factor: number) => updateObject(o.id, {
    scale: o.scale.map((v, k) => (uniform || k === axis ? v * factor : v)) as [number, number, number],
  });
  return (
    <div className={`manip-panel${docked ? ' docked' : ''}`}>
      <div className="manip-title">{docked ? 'Object manipulation' : gizmo === 'move' ? 'Move' : gizmo === 'rotate' ? 'Rotate' : 'Scale'}</div>
      <div className="manip-grid">
        <span />
        <span className="axis x">X</span>
        <span className="axis y">Y</span>
        <span className="axis z">Z</span>
        <span>Position</span>
        <Num value={inst.x} onCommit={(x) => updateInstance(o.id, inst.id, { x })} />
        <Num value={inst.y} onCommit={(y) => updateInstance(o.id, inst.id, { y })} />
        <Num value={inst.z ?? 0} onCommit={(z) => updateInstance(o.id, inst.id, { z })} />
        <span>Rotation</span>
        <Num value={o.rotX} onCommit={(rotX) => updateObject(o.id, { rotX })} />
        <Num value={o.rotY} onCommit={(rotY) => updateObject(o.id, { rotY })} />
        <Num value={inst.rotZ} onCommit={(rotZ) => updateInstance(o.id, inst.id, { rotZ })} />
        <span>Scale %</span>
        {[0, 1, 2].map((a) => (
          <Num key={a} value={Math.abs(o.scale[a]) * 100} onCommit={(v) => rescale(a, Math.max(0.1, v) / 100 / Math.abs(o.scale[a]))} />
        ))}
        <span>Size mm</span>
        {(['x', 'y', 'z'] as const).map((a, k) => (
          <Num key={a} value={Math.abs(o.scale[k]) * mesh[a]} onCommit={(v) => rescale(k, Math.max(0.01, v) / Math.max(1e-6, Math.abs(o.scale[k]) * mesh[a]))} />
        ))}
        {(gizmo === 'scale' || docked) && (
          <label className="manip-lock">
            <input type="checkbox" checked={uniform} onChange={(e) => setState({ uniformScale: e.target.checked })} />
            Uniform scale
          </label>
        )}
      </div>
    </div>
  );
}

const cleanError = (msg = '') => msg.replace(/^(error:\s*)?(validate:\s*)?/i, '');

export function AccessNotification() {
  const notice = useSyncExternalStore(subscribeAccess, accessSnapshot);
  const [value, setValue] = useState('');
  if (!notice) return null;
  return (
    <div className={`notification ${notice.status === 429 || notice.status === 503 ? 'warning' : 'failed'}`} role="alert">
      <button className="notif-close icon-btn" title="Close" onClick={() => accessNotice(null)}><Icon name="notification_close" size={14} /></button>
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
    const timers = ([[brief, 3000], [exported, 20000]] as const)
      .filter(([k]) => k)
      .map(([k, ms]) => setTimeout(() => setExpired((e) => new Set(e).add(k!)), ms));
    return () => timers.forEach(clearTimeout);
  }, [brief, exported]);
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
    <button className="notif-close icon-btn" title="Close" onClick={() => setClosed((c) => new Set(c).add(k))}>
      <Icon name="notification_close" size={14} />
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
  const exportLink = (label: string) => <button type="button" className="notif-link" title={exportLabel} onClick={download}>{label}</button>;
  const compareLink = <button type="button" className="notif-link" onClick={() => (setCompareWanted(true), setState({ tab: 'preview' }))}>Compare</button>;
  return (
    <div className="notifications" ref={stack}>
      <InTopLayer>
        <div className="notif-group">
          <AccessNotification />
          {job.state === 'failed' && notice?.message !== cleanError(job.error) && !/Access token required|Server storage full|Server busy\./.test(job.error ?? '') && !closed.has('error') && (
            <div className="notification failed">
              {close('error')}
              <div className="notif-title">Error:</div>
              <div className="notif-body">{cleanError(job.error)}</div>
            </div>
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
      {Object.keys(orientPrompts).map((id) => {
        const o = plateObjects.find((x) => String(x.id) === id);
        return o && (
          <div key={`orient${id}`} className="notification done">
            <button className="notif-close icon-btn" title="Close" onClick={() => dismissOrientPrompt(o.id)}>
              <Icon name="notification_close" size={14} />
            </button>
            <div className="notif-title">{o.name}</div>
            <div className="notif-body">Stand this part up: less support · <button type="button" className="notif-link" onClick={() => void import('../plater/orientPrompt').then((m) => m.confirmOrientPrompt(o.id))}>Apply</button></div>
          </div>
        );
      })}
      {warnings.map((w, i) =>
        closed.has(`w${i}`) ? null : (
          <div key={`w${i}`} className="notification warning">
            {close(`w${i}`)}
            <div className="notif-title">Warning:</div>
            <div className="notif-body">{w}</div>
          </div>
        ),
      )}
      {exported && !expired.has(exported) && !closed.has(exported) && (
        <div className="notification done">
          {close(exported)}
          <div className="notif-title">Exported successfully</div>
          <div className="notif-body">{job.exported}{'\n'}{fileUse(machine)}</div>
        </div>
      )}
      {busy && (
        <div className="notification running">
          <div className="notif-text">
            {slicingText(job, plateIndex)}
            <button className="notif-cancel icon-btn" title="Cancel" onClick={cancelSlice}>
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
      {brief && !expired.has(brief) && (
        <div className="notification done">
          <div className="notif-title">Slicing Canceled</div>
        </div>
      )}
      {facts.length > 0 && !closed.has(factsKey) && (
        <div className="notification done">
          {close(factsKey)}
          <div className="notif-title">{facts.length === 1 ? `${facts[0].name}: Auto` : 'Auto'}</div>
          <div className="notif-body">{previewFact(facts)}</div>
        </div>
      )}
      {done && results.length > 0 && !closed.has(`${done}:r`) && (
        <div className="notification done">
          {close(`${done}:r`)}
          <div className="notif-title">{results.length === 1 ? `${results[0].label}: ${results[0].style}` : results.every((r) => r.style === results[0].style) ? results[0].style : 'Auto'}</div>
          <div className="notif-body">
            {plateFact(results)}
            {results.some((r) => r.curved) && <> · {compareLink}</>}
            {' · '}{exportLink('Export')}
          </div>
        </div>
      )}
      {done && !results.length && !closed.has(done) && (
        <div className="notification done">
          {close(done)}
          <div className="notif-title"><Icon name="notification_slicing_complete" size={16} /> Slice complete</div>
          <div className="notif-body">
            <button type="button" className="notif-link" onClick={download}>{exportLabel}</button>
          </div>
        </div>
      )}
    </div>
  );
}

export function Toast() {
  const msg = useStore((s) => s.toast ?? s.projectNotice);
  return msg ? <InTopLayer><div className="toast" role="status">{msg}</div></InTopLayer> : null;
}

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
      <label className="select-row" style={{ whiteSpace: 'nowrap' }}><input type="checkbox" checked={keep} disabled={joined} onChange={(e) => onKeep(e.target.checked)} />Keep</label>
      <label className="select-row" style={{ whiteSpace: 'nowrap' }} title={onPlace ? undefined : 'The upper part is always placed on its cut face'}>
        <input type="checkbox" checked={keep && place} disabled={!keep || !onPlace} onChange={(e) => onPlace?.(e.target.checked)} />Place on cut
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
      <label className="select-row" style={{ whiteSpace: 'nowrap' }} title="Both pieces stay one object, the upper piece a part, where they stand">
        <input type="checkbox" checked={!!cut.toParts} disabled={joined} onChange={(e) => setCut({ toParts: e.target.checked })} />Cut to parts
      </label>
      {!cut.toParts && (
        <div style={{ display: 'grid', gridTemplateColumns: 'auto auto auto', gap: '0 12px', alignItems: 'center' }}>
          {part('Upper part', '#00ffff', cut.keepUpper, (keepUpper) => setCut({ keepUpper }), true)}
          {part('Lower part', '#ff00ff', cut.keepLower, (keepLower) => setCut({ keepLower }), cut.placeLower, (placeLower) => setCut({ placeLower }))}
          {cut.keepUpper && <label className="select-row" style={{ gridColumn: '2 / span 2', whiteSpace: 'nowrap' }}>
            <input type="checkbox" checked={!!cut.flipUpper} onChange={(e) => setCut({ flipUpper: e.target.checked })} />Flip upper part upside down
          </label>}
        </div>
      )}
      <div style={{ display: 'flex', gap: 8, marginTop: 8 }}>
        <button className="btn" title="Flip cut plane" onClick={() => void import('../plater/cutJoints').then((m) => setCut(m.flipped(getState(), cut)))}>Flip</button>
        <button className="btn" title="Reset cutting plane and remove connectors" disabled={!initial || (cut.z === initial.z && !cut.rotX && !cut.rotY && !cut.connectors?.length)}
          onClick={() => initial && setCut({ z: initial.z, rotX: 0, rotY: 0, connectors: [] })}>Reset</button>
        <span style={{ flex: 1 }} />
        <button className="btn primary" disabled={!can} onClick={() => void import('../plater/cutPerform').then((m) => m.performCut())}>Perform cut</button>
        <button className="btn" onClick={() => setState({ gizmo: 'none', cut: null })}>Cancel</button>
      </div>
    </div>
  );
}

const PAINT_TOOLS: Record<PaintKind, [BrushTool, string, string][]> = {
  support: [['circle', 'circle_paint', 'Circle'], ['sphere', 'toolbar_modifier_sphere', 'Sphere'], ['fill', 'fill_paint', 'Fill'], ['gap', 'gap_fill', 'Gap Fill']],
  seam: [['circle', 'circle_paint', 'Circle'], ['sphere', 'toolbar_modifier_sphere', 'Sphere']],
  fuzzy: [['circle', 'circle_paint', 'Circle'], ['sphere', 'toolbar_modifier_sphere', 'Sphere'], ['fill', 'fill_paint', 'Fill']],
  color: [['circle', 'circle_paint', 'Circle'], ['sphere', 'toolbar_modifier_sphere', 'Sphere'], ['triangle', 'triangle_paint', 'Triangle'],
    ['height', 'height_range', 'Height Range'], ['fill', 'fill_paint', 'Fill'], ['gap', 'gap_fill', 'Gap Fill']],
};
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
  const tips = kind === 'color' ? 'Left mouse button: Paint\nShift + Left mouse button: Erase\n1 - 9: Choose filament'
    : kind === 'fuzzy' ? 'Left mouse button: Add fuzzy skin\nShift + Left mouse button: Remove fuzzy skin'
    : `Left mouse button: ${kind === 'seam' ? 'Enforce seam' : 'Enforce supports'}\nRight mouse button: ${kind === 'seam' ? 'Block seam' : 'Block supports'}\nShift + Left mouse button: Erase`;
  return (
    <div className="manip-panel">
      <div className="manip-title" title={`${tips}\nCtrl + Mouse wheel: ${fill ? 'Smart fill angle' : height ? 'Height range' : 'Brush size'}`}>{PAINT_TITLE[kind]}</div>
      <div className="manip-grid" style={{ gridTemplateColumns: 'auto auto auto' }}>
        {kind === 'color' && (
          <>
            <span>Filaments</span>
            <span style={{ display: 'flex', gap: 4, flexWrap: 'wrap', gridColumn: 'span 2' }}>
              {filaments.slice(0, EXTRUDERS_LIMIT).map((c, i) => (
                <button key={i} type="button" className={`paint-filament ${t.filament === i + 1 ? 'active' : ''}`} style={{ background: c }}
                  title={`Shortcut Key ${i + 1}`} aria-pressed={t.filament === i + 1} onClick={() => setPaintTool({ filament: i + 1 })}>{i + 1}</button>
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
            <span title="Triangle patch area threshold, triangle patch will be merged to neighbor if its area is less than threshold">Gap area</span>
            <input type="range" min={GAP_AREA.min} max={GAP_AREA.max} step={GAP_AREA.step} value={t.gapArea} onChange={(e) => setPaintTool({ gapArea: Number(e.target.value) })} />
            <Num value={t.gapArea} step={GAP_AREA.step} onCommit={(v) => setPaintTool({ gapArea: v })} />
            <span />
            <button className="btn" style={{ gridColumn: 'span 2', justifySelf: 'start' }} disabled={!(t.gapArea > 0) || !painted}
              onClick={() => mapPaint(object.id, kind, PAINT_STEP[kind], (x, scale) => gapFill(x.geometry, paintOf(x, kind), t.gapArea / (scale * scale)))}>Apply</button>
          </>
        ) : triangle ? null : height ? (
          <>
            <span>Height range</span>
            <input type="range" min={HEIGHT.min} max={HEIGHT.max} step={HEIGHT.step} value={t.height} onChange={(e) => setPaintTool({ height: Number(e.target.value) })} />
            <Num value={t.height} step={HEIGHT.step} onCommit={(v) => setPaintTool({ height: v })} />
          </>
        ) : fill && kind === 'color' && !t.edges ? null : (
          <>
            <span>{fill ? 'Smart fill angle' : 'Brush size'}</span>
            <input type="range" min={fill ? ANGLE.min : RADIUS.min} max={fill ? ANGLE.max : RADIUS.max} step={fill ? ANGLE.step : RADIUS.step}
              value={fill ? t.angle : t.radius} onChange={(e) => setPaintTool(fill ? { angle: Number(e.target.value) } : { radius: Number(e.target.value) })} />
            <Num value={fill ? t.angle : t.radius} step={fill ? ANGLE.step : RADIUS.step} onCommit={(v) => setPaintTool(fill ? { angle: v } : { radius: v })} />
          </>
        )}
        {kind === 'color' && fill && (
          <label className="select-row" style={{ gridColumn: '1 / -1' }}>
            <input type="checkbox" checked={t.edges} onChange={(e) => setPaintTool({ edges: e.target.checked })} />Edge detection
          </label>
        )}
        {kind === 'support' && !gap && (
          <label className="select-row" style={{ gridColumn: '1 / -1' }} title={'Allows painting only on facets selected by: "Highlight overhangs"'}>
            <input type="checkbox" checked={t.overhangsOnly} onChange={(e) => setPaintTool({ overhangsOnly: e.target.checked })} />On highlighted overhangs only
          </label>
        )}
        {kind === 'support' && (
          <>
            <span style={{ gridColumn: '1 / -1' }} className="menu-sep" />
            <span title={threshold ? `Auto support threshold angle: ${threshold}` : 'No auto support'}>Highlight overhangs</span>
            <input type="range" min={0} max={90} step={1} value={t.highlight} title="Highlight faces according to overhang angle." onChange={(e) => setPaintTool({ highlight: Number(e.target.value) })} />
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
