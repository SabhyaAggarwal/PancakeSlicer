import { Component, useEffect, useRef, useState, type DragEvent, type KeyboardEvent, type ReactNode } from 'react';
import { ActionMenu } from '../components/Menu';
import { toast } from '../state/store';
import { useProject } from '../flow/project';
import { deleteProject, duplicateProject, openProject, renameProject, useProjects, type ProjectMeta } from './api';
import { fileVerbs, newProjectFrom } from './verbs';
import { setProjectsQuery, useProjectsQuery } from './query';
import './projects.css';

const ago = new Intl.RelativeTimeFormat(undefined, { numeric: 'auto', style: 'short' });
function edited(t: number) {
  const s = (t - Date.now()) / 1000;
  if (s > -60) return ago.format(0, 'minute');
  if (s > -3600) return ago.format(Math.round(s / 60), 'minute');
  if (s > -86400) return ago.format(Math.round(s / 3600), 'hour');
  if (s > -7 * 86400) return ago.format(Math.round(s / 86400), 'day');
  return new Date(t).toLocaleDateString();
}

async function remove(p: ProjectMeta) {
  const undo = await deleteProject(p.id);
  if (!undo) return;
  toast(`${p.name} deleted`, { label: 'Undo', run: () => void undo() }, 10000);
}

function CardMenu({ at, close, rename, project }: { at: { x: number; y: number }; close(): void; rename(): void; project: ProjectMeta }) {
  return <ActionMenu label="Project actions" at={at} onClose={close} items={[{label: 'Rename', shortcut: 'F2', action: rename}, {label: 'Duplicate', action: () => void duplicateProject(project.id)}, {label: 'Delete', shortcut: 'Del', action: () => void remove(project)}]} />;
}

function Card({ p, open, renaming, setRenaming, onMenu }: { p: ProjectMeta; open: boolean; renaming: boolean; setRenaming(on: boolean): void; onMenu(at: { x: number; y: number }): void }) {
  const commit = (name: string) => {
    setRenaming(false);
    if (name.trim() && name.trim() !== p.name) void renameProject(p.id, name.trim());
  };
  return (
    <div className={`pj-card ${open ? 'open' : ''}`} role="listitem" tabIndex={0} data-id={p.id} aria-label={p.name} aria-current={open || undefined}
      onClick={() => !renaming && void openProject(p.id)}
      onContextMenu={(e) => { e.preventDefault(); onMenu({ x: e.clientX, y: e.clientY }); }}>
      <div className="pj-thumb">{p.thumbnail && <img src={p.thumbnail} alt="" draggable={false} />}</div>
      <div className="pj-meta">
        {renaming ? (
          <input className="pj-rename" autoFocus defaultValue={p.name} aria-label="Name" onClick={(e) => e.stopPropagation()}
            onFocus={(e) => e.currentTarget.select()} onBlur={(e) => commit(e.currentTarget.value)}
            onKeyDown={(e) => {
              e.stopPropagation();
              if (e.key === 'Enter') commit(e.currentTarget.value);
              else if (e.key === 'Escape') setRenaming(false);
            }} />
        ) : <span className="pj-name">{p.name}</span>}
        <span className="pj-time">{p.objects === 1 ? '1 object' : `${p.objects} objects`} · {edited(p.savedAt)}</span>
      </div>
      <button type="button" className="icon-btn pj-more" aria-label="More" aria-haspopup="menu" tabIndex={-1}
        onClick={(e) => { e.stopPropagation(); const r = e.currentTarget.getBoundingClientRect(); onMenu({ x: r.left, y: r.bottom + 2 }); }}>
        ⋯
      </button>
    </div>
  );
}

class Boundary extends Component<{ children: ReactNode }, { failed: boolean }> {
  state = { failed: false };
  static getDerivedStateFromError() { return { failed: true }; }
  componentDidCatch(e: unknown) { console.warn('Projects', e); }
  render() {
    if (!this.state.failed) return this.props.children;
    return (
      <div className="projects"><div className="pj-empty">
        <span>Projects unavailable</span>
        <button type="button" className="btn-ghost" onClick={() => this.setState({ failed: false })}>Retry</button>
        <button type="button" className="btn-primary" onClick={fileVerbs.new}>New project</button>
      </div></div>
    );
  }
}

export default function ProjectsView() {
  return <Boundary><Projects /></Boundary>;
}

function Projects() {
  const list = useProjects();
  const current = useProject((s) => s.context.id);
  const query = useProjectsQuery();
  const [renaming, setRenaming] = useState<string | null>(null);
  const [menu, setMenu] = useState<{ id: string; x: number; y: number } | null>(null);
  const grid = useRef<HTMLDivElement>(null);
  const q = query.trim().toLowerCase();
  const shown = (list ?? []).filter((p) => !q || p.name.toLowerCase().includes(q));
  useEffect(() => { if (!grid.current?.contains(document.activeElement)) grid.current?.querySelector<HTMLElement>('.pj-card')?.focus(); }, [list === null]);

  const onKey = (e: KeyboardEvent) => {
    const cards = [...(grid.current?.querySelectorAll<HTMLElement>('.pj-card') ?? [])];
    const at = cards.indexOf(document.activeElement as HTMLElement);
    if (at < 0 || renaming) return;
    const p = shown[at];
    const cols = cards.filter((c) => c.offsetTop === cards[0].offsetTop).length;
    const move = ({ ArrowLeft: -1, ArrowRight: 1, ArrowUp: -cols, ArrowDown: cols } as Record<string, number>)[e.key];
    if (move) cards[Math.max(0, Math.min(cards.length - 1, at + move))]?.focus();
    else if (e.key === 'Home' || e.key === 'End') cards[e.key === 'Home' ? 0 : cards.length - 1]?.focus();
    else if (e.key === 'Enter') void openProject(p.id);
    else if (e.key === 'F2') setRenaming(p.id);
    else if (e.key === 'Delete' || e.key === 'Backspace') {
      cards[at + 1 < cards.length ? at + 1 : at - 1]?.focus();
      void remove(p);
    } else if (e.key === 'ContextMenu' || (e.shiftKey && e.key === 'F10')) {
      const r = cards[at].getBoundingClientRect();
      setMenu({ id: p.id, x: r.left + 8, y: r.bottom - 8 });
    } else return;
    e.preventDefault();
    e.stopPropagation();
  };
  const onDrop = (e: DragEvent) => {
    if (!e.dataTransfer.files.length) return;
    e.preventDefault();
    void newProjectFrom(e.dataTransfer.files);
  };
  const menuProject = menu && list?.find((p) => p.id === menu.id);

  return (
    <div className="projects" onDragOver={(e) => e.preventDefault()} onDrop={onDrop}>
      <div className="pj-bar">
        <h1 className="pj-heading">Projects</h1>
        <span className="pj-count">{list?.length ? list.length : ''}</span>
        <input className="pj-search" type="search" placeholder="Search" aria-label="Search projects" value={query} onChange={(e) => setProjectsQuery(e.target.value)} />
        <button type="button" className="btn-ghost" onClick={fileVerbs.new}>New</button>
        <button type="button" className="btn-ghost" onClick={fileVerbs.import}>Import</button>
      </div>
      {list && !list.length ? (
        <div className="pj-empty">
          <span>No projects</span>
          <button type="button" className="pj-drop" onClick={fileVerbs.import}>Drop a model</button>
        </div>
      ) : (
        <div ref={grid} className="pj-grid" role="list" aria-label="Projects" onKeyDown={onKey}>
          {shown.map((p) => (
            <Card key={p.id} p={p} open={p.id === current} renaming={renaming === p.id} setRenaming={(on) => {
              setRenaming(on ? p.id : null);
              if (!on) requestAnimationFrame(() => grid.current?.querySelector<HTMLElement>(`[data-id="${p.id}"]`)?.focus());
            }} onMenu={(at) => setMenu({ id: p.id, ...at })} />
          ))}
        </div>
      )}
      {menu && menuProject && <CardMenu at={menu} project={menuProject} close={() => setMenu(null)} rename={() => setRenaming(menu.id)} />}
    </div>
  );
}
