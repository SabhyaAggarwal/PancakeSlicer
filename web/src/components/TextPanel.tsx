import { useEffect, useRef, useState, useSyncExternalStore } from 'react';
import { changeVolumeType, setState, useStore, type TextSpec } from '../state/store';
import { editedText, loadFonts, surfaceOk, updateText, type Font } from '../plater/text';
import { setTextPlacing, subscribeTextPlacing, textPlacing } from '../plater/textPlacing';

const OPERATIONS = [['part', 'Join'], ['negative', 'Cut'], ['modifier', 'Modifier']] as const;

export default function TextPanel() {
  const objectId = useStore((s) => editedText(s)?.object.id);
  const v = useStore((s) => editedText(s)?.volume);
  const current = useStore((s) => editedText(s)?.spec);
  const [fonts, setFonts] = useState<Font[]>([]);
  const placing = useSyncExternalStore(subscribeTextPlacing, textPlacing);
  const canSurface = useStore((s) => !!v && surfaceOk(s, v.type));
  const raises = useStore((s) => !!s.features.emboss_surface_outside);
  useEffect(() => () => setTextPlacing(false), []);
  const [spec, setSpec] = useState<TextSpec | null>(current ?? null);
  const key = objectId !== undefined ? `${objectId}:${v?.id ?? ''}` : '';
  const timer = useRef(0);
  useEffect(() => void loadFonts().then(setFonts), []);
  useEffect(() => setSpec(current ?? null), [key]); // eslint-disable-line react-hooks/exhaustive-deps
  useEffect(() => setSpec((sp) => (sp && current ? { ...sp, surface: current.surface } : sp)), [current?.surface]); // eslint-disable-line react-hooks/exhaustive-deps
  if (objectId === undefined || !spec) return null;
  const edit = (patch: Partial<TextSpec>) => {
    const next = { ...spec, ...patch };
    setSpec(next);
    clearTimeout(timer.current);
    timer.current = window.setTimeout(() => void updateText(next), 400);
  };
  const num = (label: string, k: 'size' | 'depth' | 'charGap' | 'lineSpacing', min: number, max: number, step: number, unit = '') => (
    <label className="prefs-row cut-joint-row"><span>{label}</span>
      <input className="text num" type="number" min={min} max={max} step={step} value={spec[k]} aria-label={label}
        onChange={(e) => { const v = Number(e.target.value); if (Number.isFinite(v)) edit({ [k]: Math.min(max, Math.max(min, v)) }); }} />
      <span>{unit}</span>
    </label>
  );
  return (
    <div className="manip-panel brim-ears text-panel">
      <div className="manip-title">Text</div>
      <textarea className="text" rows={3} value={spec.text} aria-label="Text" onChange={(e) => edit({ text: e.target.value })} />
      {!spec.text.trim() && <div className="brim-warning">Embossed text cannot contain only white spaces.</div>}
      <label className="prefs-row cut-joint-row"><span>Font</span>
        <select className="combo" value={spec.font} aria-label="Font" onChange={(e) => edit({ font: e.target.value })}>
          <option value="">Default</option>
          {fonts.map((f) => <option key={f.id} value={f.id}>{f.name}</option>)}
        </select>
      </label>
      {num('Height', 'size', 0.1, 1000, 0.1, 'mm')}
      {num('Depth', 'depth', 0.01, 1000, 0.1, 'mm')}
      <div className="prefs-row cut-joint-row"><span>Style</span>
        <span className="cut-joint-buttons">
          <button type="button" className={`btn ${spec.bold ? 'primary' : ''}`} aria-pressed={spec.bold} onClick={() => edit({ bold: !spec.bold })}><b>B</b></button>
          <button type="button" className={`btn ${spec.italic ? 'primary' : ''}`} aria-pressed={spec.italic} onClick={() => edit({ italic: !spec.italic })}><i>I</i></button>
        </span>
      </div>
      <label className="prefs-row cut-joint-row"><span>Alignment</span>
        <select className="combo" value={spec.align} aria-label="Alignment" onChange={(e) => edit({ align: e.target.value as TextSpec['align'] })}>
          <option value="left">Left</option><option value="center">Center</option><option value="right">Right</option>
        </select>
      </label>
      {num('Char gap', 'charGap', -100, 100, 1)}
      {num('Line gap', 'lineSpacing', 0.1, 10, 0.1)}
      {v && (
        <label className="prefs-row cut-joint-row" title={canSurface ? undefined : v.type === 'part' ? 'Raised text on a surface needs a newer slicing tool; use Cut or Modifier' : 'Use surface needs a newer slicing tool'}>
          <span>Use surface</span>
          <input type="checkbox" checked={!!spec.surface || placing} disabled={!canSurface && !spec.surface}
            onChange={(e) => { if (e.target.checked) setTextPlacing(true); else { setTextPlacing(false); if (spec.surface) edit({ surface: undefined }); } }} />
        </label>
      )}
      {placing && <div className="cut-joint-hint">Click on the object's surface to place the text</div>}
      {v && (
        <label className="prefs-row cut-joint-row"><span>Operation</span>
          <select className="combo" value={v.type} aria-label="Operation" onChange={(e) => { changeVolumeType(objectId, v.id, e.target.value as typeof v.type); if (spec.surface) void updateText(spec);  }}>
            {OPERATIONS.map(([t, label]) => <option key={t} value={t} disabled={t === 'part' && !!spec.surface && !raises}>{label}</option>)}
          </select>
        </label>
      )}
      <div className="cut-joint-buttons"><span style={{ flex: 1 }} /><button type="button" className="btn primary" onClick={() => setState({ gizmo: 'none' })}>Done</button></div>
    </div>
  );
}
