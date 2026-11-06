

const MAC = /Mac|iPhone|iPad/.test(navigator.platform);
const MAC_KEYS: Record<string, string> = { Mod: '⌘', Shift: '⇧', Alt: '⌥', Ctrl: '⌃' };
export const keyName = (k: string) => (MAC ? MAC_KEYS[k] ?? k : k === 'Mod' ? 'Ctrl' : k);

export const keysText = (keys: string) => keys.split(' / ')[0].split(/(?<=.)\+(?=.)/).map(keyName).join(MAC ? '' : '+');

export function Keys({ keys, className = '' }: { keys: string; className?: string }) {
  return (
    <span className={`keys ${className}`}>
      {keys.split(' / ').map((alt, i) => (
        <span key={i} className="keys-alt">
          {i > 0 && <span className="keys-or">/</span>}
          {(alt === '+' ? ['+'] : alt.split(/(?<=.)\+(?=.)/)).map((k, j) => <kbd key={j}>{keyName(k)}</kbd>)}
        </span>
      ))}
    </span>
  );
}
