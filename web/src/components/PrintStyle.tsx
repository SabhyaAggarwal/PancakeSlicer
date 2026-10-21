import { lazy, Suspense, useRef, useState } from 'react';
import { applyStyle, isRotary, modeLabel, STYLES, styleOf, type Style, type StyleId } from '../print-style';
import { STYLE_NAMES } from '../style-names';
import { effectiveConfig, getState, useStore } from '../state/store';
import { IconButton } from './Icon';
import { styleIcon } from './icons';
import './print-style.css';

const StyleGuide = lazy(() => import('./StyleGuide'));
const ROTARY_HINT = 'Tilted chunks and rotary curved layers need a 5-axis printer (Prodcan3, FR3 cell).';

function showSetting() {
  const panel = document.querySelector('.process-panel');
  if (!panel) return;
  [...panel.querySelectorAll<HTMLButtonElement>('.page-tab')].find((b) => b.textContent === 'Quality')?.click();
  requestAnimationFrame(() => {
    const line = [...panel.querySelectorAll<HTMLElement>('.opt-line')].find((l) => l.querySelector('.opt-label')?.textContent?.trim() === 'Non-planar mode');
    if (!line) return;
    line.scrollIntoView({ block: 'center' });
    line.querySelector<HTMLElement>('select')?.focus();
    line.classList.add('ps-flash');
    setTimeout(() => line.classList.remove('ps-flash'), 1600);
  });
}

export default function PrintStyle() {
  useStore((s) => s.dirty);
  useStore((s) => s.printer);
  useStore((s) => s.userPresets);
  useStore((s) => s.projectPresets);
  useStore((s) => s.processes.length);
  const process = useStore((s) => s.process);
  const object = useStore((s) => (s.processScope === 'objects' ? s.objects.find((o) => o.id === s.selected?.object) : undefined));
  const recommended = useStore((s) => {
    const o = object ?? (s.processScope === 'global' && s.objects.length === 1 ? s.objects[0] : undefined);
    return o ? s.recommendedStyle[o.id] : undefined;
  });
  const [guide, setGuide] = useState(false);
  const [tip, setTip] = useState<{ style: Style; left: number } | null>(null);
  const row = useRef<HTMLDivElement>(null);
  if (object?.project) return null;

  const s = getState();
  const global = effectiveConfig(s, 'process');
  const rotary = isRotary(effectiveConfig(s, 'machine'));
  const target = object ? { ...global, ...object.settings } : global;
  const current = styleOf(target);
  const plate = styleOf(global);
  const shown = STYLES.filter((st) => !st.rotary || rotary);
  const known = shown.some((st) => st.id === current);
  const nameOf = (id: StyleId | 'custom', c = target) => (id === 'custom' ? `Custom (${modeLabel(c)})` : STYLE_NAMES[id]);
  const pick = (id: StyleId) => applyStyle(id, object?.id);

  const onKey = (e: React.KeyboardEvent) => {
    const step = { ArrowRight: 1, ArrowDown: 1, ArrowLeft: -1, ArrowUp: -1 }[e.key];
    if (!step) return;
    e.preventDefault();
    const i = Math.max(0, shown.findIndex((st) => st.id === current));
    const next = shown[(i + step + shown.length) % shown.length];
    pick(next.id);
    requestAnimationFrame(() => row.current?.querySelector<HTMLElement>(`[data-style="${next.id}"]`)?.focus());
  };
  const showTip = (style: Style, el: HTMLElement) => setTip({ style, left: el.offsetLeft });

  let line: React.ReactNode = null;
  if (!known)
    line = <>{nameOf(current)}{current === 'custom' || (current === 'oneshot' && rotary) ? '' : ': needs rotary axes'} · <button type="button" className="ps-link" onClick={showSetting}>Show setting</button></>;
  else if (object && current !== plate) line = <>Only {object.name} · plate: {nameOf(plate, global)}</>;
  else if (!object && /non-planar/i.test(process) && current === 'standard') line = <>This process is tuned for curved tops; Standard prints it flat.</>;

  return (
    <div className="ps-strip">
      <div className="ps-row" ref={row}>
        <span className="opt-label" id="ps-label">Print style</span>
        <div className="ps-btns" role="radiogroup" aria-labelledby="ps-label" onKeyDown={onKey} onMouseLeave={() => setTip(null)}>
          {shown.map((st, i) => {
            const on = st.id === current;
            return (
              <button key={st.id} type="button" role="radio" aria-checked={on} data-style={st.id} tabIndex={on || (!known && i === 0) ? 0 : -1}
                data-recommended={recommended === st.id || undefined} aria-description={st.tip}
                className={`ps-btn ${on ? 'active' : ''}`} onClick={() => pick(st.id)}
                onMouseEnter={(e) => showTip(st, e.currentTarget)} onFocus={(e) => showTip(st, e.currentTarget)} onBlur={() => setTip(null)}>
                <span dangerouslySetInnerHTML={{ __html: styleIcon(st.icon) }} />
                <span className="ps-name">{st.label ?? st.name}</span>
              </button>
            );
          })}
        </div>
        <IconButton icon="help" className="ps-help" title={rotary ? 'Print styles' : `Print styles. ${ROTARY_HINT}`} onClick={() => setGuide(true)} />
        {tip && (
          <div className="ps-tip" role="tooltip" style={{ left: tip.left }}>
            <span dangerouslySetInnerHTML={{ __html: styleIcon(tip.style.icon, 96) }} />
            {tip.style.tip}
          </div>
        )}
      </div>
      {line && <div className="ps-line">{line}</div>}
      {guide && <Suspense fallback={<div role="status">Loading the guide…</div>}><StyleGuide target={target} objectId={object?.id} close={() => setGuide(false)} /></Suspense>}
    </div>
  );
}
