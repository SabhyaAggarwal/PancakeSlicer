

import { contextBridge, ipcRenderer } from 'electron';
import type { LegacyRead, LegacyWorkspaceInventory } from './legacy-workspace';
import type { LegacyRendererInventory, LegacyRendererPartRead } from './legacy-renderer';

const inventory = (): Promise<LegacyWorkspaceInventory> => ipcRenderer.invoke('orca-desktop:legacy-projects:inventory');

const call = (op: string) => async (...args: unknown[]) => {
  const result = await ipcRenderer.invoke(`orca-desktop:projects:${op}`, ...args);
  if (result?.__orcaProjectError) {
    const error = new Error(result.__orcaProjectError.message);
    error.name = result.__orcaProjectError.name;
    throw error;
  }
  return result;
};
const listeners = new Set<(command: string) => void>();
const beforeClose = new Set<() => Promise<void>>();
let markCloseReady: () => void;
const closeReady = new Promise<void>((resolve) => { markCloseReady = resolve; });

const editable = (el: Element | null) =>
  !!el && (el instanceof HTMLTextAreaElement || (el instanceof HTMLInputElement && !['button', 'checkbox', 'radio', 'range', 'file'].includes(el.type))
    || (el as HTMLElement).isContentEditable);

ipcRenderer.on('orca-desktop:menu', (_e, command: string) => {

  if ((command === 'undo' || command === 'redo') && editable(document.activeElement)) {
    document.execCommand(command);
    return;
  }
  window.dispatchEvent(new CustomEvent('orca-desktop-menu', { detail: command }));
  for (const cb of listeners) cb(command);
});

contextBridge.exposeInMainWorld('orcaDesktop', {
  version: 1,
  platform: process.platform,


  exportLegacyWorkspace: inventory,
  legacyProjects: {
    version: 1,
    inventory,
    readFile: (request: LegacyRead): Promise<Uint8Array> => ipcRenderer.invoke('orca-desktop:legacy-projects:read-file', request),
  },

  bambuConnect: (jobId: string, filename: string): Promise<void> => ipcRenderer.invoke('orca-desktop:bambu-connect', jobId, filename),
  legacyRendererStorage: {
    version: 1,
    inventory: (): Promise<LegacyRendererInventory> => ipcRenderer.invoke('orca-desktop:legacy-renderer:inventory'),
    readPart: (request: LegacyRendererPartRead): Promise<Uint8Array> => ipcRenderer.invoke('orca-desktop:legacy-renderer:read-part', request),
  },
  onBeforeClose(handler: () => Promise<void>) {
    beforeClose.add(handler);
    markCloseReady();
    return () => { beforeClose.delete(handler); };
  },
  async flushBeforeClose() {

    await closeReady;
    await Promise.all([...beforeClose].map((handler) => handler()));
  },
  projects: {
    list: call('list'),
    load: call('load'),
    save: call('save'),
    rename: call('rename'),
    duplicate: call('duplicate'),
    remove: call('remove'),
    restore: call('restore'),
    putBlob: call('putBlob'),
    getBlob: call('getBlob'),
    hasBlob: call('hasBlob'),
    emptyTrash: call('emptyTrash'),
  },
  onMenu(cb: (command: string) => void) {
    listeners.add(cb);
    return () => { listeners.delete(cb); };
  },
});
