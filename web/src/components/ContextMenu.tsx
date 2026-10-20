import { useEffect, useRef, useState } from 'react';
import { addInstance, arrangeAll, openFileDialog, removeInstance, toolbarOperation } from '../actions';
import { canDrop, canPaste, centreSelection, copySelection, dropSelection, mirrorSelection, pasteClipboard, selectAll, startRename } from '../edit';
import { deleteAll, getState, removeSelected, setState, togglePrintable, updateObject, useStore } from '../state/store';

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
  const project = !!s.project;
  const editable = !!o && !o.project?.placeholder;
  const printable = (o?.instances.find((i) => menu.instance === undefined || i.id === menu.instance) ?? o?.instances[0])?.printable !== false;
  const items: Item[] = o ? [
    ...(all ? [] : [
      { label: 'Add instance', shortcut: '+', action: addInstance, disabled: !editable || project, tip: 'Add one more instance of the selected object' },
      { label: 'Remove instance', shortcut: '-', action: removeInstance, disabled: o.instances.length < 2 || project, tip: 'Remove one instance of the selected object' },
      'sep' as const,
    ]),
    { label: 'Copy', shortcut: 'Ctrl+C', action: copySelection, disabled: !editable || project },
    { label: 'Paste', shortcut: 'Ctrl+V', action: pasteClipboard, disabled: !canPaste(s) },
    { label: 'Center', action: centreSelection, disabled: !editable },
    { label: 'Drop', action: dropSelection, disabled: !editable || !canDrop(s), tip: 'Drop the selection to the bed' },
    ...(all ? [] : [{ label: 'Split', disabled: !editable || project, sub: [
      { label: 'To Objects', action: () => void toolbarOperation('split'), tip: 'Split the selected object into multiple objects' },
      { label: 'To Parts', disabled: true, tip: 'Split the selected object into multiple parts' },
    ] }]),
    { label: 'Mirror', disabled: !editable, tip: 'Mirror object', sub: ([0, 1, 2] as const).map((a) => (
      { label: `Along ${'XYZ'[a]} Axis`, action: () => void mirrorSelection(a), tip: `Mirror along the ${'XYZ'[a]} Axis` })) },
    ...(all ? [] : [{ label: 'Rename', action: () => startRename(o.id), disabled: !!o.project }]),
    { label: 'Delete', shortcut: 'Del', action: removeSelected, disabled: project, tip: 'Delete the selected object' },
    'sep',
    ...(all ? [] : [{ label: 'Change Filament', disabled: project, sub: s.filamentSlots.map((_, i) => (
      { label: `Filament ${i + 1}`, checked: o.extruder === i + 1, action: () => updateObject(o.id, { extruder: i + 1 }) })) }]),
    { label: 'Printable', shortcut: 'V', checked: printable, action: () => togglePrintable(o.id, menu.instance) },
  ] : [
    { label: 'Add Models', shortcut: 'Ctrl+I', action: openFileDialog },
    { label: 'Paste', shortcut: 'Ctrl+V', action: pasteClipboard, disabled: !canPaste(s) },
    'sep',
    { label: 'Select All', shortcut: 'Ctrl+A', action: selectAll, disabled: !s.objects.length, tip: 'Select all objects on the current plate' },
    { label: 'Delete All', shortcut: 'Ctrl+Del', action: deleteAll, disabled: !s.objects.length && !getState().projectPlates.length, tip: 'Delete all objects on the current plate' },
    { label: 'Arrange', shortcut: 'A', action: arrangeAll, disabled: !s.objects.length },
  ];
  return (
    <div ref={ref} role="menu" className="menu-pop ctx-menu" style={{ left: Math.min(menu.x, window.innerWidth - 230), top: Math.min(menu.y, window.innerHeight - 24 * items.length - 16) }}
      onContextMenu={(e) => e.preventDefault()}>
      <Items items={items} close={close} />
    </div>
  );
}
