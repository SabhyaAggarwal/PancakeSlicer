import { Choice } from '../components/kit';
import { Dropdown } from '../components/Dropdown';
import { setLayerHeight, useStore } from '../state/store';
import { availableHeights, effectiveLayerHeight } from './context';
import './layer-height.css';

const label = (h: number) => h.toFixed(2);

export function AutoInputs() {
  const offered = useStore((s) => availableHeights(s).join(','));
  const height = useStore((s) => effectiveLayerHeight(s));
  const heights = offered ? offered.split(',').map(Number) : [];
  const value = height === null ? '' : label(height);
  const choose = (id: string) => setLayerHeight(Number(id));
  return <>
    {heights.length > 0 && <div className="rail-row rail-row-wide layer-heights">
      {heights.length > 6
        ? <Dropdown className="combo" aria-label="Layer height" value={value} onValueChange={choose} options={heights.map((h) => ({ value: label(h), label: `${label(h)} mm` }))}/>
        : <Choice label="Layer height" value={value} options={heights.map((h) => ({ id: label(h), label: label(h) }))} onChange={choose}/>}
    </div>}
  </>;
}
