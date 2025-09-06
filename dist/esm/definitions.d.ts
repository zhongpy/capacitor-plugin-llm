export declare type LLMTokenEvent = {
    token: string;
};
export declare type LLMDoneEvent = Record<string, never>;
export declare type LLMErrorEvent = {
    message: string;
};
export interface InitOptions {
    assetPath?: string;
    expectedSha256?: string;
    modelPath?: string;
    remoteUrl?: string;
    nCtx?: number;
}
export interface ChatOptions {
    prompt: string;
}
export interface GenerateEssayOptions {
    title?: string;
    word_limit?: number;
    lang?: string;
    constraints?: {
        high_error_words?: string[];
        high_freq_words?: string[];
    };
    max_new_tokens?: number;
}
export interface SetSamplingOptions {
    temp?: number;
    topP?: number;
    topK?: number;
    repeatPenalty?: number;
    repeatLastN?: number;
    minP?: number;
}
export interface PluginListenerHandle {
    remove: () => Promise<void>;
}
export interface LLMPlugin {
    init(options: InitOptions): Promise<void>;
    chat(options: ChatOptions): Promise<void>;
    stop(): Promise<void>;
    free(): Promise<void>;
    generateEssay(options: GenerateEssayOptions): Promise<{
        text: string;
    }>;
    /** 新增：动态调采样参数（映射到 nativeSetSampling） */
    setSampling(options: SetSamplingOptions): Promise<void>;
    addListener(eventName: 'llmToken', listenerFunc: (event: LLMTokenEvent) => void): Promise<PluginListenerHandle>;
    addListener(eventName: 'llmDone', listenerFunc: (event: LLMDoneEvent) => void): Promise<PluginListenerHandle>;
    addListener(eventName: 'llmError', listenerFunc: (event: LLMErrorEvent) => void): Promise<PluginListenerHandle>;
}
