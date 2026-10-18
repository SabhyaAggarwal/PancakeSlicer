import { useMemo, useState } from 'react';
import type { PresetType } from '../api/types';
import { isHidden, modeVisible, OPTIONS, splitKey, useTab, type TabDesc } from '../options';
import { effectiveConfig, getState, resolvePreset, saveUserPreset, setState, toast, useStore } from '../state/store';
import { Field } from './Field';
import { Icon, IconButton } from './Icon';
import { PresetCombo } from './PresetCombo';

const TAB_OF: Record<PresetType, TabDesc['tab']> = { process: 'process', filament: 'filament', machine: 'printer' };

export function SettingsTab({ type, slot = 0, compact }: { type: PresetType; slot?: number; compact?: boolean }) {
  const { desc, error } = useTab(TAB_OF[type]);
  const [pageIdx, setPageIdx] = useState(0);
  const uiMode = useStore((s) => s.uiMode);
  const dirty = useStore((s) => s.dirty[type]);
  const presetName = useStore((s) => (type === 'machine' ? s.printer : type === 'process' ? s.process : s.filamentSlots[slot]));
  const userPresets = useStore((s) => s.userPresets);
  const lists = useStore((s) => s.machines.length + s.processes.length + s.filaments.length);
  const config = useMemo(() => effectiveConfig(getState(), type, slot), [type, slot, presetName, dirty, userPresets, lists]);
  const system = useMemo(() => resolvePreset(getState(), type, presetName ?? '').config, [type, presetName, userPresets, lists]);

  const pages = useMemo(
    () =>
      (desc?.pages ?? [])
        .map((p) => ({
          ...p,
          groups: p.groups
            .map((g) => ({
              ...g,
              options: g.options.filter((k) => {
                const def = OPTIONS[splitKey(k)[0]];
                return def && !isHidden(def.key) && modeVisible(def.mode, uiMode);
              }),
            }))
            .filter((g) => g.options.length),
        }))
        .filter((p) => p.groups.length),
    [desc, uiMode],
  );
  const page = pages[Math.min(pageIdx, pages.length - 1)];
  const isUser = userPresets.some((u) => u.type === type && u.name === presetName);
  const modifiedCount = Object.keys(dirty).length;

  const onSave = async () => {
    const suggested = isUser ? presetName : `${presetName} - Copy`;
    const name = window.prompt('Save preset as', suggested)?.trim();
    if (!name) return;
    if (getState()[type === 'machine' ? 'machines' : type === 'process' ? 'processes' : 'filaments'].some((p) => p.name === name)) {
      toast('A system preset has this name; choose another.');
      return;
    }
    await saveUserPreset(type, name);
    toast(`Preset "${name}" saved`);
  };
  const onDelete = async () => {
    const s = getState();
    if (!isUser || !s.api || !window.confirm(`Delete the preset "${presetName}"?`)) return;
    const { base } = resolvePreset(s, type, presetName!);
    await s.api.deleteUserPreset(type, presetName!);
    setState((st) => {
      const patch: Partial<typeof st> = { userPresets: st.userPresets.filter((u) => !(u.type === type && u.name === presetName)) };
      const fallback = base?.name ?? '';
      if (type === 'machine') patch.printer = fallback;
      else if (type === 'process') patch.process = fallback;
      else patch.filamentSlots = st.filamentSlots.map((f, i) => (i === slot ? fallback : f));
      return patch;
    });
  };

  return (
    <div className={`settings-tab ${compact ? 'compact' : ''}`}>
      <div className="tab-preset-row">
        <PresetCombo type={type} slot={slot} />
        <IconButton icon="save" title={modifiedCount ? `Save current ${type === 'machine' ? 'printer' : type} preset (${modifiedCount} modified)` : 'Save as user preset'} onClick={onSave} />
        <IconButton icon="delete" title="Delete this user preset" onClick={onDelete} disabled={!isUser} />
      </div>
      <div className="page-bar" role="tablist">
        {pages.map((p, i) => (
          <button key={p.title} role="tab" className={`page-tab ${p === page ? 'active' : ''}`} onClick={() => setPageIdx(i)}>
            {p.title}
          </button>
        ))}
      </div>
      <div className="page-body">
        {!desc && <div role="status">{error ? `Settings failed to load: ${error}` : 'Loading settings…'}</div>}
        {page?.groups.map((g) => (
          <section key={g.title} className="optgroup">
            {g.title && (
              <header className="optgroup-title">
                {g.icon && <Icon name={g.icon} size={16} />}
                <span>{g.title}</span>
              </header>
            )}
            {g.options.map((k) => {
              const key = splitKey(k)[0];
              return <Field key={k} optKey={k} type={type} value={config[key]} systemValue={system[key]} dirty={key in dirty} />;
            })}
          </section>
        ))}
      </div>
    </div>
  );
}
