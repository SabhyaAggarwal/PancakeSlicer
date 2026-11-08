import type { PlateObject } from '../state/store';

export const overrideCount = (o: PlateObject) => Object.keys(o.settings).length
  + (o.volumes ?? []).reduce((n, v) => n + Object.keys(v.settings ?? {}).length, 0) + (o.ranges ?? []).reduce((n, r) => n + Object.keys(r.settings).length, 0);
