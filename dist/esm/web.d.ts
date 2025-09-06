import { WebPlugin } from '@capacitor/core';
import type { LLMPlugin, InitOptions, ChatOptions, GenerateEssayOptions, SetSamplingOptions } from './definitions';
export declare class LLMWeb extends WebPlugin implements LLMPlugin {
    private abort?;
    init(_options: InitOptions): Promise<void>;
    chat(options: ChatOptions): Promise<void>;
    stop(): Promise<void>;
    free(): Promise<void>;
    setSampling(_options: SetSamplingOptions): Promise<void>;
    generateEssay(options: GenerateEssayOptions): Promise<{
        text: string;
    }>;
}
export default LLMWeb;
