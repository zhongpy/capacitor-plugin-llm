import { WebPlugin } from '@capacitor/core';

import type { LLMPlugin } from './definitions';

export class LLMWeb extends WebPlugin implements LLMPlugin {
  async echo(options: { value: string }): Promise<{ value: string }> {
    console.log('ECHO', options);
    return options;
  }
}
