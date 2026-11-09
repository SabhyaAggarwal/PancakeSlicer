const pages = import.meta.glob<string>('../../../docs/webapp/WHAT_YOU_CAN_DO.md', { eager: true, query: '?raw', import: 'default' });
const images = import.meta.glob<string>('../../../docs/webapp/images/what-you-can-do/*.webp', { eager: true, query: '?url', import: 'default' });

export interface WhatItem { title: string; text: string; shots: [string, string][] }

export const whatItems: WhatItem[] = (Object.values(pages)[0] ?? '').split(/\n\s*\n/).slice(1).reduce<WhatItem[]>((list, block) => {
  const item = /^\*\*(.+?)\*\*\s*(.*)$/s.exec(block.trim());
  if (item) list.push({ title: item[1].replace(/\.$/, ''), text: item[2], shots: [] });
  else for (const m of block.matchAll(/!\[([^\]]*)\]\(([^)]+)\)/g))
    list[list.length - 1]?.shots.push([m[1], images[`../../../docs/webapp/${m[2]}`]]);
  return list;
}, []);

export const hasWhatYouCanDo = whatItems.length > 0;
