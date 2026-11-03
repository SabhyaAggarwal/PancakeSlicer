import { lazy, Suspense, type ComponentProps } from 'react';
const RangeControl = lazy(() => import('./RangeControl'));
export function Range(props: ComponentProps<typeof RangeControl>) {
  return <Suspense fallback={<span className="orca-range" aria-busy="true"><span className="orca-range-track"/></span>}><RangeControl {...props}/></Suspense>;
}
