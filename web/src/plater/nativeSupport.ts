import { effectiveConfig, type PlateObject, type State } from '../state/store';

const on = (v: unknown) => v === true || v === 1 || v === '1' || v === 'true';
const painted = (o: { paint?: PlateObject['paint'] }) => !!o.paint?.support && o.paint.support.size > 0;

export function nativeSupportConflicts(s: State): string | null {
  const global = effectiveConfig(s, 'process');
  const lines: string[] = [];
  for (const o of s.objects) {
    const style = String(o.settings.support_style ?? global.support_style ?? '');
    const enabled = on(o.settings.enable_support ?? global.enable_support);
    if (style !== 'native' || !enabled) continue;
    if (on(o.settings.support_on_build_plate_only ?? global.support_on_build_plate_only)) lines.push(`'On build plate only' (object: ${o.name})`);
    if ((o.volumes ?? []).some((v) => v.type === 'support_enforcer' || v.type === 'support_blocker')) lines.push(`support enforcers or blockers (object: ${o.name})`);
    if (painted(o) || (o.volumes ?? []).some(painted)) lines.push(`painted support (object: ${o.name})`);
  }
  return lines.length ? `Native support doesn't support ${lines.join('; ')}. Turn it off or choose Normal/Tree.` : null;
}
