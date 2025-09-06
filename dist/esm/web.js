// web.ts
import { WebPlugin } from '@capacitor/core';
export class LLMWeb extends WebPlugin {
    async init(_options) {
        return;
    }
    async chat(options) {
        const text = `[LLMWeb mock] ${options.prompt}`;
        this.abort = new AbortController();
        for (const ch of text) {
            if (this.abort.signal.aborted)
                break;
            await new Promise((r) => setTimeout(r, 8));
            this.notifyListeners('llmToken', { token: ch });
        }
        this.notifyListeners('llmDone', {});
    }
    async stop() {
        var _a;
        (_a = this.abort) === null || _a === void 0 ? void 0 : _a.abort();
    }
    async free() {
        return;
    }
    async setSampling(_options) {
        return;
    }
    async generateEssay(options) {
        var _a, _b;
        const title = (_a = options.title) !== null && _a !== void 0 ? _a : 'An Essay';
        const len = (_b = options.word_limit) !== null && _b !== void 0 ? _b : 200;
        return { text: `[LLMWeb mock essay] ${title} (~${len} words)` };
    }
}
export default LLMWeb;
//# sourceMappingURL=web.js.map