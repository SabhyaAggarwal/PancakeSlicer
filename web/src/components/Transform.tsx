import { Suspense, lazy, useEffect, useState } from 'react';
import { Glyph } from './Glyph';
import { Popover } from 'radix-ui';
import * as THREE from 'three';
import { setState, updateInstance, updateObject, useStore, markStale } from '../state/store';
import { undoable } from '../state/history';
import { Num } from './Num';

const PartManipulation = lazy(() => import('./PartManipulation'));


let moveCoordinates: 'world' | 'object' = 'world';
const DEG = Math.PI / 180;
const AX = ['X', 'Y', 'Z'];

const rotationOf = (rot: [number, number, number]) => new THREE.Matrix4().makeRotationZ(rot[2] * DEG)
  .multiply(new THREE.Matrix4().makeRotationY(rot[1] * DEG)).multiply(new THREE.Matrix4().makeRotationX(rot[0] * DEG));


function setRotation(objectId: number, instanceId: number, rot: [number, number, number]) {
  undoable('Reset rotation', () => setState((s) => ({
    objects: s.objects.map((o) => (o.id !== objectId ? o : {
      ...o, rotX: rot[0], rotY: rot[1], instances: o.instances.map((i) => (i.id === instanceId ? { ...i, rotZ: rot[2] } : i)),
    })),
    job: markStale(s.job),
  })));
}


export function CoordToggle({ value, onChange, enabled }: { value: 'world' | 'object'; onChange: (v: 'world' | 'object') => void; enabled: boolean }) {
  return (
    <span className="seg" role="radiogroup" aria-label="Coordinates">
      {(['world', 'object'] as const).map((v) => (
        <button key={v} type="button" role="radio" aria-checked={value === v} disabled={!enabled} onClick={() => onChange(v)}>
          {v === 'world' ? 'World' : 'Object'}
        </button>
      ))}
    </span>
  );
}


function RowButton({ d, title, onClick, disabled, pressed }: { d: string; title: string; onClick: () => void; disabled?: boolean; pressed?: boolean }) {
  return (
    <button type="button" className="xf-btn" aria-label={title} aria-pressed={pressed} disabled={disabled} onClick={onClick}>
      <svg width="16" height="16" viewBox="0 0 16 16" aria-hidden="true"><path d={d} fill="none" stroke="currentColor" strokeWidth="1.4" strokeLinejoin="round" /></svg>
    </button>
  );
}


export function Row({ label, values, onCommit, names, action }: { label: string; values?: number[]; onCommit: (axis: number, v: number) => void; names: string; action?: React.ReactNode }) {
  return (
    <>
      <span className="xf-key">{label}</span>
      {[0, 1, 2].map((a) => <Num key={a} label={`${names} ${AX[a]}`} value={values?.[a]} onCommit={(v) => onCommit(a, v)} />)}
      <span className="xf-act">{action}</span>
    </>
  );
}

export function TransformPanel() {
  const gizmo = useStore((s) => s.gizmo);
  const sel = useStore((s) => s.selected);
  const o = useStore((s) => (s.selected && !s.selected.all ? s.objects.find((ob) => ob.id === s.selected!.object) : undefined));
  const uniform = useStore((s) => s.uniformScale);
  const inst = o?.instances.find((i) => i.id === sel?.instance);
  const [coordinates, setCoordinates] = useState(moveCoordinates);
  const active = gizmo === 'move' || gizmo === 'rotate' || gizmo === 'scale';
  const valid = !!o && !!inst && !sel?.all && !o.project?.placeholder;
  const close = () => setState({gizmo: 'none'});
  useEffect(() => { if (active && !valid) setState({gizmo: 'none'}); }, [active, valid]);


  const openKey = `${gizmo}|${o?.id}|${inst?.id}`;
  const [opened, setOpened] = useState<{ key: string; pos: THREE.Vector3; rot: [number, number, number] } | null>(null);
  if (o && inst && opened?.key !== openKey) setOpened({ key: openKey, pos: new THREE.Vector3(inst.x, inst.y, inst.z ?? 0), rot: [o.rotX, o.rotY, inst.rotZ] });
  const title = gizmo === 'move' ? 'Move' : gizmo === 'rotate' ? 'Rotate' : gizmo === 'scale' ? 'Scale' : 'Transform';

  const toggle = gizmo === 'move' && inst ? <CoordToggle value={coordinates} enabled onChange={(c) => { moveCoordinates = c; setCoordinates(c); }} /> : null;
  const v = o?.volumes?.find((vv) => vv.id === sel?.volume);
  const live = o && inst && !o.project?.placeholder ? o : undefined;
  const lock = <RowButton pressed={uniform} title={uniform ? 'Uniform scale: on' : 'Uniform scale: off'} onClick={() => setState({ uniformScale: !uniform })}
    d={uniform ? 'M5 7V5a3 3 0 0 1 6 0v2M4 7h8v6H4z' : 'M5 7V5a3 3 0 0 1 6 0M4 7h8v6H4z'} />;
  let body: React.ReactNode;
  if (live && inst && v) body = <Suspense fallback={null}><PartManipulation o={live} inst={inst} v={v} local={coordinates === 'object' && gizmo === 'move'} lock={lock} /></Suspense>;
  else if (live && inst) {
    const o = live;


    const mesh = o.geometry.boundingBox!.getSize(new THREE.Vector3());
    const rescale = (axis: number, factor: number) => updateObject(o.id, {
      scale: o.scale.map((s, k) => (uniform || k === axis ? s * factor : s)) as [number, number, number],
    });
    const rot: [number, number, number] = [o.rotX, o.rotY, inst.rotZ];
    const start = opened?.key === openKey ? opened : { pos: new THREE.Vector3(inst.x, inst.y, inst.z ?? 0), rot };
    const local = coordinates === 'object' && gizmo === 'move';
    const axes = rotationOf(rot);
    const relative = new THREE.Vector3(inst.x, inst.y, inst.z ?? 0).sub(start.pos).applyMatrix4(axes.clone().transpose());
    const moveTo = (axis: number, value: number) => {
      if (!local) return updateInstance(o.id, inst.id, [{ x: value }, { y: value }, { z: value }][axis]);
      const p = relative.clone().setComponent(axis, value).applyMatrix4(axes).add(start.pos);
      updateInstance(o.id, inst.id, { x: p.x, y: p.y, z: p.z });
    };

    const back = gizmo === 'rotate' && rot.some((r, k) => Math.abs(r - start.rot[k]) > 1e-9);
    const reset = <RowButton disabled={!back && rot.every((r) => r === 0)} d="M3.5 8a4.5 4.5 0 1 0 1.4-3.3M3.5 2.5v2.5H6"
      title={back ? 'Reset current rotation to the value when open the rotation tool.' : 'Reset current rotation to real zeros.'}
      onClick={() => setRotation(o.id, inst.id, back ? start.rot : [0, 0, 0])} />;
    body = (
      <>
        <Row label={local ? 'Relative' : 'Position'} names="Position" values={local ? relative.toArray() : [inst.x, inst.y, inst.z ?? 0]} onCommit={moveTo} />
        <Row label="Rotation" names="Rotation" values={rot} action={reset}
          onCommit={(a, val) => (a === 2 ? updateInstance(o.id, inst.id, { rotZ: val }) : updateObject(o.id, a === 0 ? { rotX: val } : { rotY: val }))} />
        <Row label="Scale %" names="Scale" values={o.scale.map((s) => Math.abs(s) * 100)} action={lock}
          onCommit={(a, val) => rescale(a, Math.max(0.1, val) / 100 / Math.abs(o.scale[a]))} />
        <Row label="Size mm" names="Size" values={[0, 1, 2].map((k) => Math.abs(o.scale[k]) * mesh.getComponent(k))}
          onCommit={(a, val) => rescale(a, Math.max(0.01, val) / Math.max(1e-6, Math.abs(o.scale[a]) * mesh.getComponent(a)))} />
      </>
    );
  } else {

    const none = () => undefined;
    body = (
      <>
        <Row label="Position" names="Position" onCommit={none} />
        <Row label="Rotation" names="Rotation" onCommit={none} />
        <Row label="Scale %" names="Scale" onCommit={none} />
        <Row label="Size mm" names="Size" onCommit={none} />
      </>
    );
  }
  if (!active || !valid) return null;
  return <Popover.Root open onOpenChange={open => { if (!open) close(); }}>
    <Popover.Anchor asChild><div className="transform-anchor" /></Popover.Anchor>
    <Popover.Portal><Popover.Content className="xf transform-floating" aria-label={`${title} ${v?.name ?? o.name}`} side="bottom" align="start" sideOffset={0} collisionPadding={12}
      onOpenAutoFocus={e => e.preventDefault()} onCloseAutoFocus={e => e.preventDefault()}
      onInteractOutside={e => e.preventDefault()} onEscapeKeyDown={e => { e.stopPropagation(); close(); }}>
      <div className="xf-head"><span className="xf-label">{title}</span>{toggle}<button type="button" className="xf-fold" aria-label="Close transform" onClick={close}><Glyph name="close" size={14} /></button></div>
      <div className="xf-selection">{v?.name ?? o.name}</div>
      <div className="xf-grid"><span/><span className="axis x">X</span><span className="axis y">Y</span><span className="axis z">Z</span><span/>{body}</div>
    </Popover.Content></Popover.Portal>
  </Popover.Root>;
}
