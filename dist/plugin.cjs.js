'use strict';

var core = require('@capacitor/core');

const LLM = core.registerPlugin('LLM', {
    web: () => Promise.resolve().then(function () { return web; }).then((m) => new m.LLMWeb()),
});

// web.ts
class LLMWeb extends core.WebPlugin {
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

var web = /*#__PURE__*/Object.freeze({
    __proto__: null,
    LLMWeb: LLMWeb
});

exports.LLM = LLM;
//# sourceMappingURL=plugin.cjs.js.map
