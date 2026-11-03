import { switchPreset } from '../actions';
import { undoable } from '../state/history';
import { effectiveConfig, getState, resolvePreset, setOption, type State } from '../state/store';
import { takeValue } from '../profiles/stack';
import { surfaceOf, text, writeSurface } from './surface';
import { defaultProcesses, type DefaultProcess } from './default-process-model';
export { defaultProcesses, type DefaultProcess } from './default-process-model';
const number = (v: Parameters<typeof text>[0]) => parseFloat(text(v));

export function currentDefault(s: State): DefaultProcess | undefined {
  if (resolvePreset(s, 'process', s.process).user || Object.keys(s.projectPresets?.process ?? {}).length) return;
  const config = effectiveConfig(s, 'process');
  return defaultProcesses(s).find((p) => p.preset === s.process && surfaceOf(config) === p.surface
    && Math.abs(number(config.layer_height) - p.layer) < 1e-6);
}

export function chooseDefaultProcess(id: string) {
  const s = getState(), choice = defaultProcesses(s).find((p) => p.id === id);
  if (!choice?.preset || choice.unavailable) return;
  const preset = choice.preset;
  undoable(`Process: ${choice.label}`, () => {
    const carried = { ...s.projectPresets?.process, ...s.dirty.process };
    if (preset !== s.process) {
      switchPreset('process', 0, preset, 'transfer');
      for (const [key, value] of Object.entries(carried)) setOption('process', key, value);
    }
    takeValue('process', 'layer_height', String(choice.layer));
    writeSurface(choice.surface);
  });
}
