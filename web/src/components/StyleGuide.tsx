import { useState } from 'react';
import type { Config } from '../api/types';
import { applyStyle, isRotary, styleOf, type StyleId } from '../print-style';
import { STYLE_NAMES } from '../style-names';
import { printerPreset, useStore } from '../state/store';
import { styleIcon, type IconId } from './icons';
import { Modal, ModalTitle } from './Modal';
import './print-style.css';

export interface ModeEntry {
  id: string;
  name: string;
  keys: string;
  short: string;
  sentence: string;
  bestFor?: string;
  avoidFor?: string;
  number?: [string, string];
  requirement?: string;
  group: 'all' | 'auto' | 'rotary';
  style?: StyleId;
  icon: IconId;
}

const FIVE_AXIS = 'Prodcan3, FR3 cell or another rotary printer';

export const MODES: ModeEntry[] = [
  { id: 'auto', name: STYLE_NAMES.auto, keys: 'Auto', group: 'all', style: 'auto', icon: 'layers', short: 'Chooses the best per part',
    sentence: 'Picks per part: curved tops, whole-part curved layers, or flat where nothing gains.' },
  { id: 'standard', name: STYLE_NAMES.standard, keys: 'Off', group: 'all', style: 'standard', icon: 'standard', short: 'Flat layers',
    sentence: 'Flat layers only, exactly like Bambu Studio or OrcaSlicer.', bestFor: 'print-in-place parts, threads, snap fits' },
  { id: 'smooth', name: STYLE_NAMES.smooth, keys: 'Auto, deform off', group: 'all', style: 'smooth', icon: 'smooth', short: 'Curves the top layers only',
    sentence: 'Curved top shells only, never whole-part curved layers.' },
  { id: 'zaa', name: STYLE_NAMES.zaa, keys: 'ZAA', group: 'all', style: 'zaa', icon: 'zaa', short: 'Flat layers, top lines follow surface',
    sentence: 'Flat layers, but each top line rises or dips onto the surface. No curved layers.', bestFor: 'very gentle slopes' },
  { id: 'curved-tops', name: 'Curved tops', keys: '', group: 'auto', icon: 'smooth', short: 'Domes, knobs, lids',
    sentence: 'The top few layers bend to follow the surface: no rings or steps.', number: ['Knob, A1 mini', 'top error 60 → 11 µm, +3% time'] },
  { id: 'flat-overhangs', name: 'Flat overhangs', keys: '', group: 'auto', icon: 'overhang', short: 'Ledges without support',
    sentence: 'Shelves and ledges grow outward from their wall in curved layers.', number: ['Shelf hook, A1', 'support 18.6 → 0 cm³, 99 → 53 min'] },
  { id: 'finer-walls', name: 'Finer outer walls', keys: '', group: 'auto', icon: 'walls', short: 'Half-height beads on steep walls',
    sentence: 'On steep walls the outer wall prints as two half-height beads.' },
  { id: 'curved-bridges', name: 'Curved bridges, ironing', keys: '', group: 'auto', icon: 'bridge', short: 'Arches, glossy curved tops',
    sentence: 'Bridges and ironing passes follow the curved surface.' },
  { id: 'multiplanar', name: STYLE_NAMES.multiplanar, keys: '5-axis', group: 'rotary', style: 'multiplanar', icon: 'chunks', short: 'Overhangs printed upright',
    sentence: 'The part is cut into chunks; the bed tilts so each prints its overhangs upright.', requirement: FIVE_AXIS },
  { id: 'field', name: STYLE_NAMES.field, keys: '5-axis', group: 'rotary', style: 'field', icon: 'rotary', short: 'Branches, upside-down overhangs',
    sentence: 'Every layer curved; the table tilts so each line is laid level.', requirement: 'two rotary axes (Prodcan3, FR3 cell)' },
  { id: 'wall-skin', name: 'Wall skin', keys: '5-axis', group: 'rotary', icon: 'skin', short: 'Nozzle tilts along chamfers',
    sentence: 'The nozzle tilts to lie along steep faces and chamfers.', number: ['Chamfers, Prodcan3', '2.3× smoother'], requirement: FIVE_AXIS },
  { id: 'lean-vase', name: 'Lean vase', keys: '5-axis', group: 'rotary', icon: 'vase', short: 'Flared vases in one spiral',
    sentence: 'The nozzle leans with a flaring wall, so a vase opens out in one spiral.', requirement: `Spiral vase on; ${FIVE_AXIS}` },
];

const GROUPS: { group: ModeEntry['group']; title: string }[] = [
  { group: 'all', title: 'All printers' },
  { group: 'auto', title: 'Auto can use' },
  { group: 'rotary', title: '5-axis printers (Prodcan3, FR3 cell)' },
];

const tooltip = (m: ModeEntry) => [
  m.sentence,
  m.number && `${m.number[0]}: ${m.number[1]}.`,
  m.bestFor && `Best for: ${m.bestFor}.`,
  m.avoidFor && `Not for: ${m.avoidFor}.`,
  m.requirement && `Needs: ${m.requirement}.`,
].filter(Boolean).join('\n');

export default function StyleGuide({ close, target, objectId }: { close(): void; target: Config; objectId?: number }) {
  const printer = useStore(printerPreset);
  const rotary = isRotary(printer?.config ?? {});
  const [current, setCurrent] = useState(() => styleOf(target));
  const pick = (id: StyleId) => { applyStyle(id, objectId); setCurrent(id); };
  return (
    <Modal size="m" label="Print styles" onClose={close} className="psg" backdropClose>
      <ModalTitle title="Print styles" onClose={close} />
      <div className="psg-body">
        <table className="psg-table">
          {GROUPS.map((g) => (
            <tbody key={g.group}>
              <tr className="psg-h"><th colSpan={4} scope="colgroup">{g.title}</th></tr>
              {MODES.filter((m) => m.group === g.group).map((m) => {
                const off = m.group === 'rotary' && !rotary;
                const selectable = !!m.style && !off;
                const active = !!m.style && m.style === current;
                return (
                  <tr key={m.id} className={`${off ? 'off' : ''} ${active ? 'active' : ''} ${selectable ? 'pick' : ''}`} title={tooltip(m)}
                    aria-selected={selectable ? active : undefined} onClick={selectable ? () => pick(m.style!) : undefined}>
                    <td className="i" dangerouslySetInnerHTML={{ __html: styleIcon(m.icon, 32) }} />
                    <td className="n">{selectable ? <button type="button" className="psg-pick" aria-pressed={active}>{m.name}</button> : m.name}</td>
                    <td className="d">{m.short}</td>
                    <td className="r">{m.group === 'rotary' ? m.keys : ''}</td>
                  </tr>
                );
              })}
            </tbody>
          ))}
        </table>
      </div>
      <div className="modal-buttons">
        <span style={{ flex: 1 }} />
        <button type="button" className="btn primary" data-autofocus onClick={close}>OK</button>
      </div>
    </Modal>
  );
}
