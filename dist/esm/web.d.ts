import { WebPlugin } from '@capacitor/core';
import type { LLMPlugin, InitOptions, ChatOptions, GenerateEssayOptions } from './definitions';
export declare class LLMWeb extends WebPlugin implements LLMPlugin {
    private abort?;
    constructor();
    init(_options: InitOptions): Promise<void>;
    chat(options: ChatOptions): Promise<void>;
    stop(): Promise<void>;
    free(): Promise<void>;
    generateEssay(options: GenerateEssayOptions): Promise<{
        text: string;
    }>;
}
export default LLMWeb;
