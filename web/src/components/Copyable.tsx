

import type { ReactNode } from 'react';
import { toast } from '../state/store';

export function copyText(text: string) {
  void navigator.clipboard?.writeText(text).then(() => toast(`Copied ${text}`), () => toast('Copy blocked by the browser'));
}

export function Copyable({ value, children }: { value: string; children?: ReactNode }) {
  const copy = () => { if (!window.getSelection()?.toString()) copyText(value); };
  return (
    <button type="button" className="copyable" title="Copy" onClick={copy}>{children ?? value}</button>
  );
}
