import { setState, useStore } from '../state/store';
import { Modal, ModalTitle } from './Modal';
import { SettingsTab } from './SettingsTab';

export function PresetDialog() {
  const dialog = useStore((s) => s.dialog);
  if (!dialog) return null;
  const title = dialog.type === 'machine' ? 'Printer settings' : 'Material settings';
  const close = () => setState({ dialog: null });
  return (
    <Modal size="window" label={title} onClose={close} backdropClose>
      <ModalTitle title={title} onClose={close} />
      <SettingsTab type={dialog.type} slot={dialog.slot} />
    </Modal>
  );
}
