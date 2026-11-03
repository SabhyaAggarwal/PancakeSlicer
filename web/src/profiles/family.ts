import type { SystemPreset, UserPreset } from '../api/types';
import { getState } from '../state/store';
import { makeProfile } from './catalog';
import { deriveLayers, familyName, referenceMember, systemFamilies, type SystemFamily } from './dynamic';

const familyKey = (vendor: string, name: string) => `${vendor}\n${familyName(name).toLowerCase().replace(/\s+/g, '')}`;
export function familyOf(presetName: string): SystemFamily | undefined {
  const s = getState(), preset = s.processes.find((p) => p.name === presetName);
  if (!preset) return undefined;
  const key = familyKey(preset.vendor, preset.name), members = s.processes.filter((p) => p.vendor === preset.vendor && familyKey(p.vendor, p.name) === key);
  const family = members.length > 1 ? systemFamilies(members, s.machines)[0] : undefined;
  return family && family.members.length > 1 ? family : undefined;
}
export async function addFamily(family: SystemFamily): Promise<string> {
  const members = family.members.map((m) => ({ ctx: m.ctx, config: m.preset.config, preset: m.preset as SystemPreset }));
  const reference = referenceMember(members), dynamic = deriveLayers('process', reference, members);
  const { profileCreation } = await import('./transactions');
  return profileCreation((s) => ({ ...makeProfile(s, reference.preset, family.name), dynamic, applicability: { schema: 2, printers: { models: family.models } } }) as UserPreset).commit();
}
