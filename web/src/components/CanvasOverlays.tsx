import { slicingText } from '../api/progress';
import { accessNotice, accessSnapshot, saveAccessToken, subscribeAccess } from '../api/access';
import { useEffect, useState, useSyncExternalStore } from 'react';
import { addInstance, arrangeAll, cancelSlice, openFileDialog, removeInstance, toolbarOperation, outsideVolumeWarning } from '../actions';
import { objectSize } from '../plater/geometry';
import { setState, updateInstance, updateObject, useStore, type Gizmo } from '../state/store';
import { Icon, IconButton } from './Icon';

const LATER_GIZMOS: [icon: string, tooltip: string][] = [
  ['toolbar_cut', 'Cut (C)'],
  ['toolbar_meshboolean', 'Mesh Boolean (B)'],
  ['toolbar_support', 'Support Painting (L)'],
  ['toolbar_seam', 'Seam painting (P)'],
  ['toolbar_fuzzy_skin_paint', 'Paint-on fuzzy skin (H)'],
  ['mmu_segmentation', 'Color Painting (N)'],
  ['toolbar_texture_displacement', 'Texture displacement'],
  ['toolbar_text', 'Emboss (T)'],
  ['svg_part', 'SVG'],
  ['toolbar_measure', 'Measure (U)'],
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
  const edit = !!sel && !placeholder;
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
      <IconButton size={24} icon="toolbar_flatten" title="Lay on Face (F)" disabled />
      {LATER_GIZMOS.map(([icon, title]) => (
        <IconButton key={icon} size={24} icon={icon} title={title} disabled />
      ))}
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
  const inst = o?.instances.find((i) => i.id === instId);
  if (!o || !inst || gizmo === 'none' || o.project?.placeholder) return null;
  const size = objectSize(o);
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
          <Num key={a} value={o.scale[a] * 100} onCommit={(v) => updateObject(o.id, { scale: o.scale.map(() => Math.max(0.1, v) / 100) as [number, number, number] })} />
        ))}
        <span>Size mm</span>
        {(['x', 'y', 'z'] as const).map((a) => (
          <Num
            key={a}
            value={size[a]}
            onCommit={(v) => {
              const f = Math.max(0.01, v) / Math.max(1e-6, size[a]);
              updateObject(o.id, { scale: o.scale.map((sv) => sv * f) as [number, number, number] });
            }}
          />
        ))}
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

export function SliceNotification() {
  const notice = useSyncExternalStore(subscribeAccess, accessSnapshot);
  const plateIndex = useStore((s) => s.plateIndex);
  const job = useStore((s) => s.job);
  const outside = useStore(outsideVolumeWarning);
  const [closed, setClosed] = useState<Set<string>>(new Set());
  const [expired, setExpired] = useState<Set<string>>(new Set());
  const brief = job.state === 'canceled' ? `canceled` : job.state === 'done' && !job.stale ? `done:${job.id}` : null;
  const exported = job.exported ? `exported:${job.id}:${job.exported}` : null;
  useEffect(() => {
    const timers = ([[brief, 3000], [exported, 20000]] as const)
      .filter(([k]) => k)
      .map(([k, ms]) => setTimeout(() => setExpired((e) => new Set(e).add(k!)), ms));
    return () => timers.forEach(clearTimeout);
  }, [brief, exported]);
  useEffect(() => setClosed(new Set()), [job.id]);
  const busy = job.state === 'running' || job.state === 'submitting';
  const close = (k: string) => (
    <button className="notif-close icon-btn" title="Close" onClick={() => setClosed((c) => new Set(c).add(k))}>
      <Icon name="notification_close" size={14} />
    </button>
  );
  const warnings = job.state === 'done' && !job.stale ? (job.warnings ?? []) : [];
  return (
    <div className="notifications">
      <AccessNotification />
      {outside && (
        <div className="notification warning" role="alert">
          <div className="notif-title">Object outside the print volume</div>
          <div className="notif-body">{outside}</div>
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
      {job.state === 'failed' && notice?.message !== cleanError(job.error) && !/Access token required|Server storage full|Server busy\./.test(job.error ?? '') && !closed.has('error') && (
        <div className="notification failed">
          {close('error')}
          <div className="notif-title">Error:</div>
          <div className="notif-body">{cleanError(job.error)}</div>
        </div>
      )}
      {exported && !expired.has(exported) && !closed.has(exported) && (
        <div className="notification done">
          {close(exported)}
          <div className="notif-title">Exported successfully</div>
          <div className="notif-body">{job.exported}</div>
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
      {brief && !expired.has(brief) && !exported && (
        <div className="notification done">
          <div className="notif-title">
            {job.state === 'canceled' ? 'Slicing Canceled' : <><Icon name="notification_slicing_complete" size={16} /> Slice complete</>}
          </div>
        </div>
      )}
    </div>
  );
}

export function Toast() {
  const msg = useStore((s) => s.toast ?? s.projectNotice);
  return msg ? <div className="toast">{msg}</div> : null;
}
