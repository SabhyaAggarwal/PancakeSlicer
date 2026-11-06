
const assets = new Map<string, string>();
export function bedAsset(name: string) { return assets.get(name) ?? name; }
export function registerBedAsset(file: File) {
  const old = assets.get(file.name);
  if (old) URL.revokeObjectURL(old);
  assets.set(file.name, URL.createObjectURL(file));
  return file.name;
}
window.addEventListener('pagehide', () => { assets.forEach((url) => URL.revokeObjectURL(url)); assets.clear(); });
