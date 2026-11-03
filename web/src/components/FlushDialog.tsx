import { useState } from 'react';
import { MAX_FLUSH_VOLUME, minFlushVolumes, MULTIPLIER_RANGE, parseColour, predictorMinFlush } from '../flush';
import { autoFlushMatrix, currentFlush, flushInputs } from '../flush-state';
import { markStale, setState, useStore } from '../state/store';
import { Modal, ModalBody, ModalFooter, ModalTitle } from './Modal';
import { undoable } from '../state/history';

const TYPICAL_MAX_FLUSH_VOLUME = 900;

const textOn = (hex: string) => {
  const [r, g, b] = parseColour(hex).rgb;
  return (0.299 * r + 0.587 * g + 0.114 * b) / 255 > 0.5 ? '#000' : '#fff';
};

export function FlushDialog() {
  const open = useStore((s) => s.flushDialog);
  if (!open) return null;
  return <FlushTable />;
}

function FlushTable() {
  const s = useStore((st) => st);
  const { colours, printer, filaments } = flushInputs(s);
  const n = colours.length;
  const init = currentFlush(s);
  const [raw, setRaw] = useState<number[]>(init.matrix);
  const [mult, setMult] = useState(init.multiplier);
  const [multText, setMultText] = useState(String(init.multiplier));
  const auto = autoFlushMatrix(s);
  const dataset = Number((Array.isArray(printer.nozzle_flush_dataset) ? printer.nozzle_flush_dataset[0] : printer.nozzle_flush_dataset) ?? 0) || 0;
  const minVol = Math.min(predictorMinFlush(dataset), ...minFlushVolumes(printer, filaments));
  const close = () => setState({ flushDialog: false });

  const shown = (i: number) => Math.min(MAX_FLUSH_VOLUME, Math.max(0, Math.round(raw[i] * mult)));
  const onCell = (i: number, text: string) => {
    let v = parseInt(text, 10);
    if (!Number.isFinite(v) || v < 0) v = 0;
    v = Math.min(v, MAX_FLUSH_VOLUME);
    const next = [...raw];
    next[i] = mult === 0 ? 0 : Math.round(v / mult);
    setRaw(next);
  };
  const onMult = (text: string) => {
    setMultText(text);
    let v = parseFloat(text);
    if (!Number.isFinite(v)) v = 1;
    v = Math.min(MULTIPLIER_RANGE[1], Math.max(MULTIPLIER_RANGE[0], v));
    setMult(v);
  };
  const save = () => {
    const limit = mult > 0 ? MAX_FLUSH_VOLUME / mult : MAX_FLUSH_VOLUME;
    const matrix = raw.map((v) => Math.min(Math.max(v, 0), limit));
    const same = matrix.every((v, i) => v === auto[i]);
    const matrixChanged = raw.some((v, i) => v !== init.matrix[i]);
    undoable('Flushing volumes', () => setState((st) => ({
      flush: {
        matrix: st.project && !matrixChanged ? st.flush.matrix : same && !st.project ? null : matrix,
        multiplier: st.project && mult === init.multiplier ? st.flush.multiplier : mult,
      },
      flushDialog: false,
      job: markStale(st.job),
    })));
  };
  const outOfRange = raw.some((_, i) => Math.floor(i / n) !== i % n && (shown(i) < minVol || shown(i) > MAX_FLUSH_VOLUME));

  return (
    <Modal size="l" label="Flushing volumes" onClose={close}>
        <ModalTitle title="Flushing volumes" onClose={close} />
        <ModalBody>
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
                    <th key={j}>
                      <span className="flush-chip" style={{ background: c, color: textOn(c) }}>
                        {j + 1}
                      </span>
                    </th>
                  ))}
                </tr>
              </thead>
              <tbody>
                {colours.map((c, i) => (
                  <tr key={i}>
                    <th>
                      <span className="flush-chip" style={{ background: c, color: textOn(c) }}>
                        {i + 1}
                      </span>
                    </th>
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
          <p className={outOfRange ? 'modal-error' : 'flush-note'} role={outOfRange ? 'alert' : undefined}>mm³ from row to column, typically {minVol}–{TYPICAL_MAX_FLUSH_VOLUME}</p>
        </ModalBody>
        <ModalFooter lead={<button type="button" className="btn-ghost" onClick={() => setRaw(auto)}>Recalculate</button>}>
          <button type="button" className="btn" onClick={close}>Cancel</button>
          <button type="button" className="btn-primary" onClick={save}>Save</button>
        </ModalFooter>
    </Modal>
  );
}
