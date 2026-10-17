import { useEffect } from 'react';
import { setState, useStore } from '../state/store';
import { SettingsTab } from './SettingsTab';

export function PresetDialog() {
  const dialog = useStore((s) => s.dialog);
  useEffect(() => {
    if (!dialog) return;
    const onKey = (e: KeyboardEvent) => e.key === 'Escape' && setState({ dialog: null });
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, [dialog]);
  if (!dialog) return null;
  const title = dialog.type === 'machine' ? 'Printer settings' : 'Material settings';
  return (
    <div className="modal-backdrop" onMouseDown={(e) => e.target === e.currentTarget && setState({ dialog: null })}>
      <div className="modal" role="dialog" aria-label={title}>
        <div className="modal-title">
          <span>{title}</span>
          <button className="modal-close" title="Close" onClick={() => setState({ dialog: null })}>
            ×
          </button>
        </div>
        <SettingsTab type={dialog.type} slot={dialog.slot} />
      </div>
    </div>
  );
}
