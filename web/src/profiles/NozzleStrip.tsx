import { useEffect, useState } from 'react';
import { Choice } from '../components/kit';
import { toast, useStore } from '../state/store';
import { resolveProfile } from './model';
import { nozzleModel, switchNozzle, type NozzleModel } from './nozzle-switch';
import './layer-height.css';

export function NozzleStrip({ printerKey, disabled }: { printerKey: string; disabled?: boolean }) {
  const base = useStore((s) => resolveProfile(s, 'machine', printerKey).base?.name ?? '');
  const [found, setFound] = useState<NozzleModel>();
  const [busy, setBusy] = useState(false);
  useEffect(() => {
    let live = true;
    nozzleModel(base).then((f) => live && setFound(f), (e) => live && (setFound(undefined), toast(e instanceof Error ? e.message : String(e))));
    return () => { live = false; };
  }, [base]);
  if (!found || found.model.nozzles.length < 2) return null;
  const choose = (nozzle: string) => {
    setBusy(true);
    switchNozzle(printerKey, nozzle).catch((e) => toast(e instanceof Error ? e.message : String(e), null, null, 'warning')).finally(() => setBusy(false));
  };
  return <div className="nozzle-strip"><Choice label="Nozzle" value={found.nozzle} options={found.model.nozzles.map((n) => ({ id: n, label: n }))} disabled={disabled || busy} onChange={choose}/></div>;
}
