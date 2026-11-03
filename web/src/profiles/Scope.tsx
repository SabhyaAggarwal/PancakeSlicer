import type { PresetType } from '../api/types';
import { splitKey } from '../options';
import { getState, removeObjectOptions, removeVolumeOptions, useStore, type State } from '../state/store';
import { undoable } from '../state/history';
import { removeRangeOptions } from '../plater/ranges';
import { selectChanges, type ChangeTarget, type SettingChange } from '../state/changes';
import { revert } from './stack';

const cache = new WeakMap<State, Map<string, SettingChange[]>>();
const changesOf = (s: State, type: PresetType, slot: number) => {
  let m = cache.get(s);
  if (!m) cache.set(s, (m = new Map()));
  const id = `${type}:${slot}`;
  if (!m.has(id)) m.set(id, selectChanges(s, type, slot));
  return m.get(id)!;
};

interface Drop { below: 'preset' | 'project' | 'object'; run(): void }

function dropOf(s: State, type: PresetType, key: string, slot: number, t: ChangeTarget): Drop | undefined {
  const object = s.objects.find((o) => o.id === t.object);
  const volume = object?.volumes?.find((v) => v.id === t.volume);
  const range = object?.ranges?.find((r) => r.id === t.range);
  if (object && range) return key in range.settings ? { below: 'object', run: () => removeRangeOptions(object.id, range.id, [key]) } : undefined;
  if (object && volume) return key in volume.settings ? { below: 'object', run: () => removeVolumeOptions(object.id, volume.id, [key]) } : undefined;
  if (object) return key in object.settings ? { below: 'project', run: () => removeObjectOptions(object.id, [key]) } : undefined;
  const row = changesOf(s, type, slot).find((c) => c.key === key && c.scope === 'project');
  if (!row) return undefined;
  return { below: 'preset', run: () => revert(row) };
}

interface Props { type: PresetType; slot?: number; optKey: string; target?: ChangeTarget }

export function ScopeMark({ type, slot = 0, optKey, target = {} }: Props) {
  const key = splitKey(optKey)[0];
  const below = useStore((s) => dropOf(s, type, key, slot, target)?.below);
  if (!below) return <span className="scope" />;
  const label = `Use ${below} value`;
  return (
    <span className="scope">
      <button type="button" className="scope-use" title={label} aria-label={`${label}: ${key}`}
        onClick={() => undoable(label, () => dropOf(getState(), type, key, slot, target)?.run())}
        onKeyDown={(e) => {
          if (e.key !== 'Backspace' && e.key !== 'Delete') return;
          e.preventDefault();
          undoable(label, () => dropOf(getState(), type, key, slot, target)?.run());
        }} />
    </span>
  );
}
