import { lazy, Suspense, type ComponentProps } from 'react';
const CheckControl = lazy(() => import('./CheckControl'));
export function Check(props: ComponentProps<typeof CheckControl>) {
  return <Suspense fallback={<span className="control-check" aria-busy="true"/>}><CheckControl {...props}/></Suspense>;
}
