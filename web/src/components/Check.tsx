import { Suspense, type ComponentProps } from 'react';
import { preloadable } from '../preloadable';
const CheckControl = preloadable(() => import('./CheckControl'));
export function Check(props: ComponentProps<typeof CheckControl>) {
  return <Suspense fallback={<span className="control-check" aria-busy="true"/>}><CheckControl {...props}/></Suspense>;
}
