import type * as Three from 'three';
import type * as GeometryUtils from 'three/examples/jsm/utils/BufferGeometryUtils.js';

export let T3: typeof Three;
export let T3Utils: typeof GeometryUtils;
export const threeReady: Promise<void> = Promise.all([import('three'), import('three/examples/jsm/utils/BufferGeometryUtils.js')])
  .then(([three, utils]) => { T3 = three; T3Utils = utils; });
