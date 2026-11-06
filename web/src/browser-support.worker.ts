
self.postMessage({ module: import.meta.url, error: typeof DecompressionStream === 'undefined' || typeof ReadableStream === 'undefined' ||
  typeof TransformStream === 'undefined' || typeof fetch === 'undefined' ||
  typeof Blob.prototype.stream !== 'function' ? 'Required streaming features are unavailable in workers.' : '' });
