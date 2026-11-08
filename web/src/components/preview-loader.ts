import type { ComponentType } from 'react';
import { threeReady } from '../three-runtime';

let viewer: ComponentType | undefined;
export const loadPreviewView = () => Promise.all([import('./PreviewView'), import('../preview/data'), threeReady])
  .then(([m, data]) => { data.prewarmMovesLoader(); return (viewer = m.PreviewView); });
export const loadedPreviewView = () => viewer;
