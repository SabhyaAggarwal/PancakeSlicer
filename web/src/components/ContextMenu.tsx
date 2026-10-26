import { useEffect, useRef, useState } from 'react';
import { addInstance, arrangeAll, fillBed, openFileDialog, removeInstance, setInstanceCount, toolbarOperation } from '../actions';
import { canDrop, canPaste, centreSelection, copySelection, dropSelection, mirrorSelection, pasteClipboard, selectAll, setFilament, startRename } from '../edit';
import { canEditProject } from '../api/client';
import { changeVolumeType, deleteAll, deleteVolume, effectiveConfig, getState, removeSelected, setState, togglePrintable, updateObject, updateVolume, useStore, VOLUME_ORDER, type PlateObject, type State } from '../state/store';
import { addPrimitive, loadVolume, SHAPES, TYPE_LABEL, VOLUME_MENU } from '../plater/volumes';
import { menuKeys } from '../keys';
import { canAddPlate, deletePlate, renamePlate } from '../plater/plates';
import { promptDialog } from './Ask';

async function editPlateName() {
  const s = getState();
  const plate = s.projectPlates.find((p) => p.index === s.plateIndex);
  if (!plate) return;
  const name = await promptDialog('Edit Plate Name', plate.name);
  if (name !== null) renamePlate(plate.index, name);
}

const ops = () => import('../plater/objectOps');
const exportStl = (selection: boolean, multi = false) => import('../exportStl').then((m) => m.exportStl(selection, multi));

type Item = { label: string; shortcut?: string; action?: () => void; disabled?: boolean; checked?: boolean; tip?: string; sub?: Item[] } | 'sep';

function Items({ items, close }: { items: Item[]; close: () => void }) {
  const [open, setOpen] = useState<number | null>(null);
  return (
    <>
      {items.map((it, k) =>
        it === 'sep' ? <div key={k} className="menu-sep" /> : (
          <div key={k} className="ctx-entry" onMouseEnter={() => setOpen(it.sub ? k : null)}>
            <button className="menu-item" role={it.checked === undefined ? 'menuitem' : 'menuitemcheckbox'} aria-checked={it.checked}
              aria-haspopup={it.sub ? 'menu' : undefined} aria-expanded={it.sub ? open === k : undefined}
              disabled={it.disabled} title={it.tip}
              onClick={() => { if (it.sub) setOpen(k); else { it.action?.(); close(); } }}>
              <span>{it.label}</span><span>{it.sub ? '▸' : it.shortcut ?? ''}</span>
            </button>
            {it.sub && open === k && !it.disabled && (
              <div role="menu" className="menu-pop ctx-sub"><Items items={it.sub} close={close} /></div>
            )}
          </div>
        ),
      )}
    </>
  );
}

export default function ContextMenu() {
  const menu = useStore((s) => s.contextMenu);
  const s = useStore((st) => st);
  const ref = useRef<HTMLDivElement>(null);
  const close = () => setState({ contextMenu: null });
  const back = useRef<HTMLElement | null>(null);
  useEffect(() => {
    if (!menu) return;
    back.current ??= document.activeElement instanceof HTMLElement && document.activeElement !== document.body ? document.activeElement : null;
    ref.current?.querySelector<HTMLElement>('.menu-item:not(:disabled)')?.focus();
    return () => { if (!getState().contextMenu) { back.current?.focus(); back.current = null; } };
  }, [!!menu]);
  useEffect(() => {
    const down = (e: MouseEvent) => { if (!ref.current?.contains(e.target as Node)) close(); };
    const key = (e: KeyboardEvent) => { if (e.key === 'Escape') close(); };
    window.addEventListener('mousedown', down);
    window.addEventListener('keydown', key);
    window.addEventListener('blur', close);
    return () => { window.removeEventListener('mousedown', down); window.removeEventListener('keydown', key); window.removeEventListener('blur', close); };
  }, []);
  if (!menu) return null;
  const o = s.objects.find((ob) => ob.id === menu.object);
  const all = !!s.selected?.all;
  const project = !!s.project && !canEditProject();
  const editable = !!o && !o.project?.placeholder;
  const printable = (o?.instances.find((i) => menu.instance === undefined || i.id === menu.instance) ?? o?.instances[0])?.printable !== false;
  const volume = o?.volumes?.find((v) => v.id === menu.volume);
  const addVolumes: Item[] = o && !all && s.features.volumes && editable && (!s.project || s.features.project_volumes && canEditProject()) ? [...VOLUME_MENU.map(([type, label]) => ({ label, sub: [
    { label: 'Load...', action: () => loadVolume(o.id, type) },
    'sep' as const,
    ...SHAPES.map((shape) => ({ label: shape, action: () => addPrimitive(o.id, type, shape) })),
  ] })), 'sep' as const] : [];
  const items: Item[] = o && volume ? [
    { label: 'Rename', action: () => setState({ renaming: volume.id }) },
    { label: 'Delete', shortcut: 'Del', action: () => deleteVolume(o.id, volume.id), tip: 'Delete the selected part' },
    { label: 'Mirror', tip: 'Mirror the part', sub: ([0, 1, 2] as const).map((a) => (
      { label: `Along ${'XYZ'[a]} Axis`, action: () => void ops().then((m) => m.mirrorVolume(o.id, volume.id, a)), tip: `Mirror along the ${'XYZ'[a]} Axis` })) },
    { label: 'Export as STL...', action: () => void ops().then((m) => m.exportVolumeStl(o.id, volume.id)), tip: 'Export the part as an STL' },
    ...(volume.type === 'part' ? ['sep' as const, { label: 'Change Filament', sub: [{ label: 'Default', checked: !volume.extruder, action: () => updateVolume(o.id, volume.id, { extruder: 0 }) },
      ...s.filamentSlots.map((_, i) => ({ label: `Filament ${i + 1}`, checked: volume.extruder === i + 1, action: () => updateVolume(o.id, volume.id, { extruder: i + 1 }) }))] }] : []),
    'sep',
    { label: 'Change Type', tip: 'Change part type', sub: VOLUME_ORDER.map((type) => (
      { label: TYPE_LABEL[type], checked: volume.type === type, action: () => volume.type !== type && changeVolumeType(o.id, volume.id, type) })) },
  ] : o ? [
    ...(all ? [] : [
      { label: 'Add instance', shortcut: '+', action: addInstance, disabled: !editable || project, tip: 'Add one more instance of the selected object' },
      { label: 'Remove instance', shortcut: '-', action: removeInstance, disabled: o.instances.length < 2 || project, tip: 'Remove one instance of the selected object' },
      { label: 'Set number of instances...', action: () => void setInstanceCount(), disabled: !editable || project, tip: 'Change the number of instances of the selected object' },
      { label: 'Fill bed with instances...', action: fillBed, disabled: !editable || project, tip: 'Fill the remaining area of bed with instances of the selected object' },
      'sep' as const,
    ]),
    { label: 'Copy', shortcut: 'Ctrl+C', action: copySelection, disabled: !editable || !!o.project },
    { label: 'Paste', shortcut: 'Ctrl+V', action: pasteClipboard, disabled: !canPaste(s) },
    { label: 'Center', action: centreSelection, disabled: !editable },
    { label: 'Drop', action: dropSelection, disabled: !editable || !canDrop(s), tip: 'Drop the selection to the bed' },
    ...(all ? [] : [{ label: 'Split', disabled: !editable || !!s.project, sub: [
      { label: 'To Objects', action: () => void toolbarOperation('split'), tip: 'Split the selected object into multiple objects' },
      { label: 'To Parts', action: () => void toolbarOperation('split-parts'), disabled: !s.features.volumes || !!o?.volumes?.length, tip: 'Split the selected object into multiple parts' },
    ] }]),
    ...(all ? [{ label: 'Merge', action: () => void ops().then((m) => m.mergeObjects()), disabled: !!s.project || s.objects.length < 2,
      tip: 'Merge the selected objects into one object of parts' }] : [
      { label: 'Auto orient', action: () => void toolbarOperation('orient'), disabled: !editable || project, tip: 'Auto orient the object to improve print quality' },
      { label: 'Scale to build volume', action: () => void ops().then((m) => m.scaleToBuildVolume(o.id)), disabled: !editable || !!o.project, tip: 'Scale the selected object to fit the build volume' },
      ...(o.units ? [{ label: `Restore to ${o.units}`, action: () => void ops().then((m) => m.convertUnits(o.id, null)), disabled: !editable || !!o.project }]
        : [{ label: 'Convert from inch', action: () => void ops().then((m) => m.convertUnits(o.id, 'inch')), disabled: !editable || !!o.project },
          { label: 'Convert from meter', action: () => void ops().then((m) => m.convertUnits(o.id, 'meter')), disabled: !editable || !!o.project }]),
      ...(o.volumes?.some((v) => v.type === 'part') ? [{ label: 'Set as individual objects', action: () => void ops().then((m) => m.partsToObjects(o.id)), disabled: !editable || !!o.project,
        tip: 'Each part becomes an object' }] : []),
      { label: 'Copy process settings', action: () => void ops().then((m) => m.copySettings(o.id)), disabled: !Object.keys(o.settings).length, tip: 'Copy the object\'s own process settings' },
      { label: 'Paste process settings', action: () => void ops().then((m) => m.pasteSettings([o.id])), disabled: !s.settingsClipboard, tip: 'Paste the copied process settings onto this object' },
      { label: 'Replace 3D file...', action: () => void ops().then((m) => m.replaceFile(o.id)), disabled: !editable || !!o.project, tip: 'Replace the object\'s mesh with another file' },
    ]),
    { label: 'Mirror', disabled: !editable, tip: 'Mirror object', sub: ([0, 1, 2] as const).map((a) => (
      { label: `Along ${'XYZ'[a]} Axis`, action: () => void mirrorSelection(a), tip: `Mirror along the ${'XYZ'[a]} Axis` })) },
    ...(all ? [] : [{ label: 'Rename', action: () => startRename(o.id), disabled: !!o.project }]),
    { label: 'Delete', shortcut: 'Del', action: removeSelected, disabled: project, tip: 'Delete the selected object' },
    'sep',
    ...addVolumes,
    ...(all ? [] : [{ label: 'Change Filament', disabled: project, sub: s.filamentSlots.map((_, i) => (
      { label: `Filament ${i + 1}`, checked: o.extruder === i + 1, action: () => setFilament(o.id, i + 1) })) }]),
    ...(all ? [] : flushOptions(s, o)),
    { label: 'Printable', shortcut: 'V', checked: printable, action: () => togglePrintable(o.id, menu.instance) },
    'sep',
    { label: 'Export as one STL...', action: () => void exportStl(true), disabled: !editable },
    ...(all ? [{ label: 'Export as STLs...', action: () => void exportStl(true, true), disabled: !editable }] : []),
  ] : [
    { label: 'Add Models', shortcut: 'Ctrl+I', action: openFileDialog },
    { label: 'Paste', shortcut: 'Ctrl+V', action: pasteClipboard, disabled: !canPaste(s) },
    'sep',
    { label: 'Select All', shortcut: 'Ctrl+A', action: selectAll, disabled: !s.objects.length, tip: 'Select all objects on the current plate' },
    { label: 'Delete All', shortcut: 'Ctrl+Del', action: deleteAll, disabled: !s.objects.length && !getState().projectPlates.length, tip: 'Delete all objects on the current plate' },
    { label: 'Arrange', action: () => arrangeAll(false), disabled: !s.objects.length, tip: 'Arrange current plate' },
    ...(canAddPlate(s) && s.projectPlates.length > 1 ? [
      { label: 'Delete Plate', action: () => deletePlate(), tip: 'Remove the selected plate' },
      { label: 'Edit Plate Name', action: () => void editPlateName() },
    ] : []),
    ...(canAddPlate(s) ? [{ label: 'Plate Settings...', action: () => setState({ appDialog: 'plate' }), tip: 'Customize current plate' }] : []),
  ];
  return (
    <div ref={ref} role="menu" className="menu-pop ctx-menu" style={{ left: Math.min(menu.x, window.innerWidth - 230), top: Math.min(menu.y, window.innerHeight - 24 * items.length - 16) }}
      onContextMenu={(e) => e.preventDefault()}
      onKeyDown={(e) => {
        const item = document.activeElement as HTMLElement;
        if (e.key === 'ArrowRight' && item.getAttribute('aria-haspopup')) {
          item.click();
          requestAnimationFrame(() => item.parentElement?.querySelector<HTMLElement>('.ctx-sub .menu-item:not(:disabled)')?.focus());
        } else if (e.key === 'ArrowLeft' && item.closest('.ctx-sub')) {
          item.closest('.ctx-sub')!.parentElement!.querySelector<HTMLElement>(':scope > .menu-item')?.focus();
        } else {
          const sub = item.closest<HTMLElement>('.ctx-sub');
          menuKeys(e, sub ?? ref.current, close);
          return;
        }
        e.preventDefault();
      }}>
      <Items items={items} close={close} />
    </div>
  );
}

const FLUSH: [string, string][] = [['flush_into_infill', "Flush into objects' infill"], ['flush_into_objects', 'Flush into this object'], ['flush_into_support', "Flush into objects' support"]];
const on = (v: unknown) => v === true || v === 1 || v === '1' || v === 'true';

function flushOptions(s: State, o: PlateObject): Item[] {
  const used = new Set(s.objects.flatMap((x) => [x.extruder || 1, ...(x.volumes ?? []).map((v) => v.extruder || x.extruder || 1)]));
  if (used.size < 2) return [];
  const global = effectiveConfig(s, 'process');
  const tower = on(global.enable_prime_tower);
  return [{ label: 'Flush Options', sub: FLUSH.map(([key, label]) => {
    const value = on(key in o.settings ? o.settings[key] : global[key]);
    return { label, checked: value, disabled: !tower, action: () => updateObject(o.id, { settings: { ...o.settings, [key]: !value } }, label) };
  }) }];
}
