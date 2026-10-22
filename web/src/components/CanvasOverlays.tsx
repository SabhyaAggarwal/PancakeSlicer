import { slicingText } from '../api/progress';
import { accessNotice, accessSnapshot, saveAccessToken, subscribeAccess } from '../api/access';
import { useEffect, useLayoutEffect, useMemo, useRef, useState, useSyncExternalStore } from 'react';
import * as THREE from 'three';
import { addInstance, arrangeAll, cancelSlice, download, fileUse, openFileDialog, outputKind, removeInstance, toolbarOperation, outsideVolumeWarning } from '../actions';
import { setCompareWanted } from '../compare';
import { toggleNonplanarView, useNonplanarView, usePatchPreview, type PreviewFact } from '../patch-preview';
import { applyRecommended, pendingRecommendations, plateResults, type PlateResult } from '../recommend';
import { STYLE_NAMES } from '../style-names';
import { getState, printerPreset, setState, updateInstance, updateObject, updateVolume, useStore, type Gizmo } from '../state/store';
import { instanceMatrix, volumeBox } from '../plater/geometry';
import { Icon, IconButton } from './Icon';
import { canPerformCut, cutBlocked, initialCut, performCut, setCut, toggleCut } from '../plater/cut';
import { InTopLayer } from './Modal';

const LATER_GIZMOS: [icon: string, tooltip: string][] = [
  ['toolbar_meshboolean', 'Mesh Boolean (B)'],
  ['toolbar_support', 'Support Painting (L)'],
  ['toolbar_seam', 'Seam painting (P)'],
  ['toolbar_fuzzy_skin_paint', 'Paint-on fuzzy skin (H)'],
  ['mmu_segmentation', 'Color Painting (N)'],
  ['toolbar_texture_displacement', 'Texture displacement'],
  ['toolbar_text', 'Emboss (T)'],
  ['svg_part', 'SVG'],
  ['toolbar_assembly', 'Assemble (Y)'],
  ['toolbar_brimears', 'Brim Ears (E)'],
];

export function CanvasToolbar() {
  const gizmo = useStore((s) => s.gizmo);
  const hasObjects = useStore((s) => s.objects.length > 0);
  const sel = useStore((s) => s.selected);
  const instances = useStore((s) => s.objects.find((o) => o.id === s.selected?.object)?.instances.length ?? 0);
  const project = useStore((s) => !!s.project);
  const toggle = (g: Gizmo) => setState({ gizmo: gizmo === g ? 'none' : g });
  const locked = project ? ' — instances cannot be added or removed from an opened project' : '';
  const placeholder = useStore((s) => s.objects.find((o) => o.id === s.selected?.object)?.project?.placeholder);
  const edit = !!sel && !placeholder && !sel.all;
  const cutBlock = useStore(cutBlocked);
  const preview = !!usePatchPreview();
  const nonplanarView = useNonplanarView();
  return (
    <div className="canvas-toolbar">
      <IconButton size={24} icon="toolbar_open" title="Add (Ctrl+I)" onClick={openFileDialog} />
      <IconButton size={24} icon="toolbar_add_plate" title="Add plate" disabled />
      <IconButton size={24} icon="toolbar_orient" title={"Auto orient all/selected objects (Q)\nAuto orient all objects on current plate (Shift+Q)"} onClick={(e) => toolbarOperation('orient', e.shiftKey)} disabled={!hasObjects || project} />
      <IconButton size={24} icon="toolbar_arrange" title={`Arrange all objects (A)\nArrange objects on selected plates (Shift+A)`} onClick={arrangeAll} disabled={!hasObjects} />
      <span className="tb-sep" />
      <IconButton size={24} icon="instance_add" title={`Add instance (+)${locked}`} onClick={addInstance} disabled={!edit || project} />
      <IconButton size={24} icon="instance_remove" title={`Remove instance (-)${locked}`} onClick={removeInstance} disabled={instances < 2 || project} />
      <IconButton size={24} icon="split_objects" title="Split to objects" onClick={() => toolbarOperation('split')} disabled={!edit || project} />
      <IconButton size={24} icon="split_parts" title="Split to parts" disabled />
      <IconButton size={24} icon="toolbar_variable_layer_height" title="Variable layer height" disabled />
      <IconButton size={24} icon="toolbar_assemble" title="Assembly View" disabled />
      <span className="tb-sep" />
      <IconButton size={24} icon="toolbar_move" title="Move (M)" active={gizmo === 'move'} onClick={() => toggle('move')} disabled={!edit} />
      <IconButton size={24} icon="toolbar_rotate" title="Rotate (R)" active={gizmo === 'rotate'} onClick={() => toggle('rotate')} disabled={!edit} />
      <IconButton size={24} icon="toolbar_scale" title="Scale (S)" active={gizmo === 'scale'} onClick={() => toggle('scale')} disabled={!edit} />
      <IconButton size={24} icon="toolbar_flatten" title="Lay on Face (F)" active={gizmo === 'flatten'} onClick={() => toggle('flatten')} disabled={!edit} />
      <IconButton size={24} icon="toolbar_cut" title={cutBlock ? `Cut (C) — ${cutBlock}` : 'Cut (C)'} active={gizmo === 'cut'} onClick={toggleCut} disabled={!!cutBlock && gizmo !== 'cut'} />
      <IconButton size={24} icon="toolbar_measure" title="Measure (U)" active={gizmo === 'measure'} onClick={() => toggle('measure')} disabled={!sel || !!placeholder} />
      {LATER_GIZMOS.map(([icon, title]) => (
        <IconButton key={icon} size={24} icon={icon} title={title} disabled />
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

export function ManipulationPanel() {
  const gizmo = useStore((s) => s.gizmo);
  const o = useStore((s) => s.objects.find((ob) => ob.id === s.selected?.object));
  const instId = useStore((s) => s.selected?.instance);
  const uniform = useStore((s) => s.uniformScale);
  const volumeId = useStore((s) => s.selected?.volume);
  const inst = o?.instances.find((i) => i.id === instId);
  if (!o || !inst || gizmo === 'none' || gizmo === 'flatten' || gizmo === 'cut' || gizmo === 'measure' || o.project?.placeholder) return null;
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
      <div className="manip-panel">
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
          {gizmo === 'scale' && (
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
    <div className="manip-panel">
      <div className="manip-title">{gizmo === 'move' ? 'Move' : gizmo === 'rotate' ? 'Rotate' : 'Scale'}</div>
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
        {gizmo === 'scale' && (
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

export function CutPanel() {
  const cut = useStore((s) => (s.gizmo === 'cut' ? s.cut : null));
  const can = useStore(canPerformCut);
  if (!cut) return null;
  const initial = initialCut(getState());
  const part = (label: string, colour: string, keep: boolean, onKeep: (v: boolean) => void, place: boolean, onPlace?: (v: boolean) => void) => (
    <>
      <span style={{ display: 'flex', alignItems: 'center', gap: 4 }}>
        <span style={{ width: 10, height: 10, background: colour, flex: 'none' }} />{label}
      </span>
      <label className="select-row" style={{ whiteSpace: 'nowrap' }}><input type="checkbox" checked={keep} onChange={(e) => onKeep(e.target.checked)} />Keep</label>
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
        <span style={{ gridColumn: '1 / -1' }} className="menu-sep" />
        <span style={{ gridColumn: '1 / -1' }}>After cut:</span>
      </div>
      <div style={{ display: 'grid', gridTemplateColumns: 'auto auto auto', gap: '0 12px', alignItems: 'center' }}>
        {part('Upper part', '#00ffff', cut.keepUpper, (keepUpper) => setCut({ keepUpper }), true)}
        {part('Lower part', '#ff00ff', cut.keepLower, (keepLower) => setCut({ keepLower }), cut.placeLower, (placeLower) => setCut({ placeLower }))}
      </div>
      <div style={{ display: 'flex', gap: 8, marginTop: 8 }}>
        <button className="btn" disabled={!initial || (cut.z === initial.z && !cut.rotX && !cut.rotY)} onClick={() => initial && setCut({ z: initial.z, rotX: 0, rotY: 0 })}>Reset</button>
        <span style={{ flex: 1 }} />
        <button className="btn primary" disabled={!can} onClick={() => void performCut()}>Perform cut</button>
        <button className="btn" onClick={() => setState({ gizmo: 'none', cut: null })}>Cancel</button>
      </div>
    </div>
  );
}
