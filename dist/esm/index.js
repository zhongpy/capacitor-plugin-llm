import { registerPlugin } from '@capacitor/core';
export const LLM = registerPlugin('LLM', {
    web: () => import('./web').then(m => new m.LLMWeb()),
});
export * from './definitions';
//# sourceMappingURL=index.js.map