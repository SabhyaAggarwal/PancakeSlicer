import type { ComponentType } from 'react';

let viewer: ComponentType | undefined;
export const loadPreviewView = () => Promise.all([import('./PreviewView'), import('../preview/data')])
  .then(([m, data]) => { data.prewarmMovesLoader(); return (viewer = m.PreviewView); });
export const loadedPreviewView = () => viewer;
