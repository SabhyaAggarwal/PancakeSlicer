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
