import { useEffect, useState } from 'react';
import { responseError } from '../api/access';

interface SlicedMapping { filament_map: number[]; filament_volume_map: number[] }

export function useSlicedMapping(job: string | undefined, enabled: boolean) {
  const [result, setResult] = useState<{ job: string; mapping?: SlicedMapping; error?: string }>();
  useEffect(() => {
    if (!job || !enabled) return;
    const abort = new AbortController();
    void (async () => {
      try {
        const response = await fetch(`/api/jobs/${encodeURIComponent(job)}/bambu-mapping`, { signal: abort.signal });
        if (!response.ok) throw await responseError(response);
        const mapping = await response.json() as SlicedMapping;
        if (!abort.signal.aborted) setResult({ job, mapping });
      } catch (error) {
        if (!abort.signal.aborted) setResult({ job, error: error instanceof Error ? error.message : String(error) });
      }
    })();
    return () => abort.abort();
  }, [job, enabled]);
  return enabled && result?.job === job ? result : undefined;
}
