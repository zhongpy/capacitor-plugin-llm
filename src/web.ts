// web.ts
import { WebPlugin } from '@capacitor/core';

import type {
  LLMPlugin,
  InitOptions,
  ChatOptions,
  GenerateEssayOptions,
  LLMTokenEvent,
  LLMDoneEvent,
  SetSamplingOptions,
} from './definitions';

export class LLMWeb extends WebPlugin implements LLMPlugin {
  private abort?: AbortController;

  async init(_options: InitOptions): Promise<void> {
    return;
  }

  async chat(options: ChatOptions): Promise<void> {
    const text = `[LLMWeb mock] ${options.prompt}`;
    this.abort = new AbortController();
    for (const ch of text) {
      if (this.abort.signal.aborted) break;
      await new Promise((r) => setTimeout(r, 8));
      this.notifyListeners('llmToken', { token: ch } as LLMTokenEvent);
    }
    this.notifyListeners('llmDone', {} as LLMDoneEvent);
  }

  async stop(): Promise<void> {
    this.abort?.abort();
  }
  async free(): Promise<void> {
    return;
  }

  async setSampling(_options: SetSamplingOptions): Promise<void> {
    return;
  }

  async generateEssay(options: GenerateEssayOptions): Promise<{ text: string }> {
    const title = options.title ?? 'An Essay';
    const len = options.word_limit ?? 200;
    return { text: `[LLMWeb mock essay] ${title} (~${len} words)` };
  }
}
export default LLMWeb;
