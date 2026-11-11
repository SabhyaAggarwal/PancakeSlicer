import { useEffect, useMemo, useState } from 'react';
import { responseError } from '../api/access';
import { getState, useStore } from '../state/store';
import type { PrinterSlots } from '../printers/slots';
import { findProfile } from './identity';

export interface OnPrinter { where: string[] }

export function useOnPrinter(enabled: boolean): ReadonlyMap<string, OnPrinter> {
  const connections = useStore((s) => s.printerConnections.connections);
  const trays = useStore((s) => (enabled ? JSON.stringify(Object.values(s.printerConnections.status).map((st) => [st.printer, st.trays ?? null])) : ''));
  const library = useStore((s) => s.profileLibraryRevision);
  const [printers, setPrinters] = useState<Record<string, PrinterSlots>>({});
  const on = enabled && connections.length > 0;
  useEffect(() => {
    if (!on) return;
    let live = true;
    fetch('/api/printer-slots').then(async (r) => { if (!r.ok) throw await responseError(r); return (await r.json() as { printers: Record<string, PrinterSlots> }).printers; })
      .then((next) => { if (live) setPrinters(next); }, () => {});
    return () => { live = false; };
  }, [on, trays, library]);
  return useMemo(() => {
    const out = new Map<string, OnPrinter>();
    if (!on) return out;
    const nameOf = (printer: string) => connections.find((c) => c.printer === printer)?.name || findProfile(getState(), 'machine', printer)?.name || 'Printer';
    for (const [printer, p] of Object.entries(printers)) for (const m of p.slots) {
      if (!m.filament) continue;
      const entry = out.get(m.filament) ?? { where: [] };
      entry.where.push(`${nameOf(printer)}, ${m.label}`);
      out.set(m.filament, entry);
    }
    return out;
  }, [on, printers, connections]);
}
