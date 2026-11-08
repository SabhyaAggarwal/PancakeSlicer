export const SUPPORTED_BROWSERS = 'Chrome or Edge 109+, Firefox 115+, or Safari 16.4+ (macOS or iPadOS)';

export function unsupportedBrowserMessage(detail: string): string {
  return `Use ${SUPPORTED_BROWSERS}. ${detail}`;
}

export type GraphicsCapability = { webgl2: true } | { webgl2: false; reason: string };
let graphics: GraphicsCapability | undefined;
let probed: HTMLCanvasElement | undefined;
export function graphicsCapability(): GraphicsCapability {
  if (graphics) return graphics;
  const canvas = document.createElement('canvas');
  const gl = canvas.getContext('webgl2', { alpha: true, depth: true, stencil: false, antialias: true, premultipliedAlpha: true,
    preserveDrawingBuffer: true, powerPreference: 'default', failIfMajorPerformanceCaveat: false });
  if (gl) probed = canvas;
  return graphics = gl ? { webgl2: true }
    : { webgl2: false, reason: 'The 3D view needs WebGL2. Enable hardware acceleration or update your graphics driver.' };
}
export function takeProbedCanvas(): HTMLCanvasElement | undefined {
  const canvas = probed;
  probed = undefined;
  return canvas;
}

export function showBrowserNotice(root: HTMLElement, detail: string, title = 'Unsupported browser'): void {
  const doc = root.ownerDocument;
  const heading = doc.createElement('h1');
  heading.textContent = title;
  const message = doc.createElement('p');
  message.textContent = detail;
  const reload = doc.createElement('button');
  reload.textContent = 'Reload';
  reload.onclick = () => location.reload();
  root.className = 'browser-notice';
  root.setAttribute('role', 'alert');
  root.replaceChildren(heading, message, reload);
}

export async function checkBrowserSupport(): Promise<void> {
  if (typeof Worker === 'undefined' || typeof fetch === 'undefined' ||
      typeof ReadableStream === 'undefined' || typeof TransformStream === 'undefined' ||
      typeof DecompressionStream === 'undefined' || typeof ResizeObserver === 'undefined' ||
      typeof Blob === 'undefined' || typeof Blob.prototype.stream !== 'function')
    throw new Error('Required worker, streaming or resize features are unavailable.');

  await new Promise<void>((resolve, reject) => {
    const worker = new Worker(new URL('./browser-support.worker.ts', import.meta.url), { type: 'module' });
    const finish = (error?: string) => {
      clearTimeout(timeout);
      worker.terminate();
      if (error) reject(new Error(error));
      else resolve();
    };
    const timeout = setTimeout(() => finish('The module worker did not start. Check browser settings and reload.'), 15000);
    worker.onmessage = (event: MessageEvent<{ module: string; error: string }>) =>
      finish(event.data.module ? event.data.error || undefined : 'Module workers are unavailable.');
    worker.onerror = (event) => {
      event.preventDefault();
      finish('Module workers could not load. Check browser settings or ask your administrator to allow worker scripts.');
    };
  });
}
