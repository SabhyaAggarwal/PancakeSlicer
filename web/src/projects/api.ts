import * as project from '../flow/project';
import { setState, toast } from '../state/store';

import { useMemo } from 'react';
import type { ProjectMeta } from '../flow/project';

export { flushProject, storing, type ProjectMeta } from '../flow/project';

export const clean = (p: Partial<ProjectMeta> | null | undefined): ProjectMeta | null => (!p || typeof p.id !== 'string' ? null : {
  id: p.id,
  name: typeof p.name === 'string' && p.name.trim() ? p.name : 'Untitled',
  savedAt: Number.isFinite(p.savedAt) ? Number(p.savedAt) : 0,
  ...(Number.isSafeInteger(p.revision) ? { revision: p.revision } : {}),
  objects: Number.isFinite(p.objects) ? Number(p.objects) : 0,
  thumbnail: typeof p.thumbnail === 'string' && p.thumbnail.startsWith('data:image/') ? p.thumbnail : null,
});

export function useProjects(): ProjectMeta[] | null {
  const list = project.useProjects();
  return useMemo(() => (Array.isArray(list) ? list.map(clean).filter((p): p is ProjectMeta => !!p) : null), [list]);
}

export async function listTrash(): Promise<ProjectMeta[]> {
  const { projectJson } = await import('../persist/project-wire');
  const rows = await projectJson<Partial<ProjectMeta>[]>('/api/projects?archived=1');
  if (!Array.isArray(rows)) throw new Error('Trash could not be read.');
  return rows.map(clean).filter((p): p is ProjectMeta => !!p);
}

async function guard<T>(failed: string, f: () => Promise<T>, fallback: T): Promise<T> {
  try {
    return await f();
  } catch (e) {
    console.warn(failed, e);
    toast(failed);
    return fallback;
  }
}

export const newProject = () => guard('New project failed', async () => {const created=await project.newProject();if(created)setState({tab:'prepare'});return created;},false);
export const openProject = (id: string) => guard('Open failed', async () => { await project.openProject(id); setState({ tab: 'prepare' }); }, undefined);
export const renameProject = (id: string, name: string) => guard('Rename failed', () => project.renameProject(id, name), undefined);
export const duplicateProject = (id: string) => guard('Duplicate failed', () => project.duplicateProject(id), null);
export const deleteProject = (id: string) => guard('Delete failed', () => project.deleteProject(id), null);
