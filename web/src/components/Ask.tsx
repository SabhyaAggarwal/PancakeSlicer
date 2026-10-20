import { useState, useSyncExternalStore } from 'react';
import { Modal, ModalTitle } from './Modal';

type Ask = { title: string; message?: string; value?: string; range?: [number, number]; resolve(v: string | null): void };
let current: Ask | null = null;
const listeners = new Set<() => void>();
const show = (a: Ask | null) => {
  current = a;
  listeners.forEach((l) => l());
};
const ask = (a: Omit<Ask, 'resolve'>) =>
  new Promise<string | null>((resolve) => {
    current?.resolve(null);
    show({ ...a, resolve });
  });

export const confirmDialog = (title: string, message: string) => ask({ title, message }).then((v) => v !== null);
export const promptDialog = (title: string, value: string) => ask({ title, value }).then((v) => v?.trim() || null);
export const numberDialog = (title: string, prompt: string, value: number, min: number, max: number) =>
  ask({ title, message: prompt, value: String(value), range: [min, max] }).then((v) => v === null || v.trim() === '' ? null : Math.min(max, Math.max(min, Math.round(Number(v)))))
    .then((n) => (n === null || Number.isNaN(n) ? null : n));

function AskDialog({ a }: { a: Ask }) {
  const [text, setText] = useState(a.value ?? '');
  const done = (v: string | null) => {
    show(null);
    a.resolve(v);
  };
  return (
    <Modal className="small" label={a.title} onClose={() => done(null)}>
      <form method="dialog" onSubmit={(e) => { e.preventDefault(); done(a.value === undefined ? '' : text); }}>
        <ModalTitle title={a.title} onClose={() => done(null)} />
        <div className="flush-body">
          {a.range ? (
            <label style={{ display: 'flex', alignItems: 'center', gap: 8 }}>{a.message}
              <input className="text num" type="number" min={a.range[0]} max={a.range[1]} step={1} value={text} data-autofocus onChange={(e) => setText(e.target.value)} />
            </label>
          ) : a.message && <div>{a.message}</div>}
          {a.value !== undefined && !a.range && <input className="text" aria-label={a.title} value={text} data-autofocus onChange={(e) => setText(e.target.value)} />}
        </div>
        <div className="modal-buttons center">
          <button type="submit" className="btn primary" data-autofocus={a.value === undefined ? '' : undefined}>OK</button>
          <button type="button" className="btn" onClick={() => done(null)}>Cancel</button>
        </div>
      </form>
    </Modal>
  );
}

export function AskHost() {
  const a = useSyncExternalStore((l) => { listeners.add(l); return () => void listeners.delete(l); }, () => current);
  return a ? <AskDialog key={a.title + a.value} a={a} /> : null;
}
