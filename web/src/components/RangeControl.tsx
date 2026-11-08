import { Slider } from 'radix-ui';
import type { CSSProperties } from 'react';
import './range.css';
export default function RangeControl({value, min = 0, max = 100, step = 1, onValueChange, onValueCommit, disabled, className = '', title, ...props}: {value: number; min?: number; max?: number; step?: number; onValueChange(value: number): void;  onValueCommit?(value: number): void; disabled?: boolean; className?: string; title?: string; 'aria-label'?: string; style?: CSSProperties}) {
  return <Slider.Root {...props} className={`orca-range ${className}`} min={min} max={Math.max(min + step, max)} step={step} value={[Math.max(min, Math.min(max, value))]} onValueChange={([next]) => onValueChange(next)} onValueCommit={onValueCommit && (([next]) => onValueCommit(next))} disabled={disabled} aria-description={title}><Slider.Track className="orca-range-track"><Slider.Range className="orca-range-fill" /></Slider.Track><Slider.Thumb className="orca-range-thumb" aria-label={props['aria-label']} /></Slider.Root>;
}
