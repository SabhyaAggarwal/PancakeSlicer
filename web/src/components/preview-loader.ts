import type { ComponentType } from 'react';

let viewer: ComponentType | undefined;
export const loadPreviewView = () => import('./PreviewView').then((m) => (viewer = m.PreviewView));
export const loadedPreviewView = () => viewer;
