export type LLMTokenEvent = { token: string };
export type LLMDoneEvent  = Record<string, never>;
export type LLMErrorEvent = { message: string };

export interface InitOptions {
  /** 优先从 assets 复制（例：models/Qwen3-0.6B-Instruct-q4_k_m.gguf） */
  assetPath?: string;
  /** 可选的 SHA256 校验（hex 小写/大写都可） */
  expectedSha256?: string;
  /** 已存在的绝对路径（若提供优先生效） */
  modelPath?: string;
  /** 本地没有则从该 URL 下载到 filesDir/models/ */
  remoteUrl?: string;
  /** 上下文长度（默认 1024） */
  nCtx?: number;
}

export interface ChatOptions {
  /** 用户提示词（会包上 ChatML 模板） */
  prompt: string;
}

export interface GenerateEssayOptions {
  /** 作文标题 */
  title?: string;
  /** 目标字数（近似） */
  word_limit?: number;
  /** 语言（默认 en） */
  lang?: string;
  /** 约束条件 */
  constraints?: {
    /** 高错误率词 */
    high_error_words?: string[];
    /** 高频词 */
    high_freq_words?: string[];
  };
  /** 最大新生成 tokens（默认 max(256, word_limit*3)） */
  max_new_tokens?: number;
}

export interface LLMPlugin {
  /** 初始化（复制/下载模型 + nativeInit） */
  init(options: InitOptions): Promise<void>;

  /** 开始流式对话（逐 token 触发 llmToken，结束触发 llmDone） */
  chat(options: ChatOptions): Promise<void>;

  /** 尝试中断当前流式生成 */
  stop(): Promise<void>;

  /** 释放底层资源（模型/上下文） */
  free(): Promise<void>;

  /** 一次性生成（作文） */
  generateEssay(options: GenerateEssayOptions): Promise<{ text: string }>;

  // Events
  addListener(eventName: 'llmToken', listenerFunc: (event: LLMTokenEvent) => void): Promise<PluginListenerHandle>;
  addListener(eventName: 'llmDone',  listenerFunc: (event: LLMDoneEvent)  => void): Promise<PluginListenerHandle>;
  addListener(eventName: 'llmError', listenerFunc: (event: LLMErrorEvent) => void): Promise<PluginListenerHandle>;
}

export interface PluginListenerHandle {
  remove: () => Promise<void>;
}
