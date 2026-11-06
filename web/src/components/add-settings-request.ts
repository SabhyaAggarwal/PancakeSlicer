

import { useEffect } from 'react';

let pending = false;
const listeners = new Set<() => void>();

export function requestAddSettings() {
  pending = true;
  listeners.forEach((l) => l());
}


export function useAddSettingsRequest(ready: boolean, open: () => void) {
  useEffect(() => {
    if (!ready) return;
    const run = () => { if (pending) { pending = false; open(); } };
    run();
    listeners.add(run);
    return () => void listeners.delete(run);
  }, [ready]); // eslint-disable-line react-hooks/exhaustive-deps
}
