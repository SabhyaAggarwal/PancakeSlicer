import { useState } from 'react';
import { Dropdown } from './Dropdown';
import { effectiveConfig, printerPreset } from '../state/store';
import { extruderCount, extruderLabel } from '../multinozzle/config';
import { MAX_FLUSH_VOLUME, minFlushVolumes, MULTIPLIER_RANGE, predictorMinFlush } from '../flush';
import { allFlush, autoFlushMatrix, flushInputs } from '../flush-state';
import { markStale, setState, useStore } from '../state/store';
import { Modal, ModalBody, ModalFooter, ModalTitle } from './Modal';
import { SlotChip } from './SlotChip';
import { undoable } from '../state/history';

const TYPICAL_MAX_FLUSH_VOLUME = 900;

export function FlushDialog() {
  const open = useStore((s) => s.flushDialog);
  if (!open) return null;
  return <FlushTable />;
}

function FlushTable() {
  const s = useStore((st) => st);
  const [nozzle, setNozzle] = useState(0);
  const count = extruderCount(effectiveConfig(s, 'machine'));
  const { colours, printer, filaments } = flushInputs(s, nozzle);
  const n = colours.length;
  const init = allFlush(s);
  const [raw, setRaw] = useState<number[]>(init.matrix);
  const [multipliers, setMultipliers] = useState(init.multipliers);
  const [multTexts, setMultTexts] = useState(init.multipliers.map(String));
  const mult = multipliers[nozzle], multText = multTexts[nozzle];
  const auto = autoFlushMatrix(s, nozzle);
  const offset = nozzle * n * n;
  const dataset = Number((Array.isArray(printer.nozzle_flush_dataset) ? printer.nozzle_flush_dataset[0] : printer.nozzle_flush_dataset) ?? 0) || 0;
  const minVol = Math.min(predictorMinFlush(dataset), ...minFlushVolumes(printer, filaments));
  const close = () => setState({ flushDialog: false });

  const shown = (i: number) => Math.min(MAX_FLUSH_VOLUME, Math.max(0, Math.round(raw[offset + i] * mult)));
  const onCell = (i: number, text: string) => {
    let v = parseInt(text, 10);
    if (!Number.isFinite(v) || v < 0) v = 0;
    v = Math.min(v, MAX_FLUSH_VOLUME);
    const next = [...raw];
    next[offset + i] = mult === 0 ? 0 : Math.round(v / mult);
    setRaw(next);
  };
  const onMult = (text: string) => {
    setMultTexts((texts) => texts.map((old, i) => i === nozzle ? text : old));
    let v = parseFloat(text);
    if (!Number.isFinite(v)) v = 1;
    v = Math.min(MULTIPLIER_RANGE[1], Math.max(MULTIPLIER_RANGE[0], v));
    setMultipliers((values) => values.map((old, i) => i === nozzle ? v : old));
  };
  const save = () => {
    const matrix = raw.map((v, i) => Math.min(Math.max(v, 0), multipliers[Math.floor(i / (n * n))] > 0 ? MAX_FLUSH_VOLUME / multipliers[Math.floor(i / (n * n))] : MAX_FLUSH_VOLUME));
    const autoAll = Array.from({ length: count }, (_, i) => autoFlushMatrix(s, i)).flat();
    const same = matrix.every((v, i) => v === autoAll[i]);
    const matrixChanged = matrix.some((v, i) => v !== init.matrix[i]);
    undoable('Flushing volumes', () => setState((st) => ({
      flush: { matrix: st.project && !matrixChanged ? st.flush.matrix : same && !st.project ? null : matrix, multiplier: null },
      dirty: { ...st.dirty, process: { ...st.dirty.process, flush_multiplier: multipliers } },
      flushDialog: false,
      job: markStale(st.job),
    })));
  };
  const outOfRange = raw.slice(offset, offset + n * n).some((_, i) => Math.floor(i / n) !== i % n && (shown(i) < minVol || shown(i) > MAX_FLUSH_VOLUME));

  return (
    <Modal size={n > 6 ? 'l' : 'm'} label="Flushing volumes" onClose={close}>
        <ModalTitle title="Flushing volumes" onClose={close} />
        <ModalBody>
          {count > 1 && <label className="form-row"><span>Nozzle</span><Dropdown className="combo" aria-label="Flushing nozzle" value={String(nozzle)} onValueChange={(value) => setNozzle(Number(value))}
            options={Array.from({ length: count }, (_, i) => ({ value: String(i), label: extruderLabel(i, count, printerPreset(s)?.vendor === 'BBL') }))} /></label>}
          <label className="form-row"><span>Multiplier</span>
            <input type="number" step="0.1" min={MULTIPLIER_RANGE[0]} max={MULTIPLIER_RANGE[1]} className={`text num multiplier-input ${mult !== 1 ? 'changed' : ''}`}
              value={multText} onChange={(e) => onMult(e.target.value)} /></label>
          <div className="flush-scroll">
            <table className="flush-table">
              <thead>
                <tr>
                  <th>
                    <span className="flush-corner" aria-label="From / to" />
                  </th>
                  {colours.map((c, j) => (
                    <th key={j} aria-label={`To filament ${j + 1}`}><SlotChip n={j + 1} colour={c} /></th>
                  ))}
                </tr>
              </thead>
              <tbody>
                {colours.map((c, i) => (
                  <tr key={i}>
                    <th aria-label={`From filament ${i + 1}`}><SlotChip n={i + 1} colour={c} /></th>
                    {colours.map((_, j) => {
                      const k = i * n + j;
                      const v = i === j ? 0 : shown(k);
                      const def = Math.round(auto[k] * mult);
                      const cls = i === j ? '' : v < minVol || v > MAX_FLUSH_VOLUME ? 'bad' : v !== def ? 'changed' : '';
                      return (
                        <td key={j}>
                          <input
                            type="number"
                            className={`table-input ${cls}`}
                            value={v}
                            readOnly={i === j}
                            onChange={(e) => onCell(k, e.target.value)}
                            aria-label={`from ${i + 1} to ${j + 1}`}
                          />
                        </td>
                      );
                    })}
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
          <p className={outOfRange ? 'modal-error' : 'flush-note'} role={outOfRange ? 'alert' : undefined}>mm³ flushed when switching from row to column, typically {minVol}–{TYPICAL_MAX_FLUSH_VOLUME}</p>
        </ModalBody>
        <ModalFooter lead={<button type="button" className="btn-ghost" onClick={() => setRaw((values) => values.map((old, i) => i >= offset && i < offset + n * n ? auto[i - offset] : old))}>Recalculate</button>}>
          <button type="button" className="btn" onClick={close}>Cancel</button>
          <button type="button" className="btn-primary" onClick={save}>Save</button>
        </ModalFooter>
    </Modal>
  );
}
