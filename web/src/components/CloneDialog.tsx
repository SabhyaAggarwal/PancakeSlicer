import { preferenceText, savePreferences, usePreference } from '../saved-preferences';
import { Check } from './Check';
import { useState } from 'react';
import { arrangeAll, fillBed } from '../actions';
import { copySelection, pasteClipboard } from '../edit';
import { beginStep } from '../state/history';
import { Modal, ModalTitle } from './Modal';

const KEY = 'orca.web.cloneArrange';

export default function CloneDialog({ close }: { close(): void }) {
  const [count, setCount] = useState(1);
  usePreference(KEY);
  const [choice, setArrange] = useState<boolean | null>(null);
  const arrange = choice ?? (preferenceText(KEY) === 'true');
  const ok = () => {
    savePreferences({[KEY]: String(arrange)});
    const end = beginStep('Selection-clone');
    try {
      copySelection();
      for (let i = 0; i < count; i++) pasteClipboard();
    } finally {
      end();
    }
    if (arrange) arrangeAll(false);
    close();
  };
  return (
    <Modal className="small" label="Clone" onClose={close}>
      <ModalTitle title="Clone" onClose={close} />
      <div className="prefs-body">
        <label className="prefs-row"><span>Number of copies:</span>
          <input className="text num" type="number" min={1} max={1000} value={count} data-autofocus
            onChange={(e) => setCount(Math.min(1000, Math.max(1, Math.round(Number(e.target.value) || 1))))}
            onKeyDown={(e) => e.key === 'Enter' && ok()} />
        </label>
        <label className="prefs-row"><span>Auto arrange plate after cloning:</span>
          <Check checked={arrange} onCheckedChange={(next) => setArrange(next)} />
        </label>
      </div>
      <div className="modal-buttons">
        <button className="btn" title="Fill bed with copies" onClick={() => { fillBed(); close(); }}>Fill</button>
        <span style={{ flex: 1 }} />
        <button className="btn primary" onClick={ok}>OK</button>
        <button className="btn" onClick={close}>Cancel</button>
      </div>
    </Modal>
  );
}
