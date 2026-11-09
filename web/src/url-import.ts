import { useSyncExternalStore } from 'react';
import { jsonWrite, serviceRequest } from './persist/service';
import { mutation } from './api/access';
import { assignBlobKey } from './persist/session';
import { documentGeneration, onLeaveDocument } from './state/history';
import { getState } from './state/store';
import { SEP } from './separator';

export interface UrlFile { name: string; url?: string; key?: string; sha256?: string; size?: number }
type Phase = 'editing' | 'resolving' | 'picking' | 'downloading' | 'importing';
interface UrlImportState {
  open: boolean; inline: boolean; url: string; phase: Phase; files: UrlFile[]; checked: boolean[]; error: string; progress: string;
}
const initial: UrlImportState = { open: false, inline: false, url: '', phase: 'editing', files: [], checked: [], error: '', progress: '' };
let state = initial;
const listeners = new Set<() => void>();
const snapshot = () => state;
const subscribe = (listener: () => void) => { listeners.add(listener); return () => { listeners.delete(listener); }; };
function update(patch: Partial<UrlImportState>) { state = { ...state, ...patch }; listeners.forEach((fn) => fn()); }
export const useUrlImport = () => useSyncExternalStore(subscribe, snapshot);
let controller: AbortController | undefined;
let operation = 0;
export function closeUrlImport() { operation++; controller?.abort(); controller = undefined; update(initial); }
onLeaveDocument(closeUrlImport);
export function openUrlImport(url = '', inline = false) {
  closeUrlImport();
  update({ ...initial, open: true, inline: inline && !getState().objects.length, url });
  if (url) void resolveUrlImport();
}
export function editImportUrl(url: string) { if (state.phase === 'editing' || state.phase === 'picking') update({ url, files: [], checked: [], phase: 'editing', error: '' }); }
export function checkUrlFile(index: number, checked: boolean) { update({ checked: state.checked.map((value, i) => i === index ? checked : value) }); }
export function checkAllUrlFiles(checked: boolean) { update({ checked: state.files.map(() => checked) }); }
const busy = () => state.phase === 'resolving' || state.phase === 'downloading' || state.phase === 'importing';
export const urlImportBusy = busy;

async function requestFiles(url: string, signal: AbortSignal, file?: UrlFile) {
  const response = await mutation('/api/imports/url', {
    ...jsonWrite('POST', { url, ...(file ? { fileUrl: file.url, name: file.name } : {}) }), signal,
  }, false, signal, undefined, true);
  const result = await response.json() as { sourceUrl: string; files: UrlFile[] };
  if (!result?.files.length) throw new Error('Hmm, we couldn\'t find any downloadable 3D model files at that link.');
  return result;
}
async function assetFile(asset: UrlFile, signal: AbortSignal, current: () => boolean): Promise<File> {
  if (!asset.key || !asset.sha256) throw new Error('Oops! The server couldn\'t save the downloaded model file.');
  const response = await serviceRequest(`/api/assets/${encodeURIComponent(asset.key)}`, { signal });
  if (!response) throw new Error('Oops! The downloaded model file couldn\'t be found.');
  let blob: Blob;
  if (response.body && asset.size) {
    const reader = response.body.getReader(), chunks: Uint8Array[] = [];
    let received = 0;
    for (;;) {
      const { done, value } = await reader.read();
      if (done) break;
      chunks.push(value); received += value.byteLength;
      if (current()) update({ progress: `Loading ${asset.name}${SEP}${Math.min(100, Math.round(received / asset.size * 100))}%` });
    }
    blob = new Blob(chunks as BlobPart[]);
  } else blob = await response.blob();
  const file = new File([blob], asset.name);
  assignBlobKey(file, asset.key);
  return file;
}

async function amfGeometry(file: File): Promise<File> {
  if (!/\.amf$/i.test(file.name)) return file;
  const [{ AMFLoader }, { STLExporter }] = await Promise.all([
    import('three/examples/jsm/loaders/AMFLoader.js'), import('three/examples/jsm/exporters/STLExporter.js'),
  ]);
  const group = new AMFLoader().parse(await file.arrayBuffer());
  try {
    group.updateMatrixWorld(true);
    const stl = new STLExporter().parse(group, { binary: true });
    return new File([stl as BlobPart], file.name.replace(/\.amf$/i, '.stl'));
  } finally {
    group.traverse((node) => {
      const mesh = node as import('three').Mesh;
      mesh.geometry?.dispose();
      const materials = mesh.material ? Array.isArray(mesh.material) ? mesh.material : [mesh.material] : [];
      materials.forEach((m) => m.dispose());
    });
  }
}

async function runImport(resolve: boolean) {
  if (busy()) return;
  const url = state.url.trim();
  if (!/^https?:\/\/\S+$/i.test(url)) { update({ error: 'Please paste a valid http:// or https:// link.' }); return; }
  const ticket = ++operation, generation = documentGeneration();
  controller?.abort(); const request = new AbortController(); controller = request;
  const signal = request.signal;
  const current = () => ticket === operation && generation === documentGeneration() && !signal.aborted;
  const timer = setTimeout(() => request.abort(), 180_000);
  update({ phase: resolve ? 'resolving' : 'downloading', error: '', progress: resolve ? 'Finding model files…' : 'Downloading selected files…' });
  try {
    let files: UrlFile[], sourceUrl = url;
    if (resolve) {
      const result = await requestFiles(url, signal); if (!current()) return;
      files = result.files; sourceUrl = result.sourceUrl;
      if (files.length > 1) { update({ phase: 'picking', files, checked: files.map(() => true), progress: '' }); return; }
    } else files = state.files.filter((_, i) => state.checked[i]);
    const resolved: UrlFile[] = [];
    let expanded = false;
    for (const file of files) {
      if (!current()) return;
      update({ phase: 'downloading', progress: `Downloading ${file.name}…` });
      const assets = file.key ? [file] : (await requestFiles(sourceUrl, signal, file)).files;
      if (!current()) return;
      if (assets.length > 1) expanded = true;
      resolved.push(...assets);
    }
    if (expanded) { update({ phase: 'picking', files: resolved, checked: resolved.map(() => true), progress: '' }); return; }
    const models: File[] = [];
    for (const asset of resolved) {
      const model = await amfGeometry(await assetFile(asset, signal, current));
      if (!current()) return;
      models.push(model);
    }
    if (!models.length) throw new Error('Pick at least one model file to import.');
    update({ phase: 'importing', progress: 'Adding model geometry…' });
    const { importFiles } = await import('./actions');
    if (!current()) return;
    const count = await importFiles(models, { sourceUrl });
    if (!current()) return;
    if (!count) throw new Error('No models were added—either the import was canceled or we couldn\'t read the file.');
    closeUrlImport();
  } catch (error) {
    if (ticket !== operation || generation !== documentGeneration()) return;
    update({ phase: state.files.length ? 'picking' : 'editing', progress: '', error: signal.aborted ? 'Oops! That download took too long and timed out. Give it another try.' : error instanceof Error ? error.message : String(error) });
  } finally { clearTimeout(timer); }
}
export const resolveUrlImport = () => runImport(true);
export const importSelectedUrls = () => runImport(false);
