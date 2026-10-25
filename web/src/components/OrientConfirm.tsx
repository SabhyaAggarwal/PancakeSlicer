import { setState, useStore, type OrientSlice } from '../state/store';
import { Modal, ModalTitle } from './Modal';
import { supportText } from '../preview/explain-record';
import { shortTime } from '../preview/format';

const FLOAT_MARGIN_MM = 10;

const time = (s: OrientSlice | undefined) => (s?.time_s == null ? '…' : shortTime(s.time_s));
const support = (s: OrientSlice | undefined) => (s ? supportText(s.support_g) : '…');
const area = (mm2: number) => `${Math.round(mm2)} mm²`;
const floating = (s: OrientSlice | undefined) => (!s ? '…' : s.floating_mm == null ? '–' : `${s.floating_mm.toFixed(1)} mm`);

export function OrientConfirm() {
  const c = useStore((s) => s.orientConfirm);
  const name = useStore((s) => s.objects.find((o) => o.id === c?.id)?.name ?? '');
  if (!c) return null;
  const close = () => setState({ orientConfirm: null });
  const keep = () => setState((st) => {
    const orientPrompts = { ...st.orientPrompts };
    delete orientPrompts[c.id];
    return { orientConfirm: null, orientPrompts, orientDismissed: [...st.orientDismissed, c.id] };
  });
  const { original: a, standing: b } = c;
  const slower = a?.time_s != null && b?.time_s != null && b.time_s > a.time_s;
  const more = a?.floating_mm != null && b?.floating_mm != null ? b.floating_mm - a.floating_mm : null;
  const advice = more != null && more >= FLOAT_MARGIN_MM ? `Stood up, ${more.toFixed(0)} mm more road prints in the air: keep the original.`
    : slower && !(more != null && more <= -FLOAT_MARGIN_MM) ? 'Stood up, it prints slower: keep the original.' : null;
  return (
    <Modal label="Stand this part up?" onClose={close} className="small orient-confirm">
      <ModalTitle title={`Stand ${name} up?`} onClose={close} />
      <table className="orient-table">
        <thead><tr><th /><th>As loaded</th><th>Stood up</th></tr></thead>
        <tbody>
          <tr><td>Time</td><td>{time(a)}</td><td>{time(b)}</td></tr>
          {c.supportOn
            ? <tr><td>Support</td><td>{support(a)}</td><td>{support(b)}</td></tr>
            : <tr><td>Support needed (est.)</td><td>{area(c.supportArea[0])}</td><td>{area(c.supportArea[1])}</td></tr>}
          <tr><td>Floating</td><td>{floating(a)}</td><td>{floating(b)}</td></tr>
        </tbody>
      </table>
      {c.error ? <div className="orient-note">{c.error}</div> : !b ? <div className="orient-note">Slicing both…</div> : advice && <div className="orient-note">{advice}</div>}
      <div className="modal-buttons center">
        <button type="button" className={`btn ${advice ? 'primary' : ''}`} onClick={keep}>Keep original</button>
        <button type="button" className={`btn ${advice ? '' : 'primary'}`} disabled={!b} onClick={() => void import('../plater/orientPrompt').then((m) => m.standUp(c.id))}>Stand up</button>
      </div>
    </Modal>
  );
}
