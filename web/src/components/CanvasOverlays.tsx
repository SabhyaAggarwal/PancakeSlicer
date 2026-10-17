import { useEffect, useState } from 'react';
import { addInstance, arrangeAll, cancelSlice, download, openFileDialog, outputKind, removeInstance } from '../actions';
import { objectSize } from '../plater/geometry';
import { setState, updateInstance, updateObject, useStore, type Gizmo } from '../state/store';
import { IconButton } from './Icon';

const LATER = 'Not available in the web app yet';

export function CanvasToolbar() {
  const gizmo = useStore((s) => s.gizmo);
  const hasObjects = useStore((s) => s.objects.length > 0);
  const sel = useStore((s) => s.selected);
  const instances = useStore((s) => s.objects.find((o) => o.id === s.selected?.object)?.instances.length ?? 0);
  const toggle = (g: Gizmo) => setState({ gizmo: gizmo === g ? 'none' : g });
  return (
    <div className="canvas-toolbar">
      <IconButton size={24} icon="toolbar_open" title="Add (Ctrl+I)" onClick={openFileDialog} />
      <IconButton size={24} icon="toolbar_add_plate" title={`Add plate — ${LATER}`} disabled />
      <IconButton size={24} icon="toolbar_orient" title={`Auto orient — ${LATER}`} disabled />
      <IconButton size={24} icon="toolbar_arrange" title="Arrange all objects (A)" onClick={arrangeAll} disabled={!hasObjects} />
      <span className="tb-sep" />
      <IconButton size={24} icon="instance_add" title="Add instance (+)" onClick={addInstance} disabled={!sel} />
      <IconButton size={24} icon="instance_remove" title="Remove instance (-)" onClick={removeInstance} disabled={instances < 2} />
      <IconButton size={24} icon="split_objects" title={`Split to objects — ${LATER}`} disabled />
      <IconButton size={24} icon="split_parts" title={`Split to parts — ${LATER}`} disabled />
      <IconButton size={24} icon="toolbar_variable_layer_height" title={`Variable layer height — ${LATER}`} disabled />
      <span className="tb-sep" />
      <IconButton size={24} icon="toolbar_move" title="Move (M)" active={gizmo === 'move'} onClick={() => toggle('move')} disabled={!sel} />
      <IconButton size={24} icon="toolbar_rotate" title="Rotate (R)" active={gizmo === 'rotate'} onClick={() => toggle('rotate')} disabled={!sel} />
      <IconButton size={24} icon="toolbar_scale" title="Scale (S)" active={gizmo === 'scale'} onClick={() => toggle('scale')} disabled={!sel} />
      <IconButton size={24} icon="toolbar_flatten" title={`Lay on face — ${LATER}`} disabled />
      <IconButton size={24} icon="toolbar_cut" title={`Cut — ${LATER}`} disabled />
      <IconButton size={24} icon="toolbar_support" title={`Support painting — ${LATER}`} disabled />
      <IconButton size={24} icon="toolbar_seam" title={`Seam painting — ${LATER}`} disabled />
      <IconButton size={24} icon="toolbar_measure" title={`Measure — ${LATER}`} disabled />
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
  if (!o || !inst || gizmo === 'none') return null;
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
        <span className="manip-ro">0.00</span>
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

export function SliceNotification() {
  const job = useStore((s) => s.job);
  const record = job.status?.record as { text_summary?: Record<string, string> } | undefined;
  if (job.state === 'idle') return null;
  const busy = job.state === 'running' || job.state === 'submitting';
  return (
    <div className={`notification ${job.state}`}>
      {busy && (
        <>
          <div className="notif-text">
            Slicing Plate 1 <span className="notif-stage">{job.stage}</span>
            <button className="link" onClick={cancelSlice}>
              Cancel
            </button>
          </div>
          <div className="progress">
            <div className="progress-fill" style={{ width: `${Math.max(2, job.pct)}%` }} />
          </div>
          <div className="notif-pct">{Math.round(job.pct)}%</div>
        </>
      )}
      {job.state === 'done' && (
        <div className="notif-text">
          {job.stale ? 'Settings changed since slicing. Slice again to export.' : 'Slicing complete.'}
          {!job.stale && (
            <button className="link" onClick={download}>
              {outputKind() === 'gcode.3mf' ? 'Export plate sliced file' : 'Export G-code file'}
            </button>
          )}
          {record?.text_summary?.estimated_printing_time && <span className="notif-stage">{record.text_summary.estimated_printing_time}</span>}
        </div>
      )}
      {job.state === 'failed' && <div className="notif-text error">Slicing failed: {job.error}</div>}
    </div>
  );
}

export function Toast() {
  const msg = useStore((s) => s.toast);
  return msg ? <div className="toast">{msg}</div> : null;
}
