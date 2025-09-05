import { WebPlugin } from '@capacitor/core';
import type {
  LLMPlugin,
  InitOptions,
  ChatOptions,
  GenerateEssayOptions,
  LLMTokenEvent,
  LLMDoneEvent,
} from './definitions';

export class LLMWeb extends WebPlugin implements LLMPlugin {
  private abort?: AbortController;

  constructor() {
    super();
  }

  async init(_options: InitOptions): Promise<void> {
    // Web 端不做本地模型管理；如需接云端，可在此保存 endpoint/key 等
    return;
  }

  async chat(options: ChatOptions): Promise<void> {
    // 这里提供一个非常简易的“模拟流式”以保证前端联调
    const text = `[LLMWeb mock] ${options.prompt}`;
    this.abort = new AbortController();

    for (const ch of text) {
      if (this.abort.signal.aborted) break;
      await new Promise(r => setTimeout(r, 8));
      this.notifyListeners('llmToken', { token: ch } as LLMTokenEvent);
    }
    this.notifyListeners('llmDone', {} as LLMDoneEvent);
  }

  async stop(): Promise<void> {
    this.abort?.abort();
  }

  async free(): Promise<void> {
    // no-op for web
    return;
  }

  async generateEssay(options: GenerateEssayOptions): Promise<{ text: string }> {
    const title = options.title ?? 'An Essay';
    const len = options.word_limit ?? 200;
    // 纯前端占位返回
    return { text: `[LLMWeb mock essay] ${title} (~${len} words)` };
  }
}

export default LLMWeb;
