import { registerPlugin } from '@capacitor/core';

import type { LLMPlugin } from './definitions';

export const LLM = registerPlugin<LLMPlugin>('LLM', {
  web: () => import('./web').then((m) => new m.LLMWeb()),
});

export * from './definitions';
