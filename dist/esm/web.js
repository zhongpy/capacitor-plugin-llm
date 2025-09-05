import { WebPlugin } from '@capacitor/core';
export class LLMWeb extends WebPlugin {
    constructor() {
        super();
    }
    async init(_options) {
        // Web 端不做本地模型管理；如需接云端，可在此保存 endpoint/key 等
        return;
    }
    async chat(options) {
        // 这里提供一个非常简易的“模拟流式”以保证前端联调
        const text = `[LLMWeb mock] ${options.prompt}`;
        this.abort = new AbortController();
        for (const ch of text) {
            if (this.abort.signal.aborted)
                break;
            await new Promise(r => setTimeout(r, 8));
            this.notifyListeners('llmToken', { token: ch });
        }
        this.notifyListeners('llmDone', {});
    }
    async stop() {
        var _a;
        (_a = this.abort) === null || _a === void 0 ? void 0 : _a.abort();
    }
    async free() {
        // no-op for web
        return;
    }
    async generateEssay(options) {
        var _a, _b;
        const title = (_a = options.title) !== null && _a !== void 0 ? _a : 'An Essay';
        const len = (_b = options.word_limit) !== null && _b !== void 0 ? _b : 200;
        // 纯前端占位返回
        return { text: `[LLMWeb mock essay] ${title} (~${len} words)` };
    }
}
export default LLMWeb;
//# sourceMappingURL=web.js.map