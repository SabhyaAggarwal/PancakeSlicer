import { useEffect, useRef, useState } from 'react';
import { Modal, ModalTitle } from './Modal';
import { whatItems as items } from './what-you-can-do-page';
import './what-you-can-do.css';

export default function WhatYouCanDo({ close }: { close(): void }) {
  const [i, setI] = useState(0);
  const it = items[i];
  useEffect(() => { if (!it) close(); }, [it, close]);
  const go = (n: number) => setI((n + items.length) % items.length);
  const rail = useRef<HTMLOListElement>(null);
  useEffect(() => { if (rail.current?.contains(document.activeElement)) rail.current.querySelector<HTMLElement>('.active')?.focus(); }, [i]);
  if (!it) return null;
  return (
    <Modal size="xl" className="wycd-dialog" label="What you can do" onClose={close} backdropClose>
      <ModalTitle title="What you can do" onClose={close} />
      <div className="wycd" onKeyDown={(e) => {
        if (e.key === 'ArrowRight' || e.key === 'ArrowDown') { e.preventDefault(); go(i + 1); }
        else if (e.key === 'ArrowLeft' || e.key === 'ArrowUp') { e.preventDefault(); go(i - 1); }
      }}>
        <ol ref={rail} className="wycd-steps" role="tablist" aria-orientation="vertical">
          {items.map((x, n) => (
            <li key={x.title}>
              <button type="button" role="tab" aria-selected={n === i} tabIndex={n === i ? 0 : -1} data-autofocus={n === 0 || undefined}
                className={n === i ? 'active' : ''} onClick={() => setI(n)}>
                <span className="wycd-n">{n + 1}</span>{x.title}
              </button>
            </li>
          ))}
        </ol>
        <section className="wycd-slide" role="tabpanel" aria-label={it.title}>
          <div className="wycd-shots">
            {it.shots.map(([alt, src]) => <img key={src} src={src} alt={alt} />)}
          </div>
          <p>{it.text.replace(/`/g, '')}</p>
          <div className="wycd-nav">
            <button type="button" className="btn-ghost" onClick={() => go(i - 1)} aria-label="Previous">←</button>
            <span className="num">{i + 1} / {items.length}</span>
            <button type="button" className="btn-ghost" onClick={() => go(i + 1)} aria-label="Next">→</button>
          </div>
        </section>
      </div>
    </Modal>
  );
}
