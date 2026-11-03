import { Check } from './Check';
import { Range } from './Range';
import { Dropdown } from './Dropdown';
import { useEffect, useState } from 'react';
import { ActionMenu } from './Menu';
import { getState, useStore, type Mix, type State } from '../state/store';
import { addMix, blend, broken, componentColour, deleteMixes, editMix, filamentType, physicalCount, recommendations, SLOTS_MAX, watchColours } from '../plater/mixed';
import { filamentColor } from '../plater/filamentColor';
import { confirmDialog } from '../actions';
import { IconButton } from './Icon';
import { Modal, ModalTitle } from './Modal';
import './MixedFilaments.css';

const MIN_RATIO = 10;
const ramp = (s: State, m: Mix) => {
  const [a, b] = m.components.map((c) => componentColour(s, c));
  return `linear-gradient(to bottom, ${m.gradient ? b : a}, ${m.gradient ? a : b})`;
};
const swatch = (s: State, m: Mix, i: number) => (m.gradient !== null && m.components.length === 2 ? ramp(s, m) : filamentColor(s, i + 1));

export default function MixedFilaments() {
  const mixes = useStore((s) => s.mixes);
  useStore((s) => s.slotColours);
  useStore((s) => s.filamentSlots);
  const s = getState();
  const [dialog, setDialog] = useState<{ slot: number | null } | null>(null);
  const [menu, setMenu] = useState<{ slot: number; x: number; y: number } | null>(null);
  useEffect(watchColours, []);
  const slots = Object.keys(mixes).map(Number).sort((a, b) => a - b);
  const full = s.filamentSlots.length >= SLOTS_MAX;
  const removeAll = () => void confirmDialog('Mixed Filament', `Remove all ${slots.length} mixed filaments?`).then((ok) => ok && deleteMixes(slots, undefined, 'Remove all mixed filaments'));
  return (
    <div className="mixed-filaments">
      {!slots.length ? (
        <button type="button" className="mixed-add" disabled={full} onClick={() => setDialog({ slot: null })}>+ Add Mixed Filament</button>
      ) : (
        <>
          <div className="mixed-title">
            <span>Mixed Filament</span>
            <IconButton icon="delete_filament" title="Remove last mixed filament" onClick={() => deleteMixes([slots[slots.length - 1]])} />
            <IconButton icon="delete_all_filaments" title="Remove all mixed filaments" onClick={removeAll} />
            <IconButton icon="add_filament" title="Add mixed filament" disabled={full} onClick={() => setDialog({ slot: null })} />
          </div>
          <div className="mixed-list">
            {slots.map((i) => {
              const m = mixes[i], bad = broken(s, m);
              return (
                <div key={i} className={`mixed-row ${bad ? 'broken' : ''}`}>
                  <button type="button" className="filament-swatch" style={{ background: swatch(s, m, i) }} onClick={() => setDialog({ slot: i })}>{i + 1}</button>
                  <button type="button" className="mixed-content" title={m.components.map((c, k) => `${c} (${m.ratios[k]}%)`).join(' + ')} onClick={() => setDialog({ slot: i })}>
                    {m.components.map((c, k) => (
                      <span key={k}>{k > 0 && (m.gradient !== null ? '→' : '+')}<i style={{ background: componentColour(s, c) }}>{c || '?'}</i></span>
                    ))}
                    {m.gradient === null && <em>{m.ratios.map((r) => `${r}%`).join(' ')}</em>}
                  </button>
                  <IconButton icon={bad ? 'error' : 'menu_filament'} title={bad ? 'Mixed filament has broken component references' : 'Edit / Delete / Merge'}
                    onClick={(e) => { const r = e.currentTarget.getBoundingClientRect(); setMenu(menu?.slot === i ? null : { slot: i, x: Math.min(r.left, window.innerWidth - 230), y: r.bottom }); }} />
                </div>
              );
            })}
          </div>
        </>
      )}
      {menu && <ActionMenu label={`Filament ${menu.slot + 1}`} at={menu} onClose={() => setMenu(null)} items={[
        { label: 'Mixed filament', header: true },
        { label: 'Edit', action: () => setDialog({ slot: menu.slot }) },
        { label: 'Merge with', sub: s.filamentSlots.flatMap((_, j) => j === menu.slot ? [] : [{ label: `Filament ${j + 1}`,
          icon: <i className="mixed-dot" style={{ background: filamentColor(s, j + 1) }} />,
          action: () => deleteMixes([menu.slot], j + 1, 'Merge filament') }]) },
        'sep', { label: 'Delete', action: () => deleteMixes([menu.slot]) },
      ]} />}
      {dialog && <MixDialog slot={dialog.slot} close={() => setDialog(null)} />}
    </div>
  );
}

function MixDialog({ slot, close }: { slot: number | null; close(): void }) {
  const s = getState();
  const n = physicalCount(s);
  const [m, setM] = useState<Mix>(() => (slot !== null && s.mixes[slot]) || { components: [1, 2], ratios: [50, 50], gradient: null, perPart: false });
  const three = m.components.length === 3;
  const colours = m.components.map((c) => componentColour(s, c));
  const types = m.components.map((c) => (c >= 1 && c <= n ? filamentType(s, c) : ''));
  const mismatch = new Set(types.filter(Boolean)).size > 1;
  const unset = m.components.some((c) => c < 1 || c > n);
  const setRatio = (k: number, v: number) => {
    if (!three) return setM({ ...m, ratios: k ? [100 - v, v] : [v, 100 - v] });
    const r = [...m.ratios];
    r[k] = Math.max(MIN_RATIO, Math.min(100 - 2 * MIN_RATIO, v));
    const o = [0, 1, 2].filter((j) => j !== k), rest = 100 - r[k], had = r[o[0]] + r[o[1]] || 1;
    r[o[0]] = Math.max(MIN_RATIO, Math.min(rest - MIN_RATIO, Math.round((r[o[0]] * rest) / had)));
    r[o[1]] = rest - r[o[0]];
    setM({ ...m, ratios: r });
  };
  const preview = m.gradient !== null && !three
    ? `linear-gradient(to right, ${m.gradient ? colours[1] : colours[0]}, ${m.gradient ? colours[0] : colours[1]})`
    : `repeating-linear-gradient(to bottom, ${m.components.flatMap((_, k) => { const at = m.ratios.slice(0, k).reduce((a, b) => a + b, 0) / 10; return [`${colours[k]} ${at}px`, `${colours[k]} ${at + m.ratios[k] / 10}px`]; }).join(', ')})`;
  const ok = () => { if (slot === null) addMix(m); else editMix(slot, m); close(); };
  const recs = recommendations(s, three);
  return (
    <Modal label={slot === null ? 'Add Mixed Filament' : 'Edit Mixed Filament'} onClose={close} className="small mixed-dialog">
      <ModalTitle title={slot === null ? 'Add Mixed Filament' : 'Edit Mixed Filament'} onClose={close} />
      <div className="prefs-body">
        <div className="mixed-section">Effect Preview</div>
        <div className="mixed-preview" style={{ background: preview }}><i style={{ background: unset ? '#808080' : blend(colours, m.ratios) }} /></div>
        <div className="mixed-section">Select Mixed Materials</div>
        {m.components.map((c, k) => (
          <label key={k} className="prefs-row"><span>Filament {k + 1}</span>
            <Dropdown className="combo" value={c} onValueChange={(next) => setM({ ...m, components: m.components.map((x, j) => (j === k ? Number(next) : x)) })} options={[...(!c ? [{ value: 0, label: '-- Select --' }] : []), ...Array.from({ length: n }, (_, j) => j + 1).filter((j) => j === c || !m.components.includes(j)).map((j) => (
                ({ value: j, label: <>{j}: {s.filamentSlots[j - 1]}</> })
              ))]} />
          </label>
        ))}
        <div className="mixed-buttons">
          {!three && <button type="button" className="btn" disabled={n < 3} title={n < 3 ? 'Maximum number of components reached' : undefined}
            onClick={() => setM({ ...m, components: [...m.components, 0], ratios: [33, 33, 34], gradient: null, perPart: false })}>+ Add Material</button>}
          {three && <button type="button" className="btn" title="Remove the third material" onClick={() => setM({ ...m, components: m.components.slice(0, 2), ratios: [50, 50] })}>- Delete Material</button>}
        </div>
        {(m.gradient === null || three) && <>
          <div className="mixed-section">Ratio</div>
          {three ? m.components.map((c, k) => (
            <label key={k} className="prefs-row"><span><i className="mixed-dot" style={{ background: colours[k] }} />{c || '?'}</span>
              <input className="text num" type="number" min={MIN_RATIO} max={100 - 2 * MIN_RATIO} value={m.ratios[k]} onChange={(e) => setRatio(k, Number(e.target.value))} /><span>%</span>
            </label>
          )) : (
            <label className="prefs-row mixed-ratio">
              <span>{m.ratios[0]}%</span>
              <Range min={MIN_RATIO} max={100 - MIN_RATIO} step={1} value={m.ratios[1]} aria-label="Ratio"
                style={{ accentColor: colours[1] }} onValueChange={(next) => setRatio(1, next)} />
              <span>{m.ratios[1]}%</span>
            </label>
          )}
        </>}
        {!three && <>
          <label className="prefs-row"><span>Gradient Effect</span>
            <Check checked={m.gradient !== null} onCheckedChange={(next) => setM({ ...m, gradient: next ? 0 : null, perPart: false })} />
            {m.gradient !== null && (
              <Dropdown className="combo" value={m.gradient} aria-label="Gradient direction" onValueChange={(next) => setM({ ...m, gradient: Number(next) as 0 | 1 })} options={[({ value: 0, label: <>{m.components[0]} → {m.components[1]}</> }), ({ value: 1, label: <>{m.components[1]} → {m.components[0]}</> })]} />
            )}
          </label>
          {m.gradient !== null && (
            <label className="prefs-row"><span>Enable per-part gradient effect</span>
              <Check checked={m.perPart} onCheckedChange={(next) => setM({ ...m, perPart: next })} />
            </label>
          )}
        </>}
        {recs.length > 0 && <>
          <div className="mixed-section">Mixing Recommendations</div>
          <div className="mixed-recs">
            {recs.map((r, k) => (
              <button key={k} type="button" className="mixed-rec" title={r.components.map((c, j) => `${c} (${r.ratios[j]}%)`).join(' + ')}
                style={{ background: blend(r.components.map((c) => componentColour(s, c)), r.ratios) }} onClick={() => setM(r)} />
            ))}
          </div>
        </>}
        {mismatch && <div className="mixed-error">Different filament types cannot be mixed: {m.components.map((c, k) => `Slot ${c} (${types[k]})`).join(', ')}. Please select the same filament type.</div>}
      </div>
      <div className="modal-buttons">
        <button className="btn" onClick={close}>Cancel</button>
        <button className="btn primary" disabled={unset || mismatch} title={unset ? 'Please select a filament for all components' : mismatch ? 'Cannot mix different filament types' : undefined} onClick={ok}>OK</button>
      </div>
    </Modal>
  );
}
