import type { Config, ConfigValue } from '../api/types';
import { OPTIONS } from '../options';
import { sameValue } from '../state/value-equality';
import { changedKeys, PRESET_META } from './model';

interface Changes { revision: unknown; value: Config }
const drafts = new WeakMap<Config, WeakMap<Config, Changes>>();

function remember(values: Config, baseline: Config, revision: unknown, value: Config): Config {
  let baselines = drafts.get(values);
  if (!baselines) drafts.set(values, (baselines = new WeakMap()));
  baselines.set(baseline, { revision, value });
  return value;
}

export function profileDraftChanges(values: Config, baseline: Config, revision: unknown): Config {
  const cached = drafts.get(values)?.get(baseline);
  return cached && Object.is(cached.revision, revision) ? cached.value
    : remember(values, baseline, revision, changedKeys(values, baseline));
}

export function editProfileDraftValue(values: Config, baseline: Config, key: string, value: ConfigValue | undefined, revision: unknown): Config {
  const previous = profileDraftChanges(values, baseline, revision);
  const next = { ...values }, changes = { ...previous };
  if (value === undefined) delete next[key]; else next[key] = value;
  if (value === undefined || PRESET_META.test(key) || sameValue(key, value, baseline[key] ?? OPTIONS[key]?.default as ConfigValue | undefined)) delete changes[key];
  else changes[key] = value;
  remember(next, baseline, revision, changes);
  return next;
}
