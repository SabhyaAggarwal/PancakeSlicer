import { Range } from './Range';


import { setAssembly, useAssembly } from '../plater/assembly';

export default function AssemblyBar() {
  const { ratio } = useAssembly();
  return (
    <div className="assembly-bar">
      <span>Explosion Ratio</span>
      <Range min={1} max={3} step={0.01} value={ratio} aria-label="Explosion Ratio" onValueChange={(next) => setAssembly({ ratio: next })} />
      <span className="num">{ratio.toFixed(2)}</span>
      <button type="button" className="btn small" onClick={() => setAssembly({ on: false })}>Return</button>
    </div>
  );
}
