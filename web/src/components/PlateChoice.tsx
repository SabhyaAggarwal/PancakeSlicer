import { setState, useStore } from '../state/store';

export default function PlateChoice() {
  const plates = useStore((s) => s.projectPlates);
  const current = useStore((s) => s.plateIndex);
  const busy = useStore((s) => s.job.state === 'running' || s.job.state === 'submitting');
  return <div className="plate-choice" role="group" aria-label="Project plates">
    {plates.map((plate) => <button key={plate.index} aria-pressed={plate.index === current} disabled={busy}
      title={`Select plate ${plate.index}${plate.name ? `: ${plate.name}` : ''}`}
      onClick={() => setState((s) => ({
        projectPlates: s.projectPlates.map((p) => p.index === s.plateIndex ? { ...p, objects: s.objects } : p),
        objects: s.projectPlates.find((p) => p.index === plate.index)!.objects,
        plateIndex: plate.index, selected: null, gizmo: 'none',
      }))}>Plate {plate.index}{plate.name ? ` · ${plate.name}` : ''}</button>)}
  </div>;
}
