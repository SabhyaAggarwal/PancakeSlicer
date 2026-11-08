import { setState, useStore } from '../state/store';
import { canAddPlate, moveToPlate, OBJECT_DRAG, selectPlate } from '../plater/plates';
import { Sep } from '../separator';

export default function PlateChoice() {
  const plates = useStore((s) => s.projectPlates);
  const current = useStore((s) => s.plateIndex);
  const busy = useStore((s) => s.job.state === 'running' || s.job.state === 'submitting');
  const meshPlates = useStore(canAddPlate);
  const preview = useStore((s) => s.tab === 'preview');
  return <div className={`plate-choice ${preview ? 'in-preview' : ''}`} role="group" aria-label="Project plates">
    {plates.map((plate) => <button key={plate.index} aria-pressed={plate.index === current} disabled={busy}

      onClick={() => selectPlate(plate.index)}
      onContextMenu={(e) => { e.preventDefault(); selectPlate(plate.index); setState({ contextMenu: { x: e.clientX, y: e.clientY } }); }}
      onDragOver={(e) => { if (meshPlates && plate.index !== current && e.dataTransfer.types.includes(OBJECT_DRAG)) e.preventDefault(); }}
      onDrop={(e) => { e.preventDefault(); moveToPlate(e.dataTransfer.getData(OBJECT_DRAG).split(',').map(Number), plate.index); }}
    >Plate {plate.index}{plate.name && plate.name !== `Plate ${plate.index}` && <><Sep/>{plate.name}</>}</button>)}
  </div>;
}
