import { useEffect, useState } from 'react';
import { Modal, ModalTitle } from './Modal';
import { offer, remove, restore, sessions, type Session } from '../persist/session';
import { setState, useStore } from '../state/store';

const when = (t: number) => new Date(t).toLocaleString(undefined, { dateStyle: 'medium', timeStyle: 'short' });
const summary = (s: Session) => `${s.objects} object${s.objects === 1 ? '' : 's'} · ${when(s.savedAt)}`;

function Thumb({ s }: { s: Session }) {
  return s.thumbnail ? <img className="session-thumb" src={s.thumbnail} alt="" /> : <div className="session-thumb empty" />;
}

export default function SessionDialog() {
  const mode = useStore((s) => s.sessionDialog);
  const [list, setList] = useState<Session[] | null>(null);
  const close = () => setState({ sessionDialog: null });
  useEffect(() => {
    if (mode === 'recent') void sessions().then(setList);
  }, [mode]);

  if (mode === 'restore' && offer) {
    const s = offer;
    return (
      <Modal className="small" label="Restore your last session?" onClose={close}>
          <ModalTitle title="Restore your last session?" onClose={close} />
          <div className="session-offer">
            <Thumb s={s} />
            <div>
              <div className="session-name">{s.name}</div>
              <div className="session-meta">{summary(s)}</div>
            </div>
          </div>
          <div className="modal-buttons center">
            <button className="btn primary" data-autofocus onClick={() => void restore(s.id)}>Restore</button>
            <button className="btn" onClick={close}>Start new</button>
          </div>
      </Modal>
    );
  }
  return (
    <Modal className="small" label="Recent projects" onClose={close} backdropClose>
        <ModalTitle title="Recent projects" onClose={close} />
        <div className="session-list">
          {list === null ? <div role="status">Loading…</div>
            : !list.length ? <div className="session-meta">No recent projects</div>
            : list.map((s) => (
              <div key={s.id} className="session-row">
                <button className="session-open" title={`Open ${s.name}`} onClick={() => void restore(s.id)}>
                  <Thumb s={s} />
                  <span>
                    <span className="session-name">{s.name}</span>
                    <span className="session-meta">{summary(s)}</span>
                  </span>
                </button>
                <button className="icon-btn opt-remove" title="Remove from the list" aria-label={`Remove ${s.name}`}
                  onClick={() => void remove(s.id).then(sessions).then(setList)}>×</button>
              </div>
            ))}
        </div>
    </Modal>
  );
}
