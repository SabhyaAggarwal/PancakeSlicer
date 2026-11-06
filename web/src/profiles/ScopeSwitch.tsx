

import { setState, useStore } from '../state/store';

export function ScopeSwitch() {
  const scope = useStore((s) => s.processScope);
  const any = useStore((s) => s.objects.length > 0);
  const pick = (next: 'global' | 'objects') => setState((s) => ({
    processScope: next,
    ...(next === 'objects' && !s.selected && s.objects[0] ? { selected: { object: s.objects[0].id, instance: s.objects[0].instances[0].id, allInstances: true } } : {}),
  }));
  return (
    <div className="pf-scope-switch" role="group" aria-label="Settings scope">
      <button type="button" aria-pressed={scope === 'global'} onClick={() => pick('global')}>Global</button>
      <button type="button" aria-pressed={scope === 'objects'} disabled={!any} onClick={() => pick('objects')}>Objects</button>
    </div>
  );
}
