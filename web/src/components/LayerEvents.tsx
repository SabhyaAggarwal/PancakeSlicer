import { useEffect, useRef, useState } from 'react';
import type { LayerRec } from '../preview/format';
import { filamentColor } from '../plater/filamentColor';
import { effectiveConfig, getState, setEvents, useStore, type LayerEvent } from '../state/store';
import type { SliderTick } from './PreviewSliders';

const COLOR: Record<LayerEvent['type'], string> = {
  pause: '#E67E22', custom: '#3A7BD5', template: '#8E44AD', colour: '#E53935', change: '#00AE42',
};

export function eventLabel(e: LayerEvent): string {
  switch (e.type) {
    case 'pause': return 'Pause';
    case 'custom': return 'Custom G-code';
    case 'template': return 'Custom template';
    case 'colour': return 'Color change';
    case 'change': return `Change filament ${e.text}`;
  }
}

export function eventArg(e: LayerEvent): string {
  const z = String(Math.round(e.z * 1e4) / 1e4);
  if (e.type === 'custom') return `${z}:custom:${(e.text ?? '').replace(/\r?\n/g, '\\n')}`;
  if (e.type === 'change') return `${z}:change:${e.text}`;
  return `${z}:${e.type}`;
}

const sameZ = (a: number, b: number) => Math.abs(a - b) < 1e-3;

export function eventTicks(events: LayerEvent[], layers: LayerRec[]): SliderTick[] {
  return events.flatMap((e) => {
    const i = layers.findIndex((l) => l.z >= e.z - 1e-3);
    return i < 0 ? [] : [{ layer: i, color: COLOR[e.type], title: `${eventLabel(e)} at ${e.z.toFixed(2)} mm${e.text && e.type === 'custom' ? `: ${e.text}` : ''}` }];
  });
}

export function EventMenu({ anchor, z, layer, onClose }: { anchor: DOMRect; z: number; layer: number; onClose(): void }) {
  const events = useStore((s) => s.events);
  const slots = useStore((s) => s.filamentSlots.length);
  const template = useStore((s) => String(effectiveConfig(s, 'machine').template_custom_gcode ?? ''));
  const [custom, setCustom] = useState<string | null>(null);
  const ref = useRef<HTMLDivElement>(null);
  const here = events.filter((e) => sameZ(e.z, z));
  useEffect(() => {
    const onKey = (e: KeyboardEvent) => e.key === 'Escape' && onClose();
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, [onClose]);
  useEffect(() => {
    if (custom !== null) return;
    const close = (e: MouseEvent) => !ref.current?.contains(e.target as Node) && onClose();
    window.addEventListener('mousedown', close);
    return () => window.removeEventListener('mousedown', close);
  }, [custom]);

  const put = (e: LayerEvent) => {
    setEvents([...getState().events.filter((x) => !sameZ(x.z, z)), e], `Add ${eventLabel(e).toLowerCase()}`);
    onClose();
  };
  const del = () => {
    setEvents(getState().events.filter((x) => !sameZ(x.z, z)), 'Delete layer event');
    onClose();
  };
  const item = (label: string, fn: () => void, tip?: string, swatch?: string) => (
    <button key={label} className="menu-item" title={tip} onClick={fn}>
      <span className="menu-label">
        {swatch && <span className="menu-swatch" style={{ background: swatch }} />}
        {label}
      </span>
    </button>
  );
  const style = { right: window.innerWidth - anchor.left + 6, top: Math.min(anchor.top, window.innerHeight - 260) };

  if (custom !== null)
    return (
      <div className="modal-backdrop">
        <div className="modal small" role="dialog" aria-label="Custom G-code">
          <div className="modal-title">
            <span>Custom G-code</span>
            <button className="modal-close" title="Close" onClick={onClose}>
              ×
            </button>
          </div>
          <div className="flush-body">
            <div>Enter Custom G-code used on current layer:</div>
            <textarea className="text multiline gcode-input" autoFocus value={custom} spellCheck={false} onChange={(e) => setCustom(e.target.value)} />
          </div>
          <div className="modal-buttons center">
            <button className="btn primary" disabled={!custom.trim()} onClick={() => put({ z, type: 'custom', text: custom })}>
              OK
            </button>
            <button className="btn" onClick={onClose}>
              Cancel
            </button>
          </div>
        </div>
      </div>
    );

  return (
    <div className="menu-pop event-menu" ref={ref} style={style}>
      <div className="menu-caption">
        Layer {layer + 1} · {z.toFixed(2)} mm
      </div>
      {here.length ? (
        <>
          {here[0].type === 'custom' && item('Edit Custom G-code', () => setCustom(here[0].text ?? ''))}
          {here[0].type === 'change' && slots > 1 &&
            Array.from({ length: slots }, (_, i) =>
              item(`Filament ${i + 1}`, () => put({ z, type: 'change', text: String(i + 1) }), 'Change filament at the beginning of this layer.', filamentColor(getState(), i + 1)),
            )}
          {item(`Delete ${eventLabel(here[0]).replace(/ \d+$/, '')}`, del)}
        </>
      ) : (
        <>
          {slots === 1 && item('Add Color Change', () => put({ z, type: 'colour' }), 'Insert a filament change (M600) at the beginning of this layer.')}
          {item('Add Pause', () => put({ z, type: 'pause' }), 'Insert a pause command at the beginning of this layer.')}
          {item('Add Custom G-code', () => setCustom(''), 'Insert custom G-code at the beginning of this layer.')}
          {template.trim() && item('Add Custom Template', () => put({ z, type: 'template' }), 'Insert template custom G-code at the beginning of this layer.')}
          {slots > 1 && (
            <>
              <div className="menu-sep" />
              <div className="menu-caption">Change Filament</div>
              {Array.from({ length: slots }, (_, i) =>
                item(`Filament ${i + 1}`, () => put({ z, type: 'change', text: String(i + 1) }), 'Change filament at the beginning of this layer.', filamentColor(getState(), i + 1)),
              )}
            </>
          )}
        </>
      )}
    </div>
  );
}

export function EventList({ onPick }: { onPick?(z: number): void }) {
  const events = useStore((s) => s.events);
  if (!events.length) return null;
  return (
    <div className="events-panel">
      <div className="events-title">Custom G-code</div>
      {events.map((e) => (
        <div key={`${e.z}-${e.type}`} className="events-row">
          <span className="events-dot" style={{ background: COLOR[e.type] }} />
          <button className="link events-z" title="Go to this layer" onClick={() => onPick?.(e.z)}>
            {e.z.toFixed(2)} mm
          </button>
          <span className="events-label" title={e.text}>
            {eventLabel(e)}
            {e.type === 'custom' && e.text ? `: ${e.text.split('\n')[0]}` : ''}
          </span>
          <button className="icon-btn" title="Delete" onClick={() => setEvents(getState().events.filter((x) => x !== e), 'Delete layer event')}>
            ×
          </button>
        </div>
      ))}
    </div>
  );
}
