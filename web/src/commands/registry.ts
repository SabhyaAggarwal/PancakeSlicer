






export interface Command {

  id: string;
  title: string;

  group: string;

  keys?: string;

  keywords?: string;

  detail?: string;

  enabled?: () => boolean | string;

  run?: () => void;

  boost?: number;
}


export type Provider = (query: string) => Command[] | Promise<Command[]>;

const commands = new Map<string, Command>();
const providers = new Map<string, Provider>();
const listeners = new Set<() => void>();
let version = 0;
const notify = () => { version++; listeners.forEach((l) => l()); };


export function registerCommand(c: Command | Command[]): () => void {
  const list = Array.isArray(c) ? c : [c];
  list.forEach((x) => commands.set(x.id, x));
  notify();
  return () => { list.forEach((x) => commands.get(x.id) === x && commands.delete(x.id)); notify(); };
}


export function registerProvider(id: string, p: Provider): () => void {
  providers.set(id, p);
  notify();
  return () => { if (providers.get(id) === p) providers.delete(id); notify(); };
}

export const allCommands = () => [...commands.values()];
export const allProviders = () => [...providers.entries()];
export const subscribeCommands = (l: () => void) => { listeners.add(l); return () => { listeners.delete(l); }; };
export const commandsVersion = () => version;


export function runCommand(id: string): boolean {
  const c = commands.get(id);
  if (!c?.run || (c.enabled && c.enabled() !== true)) return false;
  c.run();
  return true;
}


let palette: string | null = null;
const paletteListeners = new Set<() => void>();
export const paletteQuery = () => palette;
export const subscribePalette = (l: () => void) => { paletteListeners.add(l); return () => { paletteListeners.delete(l); }; };
export function openPalette(query = '') { palette = query; paletteListeners.forEach((l) => l()); }
export function closePalette() { if (palette === null) return; palette = null; paletteListeners.forEach((l) => l()); }

export const loadPalette = () => import('./Palette');
