import { preferenceText, savePreferences, usePreference } from '../saved-preferences';
import { Check } from './Check';
import { useState } from 'react';
import { arrangeAll, fillBed } from '../actions';
import { copySelection, pasteClipboard } from '../edit';
import { beginStep } from '../state/history';
import { Modal, ModalBody, ModalFooter, ModalTitle } from './Modal';

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
    <Modal size="m" label="Clone" onClose={close}>
      <ModalTitle title="Clone" onClose={close} />
      <ModalBody className="modal-fields">
        <label className="form-row"><span>Copies</span>
          <input className="text num" type="number" min={1} max={1000} value={count}
            onChange={(e) => setCount(Math.min(1000, Math.max(1, Math.round(Number(e.target.value) || 1))))}
            onKeyDown={(e) => e.key === 'Enter' && ok()} />
        </label>
        <label className="modal-check"><Check checked={arrange} onCheckedChange={(next) => setArrange(next)} />Arrange the plate afterwards</label>
      </ModalBody>
      <ModalFooter lead={<button type="button" className="btn-ghost" onClick={() => { fillBed(); close(); }}>Fill the plate</button>}>
        <button type="button" className="btn" onClick={close}>Cancel</button>
        <button type="button" className="btn-primary" onClick={ok}>Clone</button>
      </ModalFooter>
    </Modal>
  );
}
