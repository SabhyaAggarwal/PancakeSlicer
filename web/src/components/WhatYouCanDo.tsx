import page from '../../../docs/webapp/WHAT_YOU_CAN_DO.md?raw';
import { Modal, ModalTitle } from './Modal';

const images = import.meta.glob<string>('../../../docs/webapp/images/what-you-can-do/*.webp', { eager: true, query: '?url', import: 'default' });

const items = page.split(/\n\s*\n/).slice(1).reduce<{ title: string; text: string; shots: [string, string][] }[]>((list, block) => {
  const item = /^\*\*(.+?)\*\*\s*(.*)$/s.exec(block.trim());
  if (item) list.push({ title: item[1].replace(/\.$/, ''), text: item[2], shots: [] });
  else for (const m of block.matchAll(/!\[([^\]]*)\]\(([^)]+)\)/g))
    list[list.length - 1]?.shots.push([m[1], images[`../../../docs/webapp/${m[2]}`]]);
  return list;
}, []);

export default function WhatYouCanDo({ close }: { close(): void }) {
  return (
    <Modal className="shortcuts-dialog" label="What you can do" onClose={close} backdropClose>
      <ModalTitle title="What you can do" onClose={close} />
      <div className="shortcuts-body" tabIndex={0}>
        {items.map((it) => (
          <section key={it.title}>
            <h3>{it.title}</h3>
            <p>{it.text.replace(/`/g, '')}</p>
            {it.shots.map(([alt, src]) => <img key={src} src={src} alt={alt} loading="lazy" style={{ width: '100%', border: '1px solid var(--line-ccc)' }} />)}
          </section>
        ))}
      </div>
    </Modal>
  );
}
