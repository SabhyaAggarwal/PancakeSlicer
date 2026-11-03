import { Choice } from '../components/kit';
import { Dropdown } from '../components/Dropdown';
import { setLayerHeight, useStore } from '../state/store';
import { effectiveLayerHeight, printContext, processHeights } from './context';
import { sameMm } from './dynamic';
import './layer-height.css';

const label = (h: number) => h.toFixed(2);

export function LayerHeightChooser() {
  const offered = useStore((s) => processHeights(s).join(','));
  const effective = useStore(effectiveLayerHeight);
  const stored = useStore((s) => s.processVariant?.layerHeight ?? null);
  const nozzle = useStore((s) => printContext(s).nozzle);
  if (!offered || effective === null) return null;
  const heights = offered.split(',').map(Number), value = label(effective);
  const unavailable = stored !== null && !heights.some((h) => sameMm(h, stored));
  const choose = (id: string) => setLayerHeight(Number(id));
  return <div className="rail-row rail-row-wide layer-heights">
    {heights.length > 6
      ? <Dropdown className="combo" aria-label="Layer height" value={value} onValueChange={choose} options={heights.map((h) => ({ value: label(h), label: `${label(h)} mm` }))}/>
      : <Choice label="Layer height" value={value} options={heights.map((h) => ({ id: label(h), label: label(h) }))} onChange={choose}/>}
    {unavailable && <p className="layer-heights-note">Your {label(stored)} isn't available on {nozzle} mm</p>}
  </div>;
}
