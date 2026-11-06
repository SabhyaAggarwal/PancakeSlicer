


const MAX_PIXELS = 3840 * 2160;

export function pixelRatio(width: number, height: number): number {
  const dpr = window.devicePixelRatio || 1;
  return Math.max(1, Math.min(dpr, Math.sqrt(MAX_PIXELS / Math.max(1, width * height))));
}


export function watchPixelRatio(changed: () => void): () => void {
  let query: MediaQueryList | null = null;
  const arm = () => {
    query?.removeEventListener('change', fire);
    query = window.matchMedia(`(resolution: ${window.devicePixelRatio}dppx)`);
    query.addEventListener('change', fire);
  };
  const fire = () => {
    arm();
    changed();
  };
  arm();
  return () => query?.removeEventListener('change', fire);
}
