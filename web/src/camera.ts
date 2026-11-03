import { observePreference, preferenceJson, savePreferences } from './saved-preferences';
export const CAMERA_VIEWS = ['Default View', 'Top', 'Bottom', 'Front', 'Rear', 'Left', 'Right'] as const;
export type CameraView = 'plate' | 'top' | 'bottom' | 'front' | 'rear' | 'left' | 'right';
export const VIEW_DIRECTIONS: CameraView[] = ['plate', 'top', 'bottom', 'front', 'rear', 'left', 'right'];
export type CameraCommand = CameraView | 'perspective' | 'orthogonal' | 'auto';
export let perspective = true;
export let autoPerspective = true;
const listeners = new Set<() => void>();
export const subscribeCamera = (fn: () => void) => { listeners.add(fn); return () => { listeners.delete(fn); }; };
export const cameraSnapshot = () => `${perspective}:${autoPerspective}`;
export function cameraProjection(value: boolean) {
  perspective = value;
  listeners.forEach((fn) => fn());
}
export function cameraCommand(command: CameraCommand) {
  if (command === 'auto') { autoPerspective = !autoPerspective; listeners.forEach((fn) => fn()); }
  if (command === 'perspective' || command === 'orthogonal') cameraProjection(command === 'perspective');
  window.dispatchEvent(new CustomEvent('camera-command', { detail: command }));
}

export type CameraPrefs = { style: 'default' | 'touchpad'; orbitMult: number; zoomToMouse: boolean; reverseZoom: boolean };
const PREFS_KEY = 'orca.web.camera';
export let cameraPrefs: CameraPrefs = { style: 'default', orbitMult: 1, zoomToMouse: true, reverseZoom: false };

export function setCameraPrefs(patch: Partial<CameraPrefs>) {
  cameraPrefs = { ...cameraPrefs, ...patch };
  savePreferences({[PREFS_KEY]: {...cameraPrefs}});
  listeners.forEach((fn) => fn());
}
export const cameraPrefsSnapshot = () => cameraPrefs;

observePreference(PREFS_KEY, () => {
  const saved = preferenceJson<Partial<CameraPrefs>>(PREFS_KEY, {});
  cameraPrefs = { style: saved.style === 'touchpad' ? 'touchpad' : 'default', orbitMult: typeof saved.orbitMult === 'number' && saved.orbitMult >= .05 && saved.orbitMult <= 2 ? saved.orbitMult : 1, zoomToMouse: saved.zoomToMouse !== false, reverseZoom: saved.reverseZoom === true };
  listeners.forEach(listener => listener());
});
