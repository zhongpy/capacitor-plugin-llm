import { registerPlugin } from '@capacitor/core';

import type { LLMPlugin } from './definitions';

const LLM = registerPlugin<LLMPlugin>('LLM', {
  web: () => import('./web').then((m) => new m.LLMWeb()),
});

export * from './definitions';
export { LLM };
