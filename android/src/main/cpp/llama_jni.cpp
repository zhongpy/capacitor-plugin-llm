#include <jni.h>
#include <atomic>
#include <string>
#include <vector>
#include <cstdint>
#include <algorithm>
#include <limits>
#include "llama.h"
#include <android/log.h>

// ===== 全局（单模型/单上下文） =====
static llama_model*           g_model  = nullptr;
static llama_context*         g_ctx    = nullptr;
static const llama_vocab*     g_vocab  = nullptr;
static std::atomic<bool>      g_stop{false};
#define LOG_TAG "MyNativeModule"
#define LOGV(...) __android_log_print(ANDROID_LOG_VERBOSE, LOG_TAG, __VA_ARGS__)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
// ===== ChatML（Qwen3风格）prompt =====
static std::string build_chatml_prompt(const std::string& user) {
    std::string s;
    s += "<|im_start|>system\nYou are a helpful assistant.<|im_end|>\n";
    s += "<|im_start|>user\n" + user + "<|im_end|>\n";
    s += "<|im_start|>assistant\n";
    return s;
}

// ===== 作文 prompt（一次性输出）=====
static std::string build_essay_prompt(
        const std::string& title,
        int                word_limit,
        const std::string& lang,
        const std::vector<std::string>& hi_err,
        const std::vector<std::string>& hi_freq) {

    std::string s;
    s += "Write a " + lang + " essay.\n";
    s += "Title: " + title + "\n";
    s += "Length: ~" + std::to_string(std::max(50, word_limit)) + " words.\n";
    s += "Requirements:\n";
    s += "- Clear structure with introduction, body, and conclusion.\n";
    s += "- Use simple sentences suitable for ESL learners.\n";
    if (!hi_err.empty()) {
        s += "- Pay attention to commonly mistaken words: ";
        for (size_t i = 0; i < hi_err.size(); ++i) {
            s += hi_err[i];
            s += (i + 1 == hi_err.size()) ? ".\n" : ", ";
        }
    }
    if (!hi_freq.empty()) {
        s += "- Try to include high-frequency vocabulary: ";
        for (size_t i = 0; i < hi_freq.size(); ++i) {
            s += hi_freq[i];
            s += (i + 1 == hi_freq.size()) ? ".\n" : ", ";
        }
    }
    s += "- Avoid overly complex grammar. Keep the vocabulary practical.\n";
    s += "Now produce only the final essay content.\n";
    // 直接用“纯文本指令”，不包 ChatML，以免模型把要求一起复述出来
    return s;
}

// 放在文件顶部全局区
static std::string g_pending_utf8;

// 返回 s 中“最后一个**完整** UTF-8 码点边界”的下标（可发出的长度）
static size_t utf8_last_complete(const std::string& s) {
    // 从尾部最多回看 3 字节，判定是否位于多字节序列中
    size_t n = s.size();
    if (n == 0) return 0;
    size_t i = n;
    int cont = 0;
    // 向左扫描最多 3 字节，数连续的 10xxxxxx
    for (size_t k = 0; k < 3 && i > 0; ++k) {
        unsigned char c = (unsigned char)s[i - 1];
        if ((c & 0xC0) == 0x80) { // 10xxxxxx
            ++cont;
            --i;
        } else break;
    }
    if (i == 0) {
        // 全是续字节 -> 不完整
        return 0;
    }
    unsigned char lead = (unsigned char)s[i - 1];
    int need = 1;
    if ((lead & 0x80) == 0x00) need = 1;           // 0xxxxxxx
    else if ((lead & 0xE0) == 0xC0) need = 2;      // 110xxxxx
    else if ((lead & 0xF0) == 0xE0) need = 3;      // 1110xxxx
    else if ((lead & 0xF8) == 0xF0) need = 4;      // 11110xxx
    else return i - 1; // 非法领头，保守起见只到前一个字节

    int have = n - (int)(i - 1);
    if (have >= need) return n;     // 末尾完整
    return n - (need - have);       // 去掉不完整的尾部字节
}

// 仅把“完整 UTF-8”部分抛给 Java，尾巴留在 g_pending_utf8
// —— 新版：不再每次 GetObjectClass/GetMethodID ——

// 把完整 UTF-8 片段发给 Java
static void emit_utf8_safely(JNIEnv* env, jobject cb, jclass cbCls, jmethodID midOnToken,
                             const std::string& chunk) {
    //static std::string g_pending_utf8;  // 迁到函数静态也行；若你全局已定义就删这行
    std::string &buf = g_pending_utf8;
    buf.append(chunk);
    size_t cut = utf8_last_complete(buf);
    if (cut == 0) return;

    std::string out = buf.substr(0, cut);
    buf.erase(0, cut);

    jstring jtok = env->NewStringUTF(out.c_str());
    if (!jtok) return;
    env->CallVoidMethod(cb, midOnToken, jtok);
    env->DeleteLocalRef(jtok);
}

// 刷新尾巴
static void flush_pending(JNIEnv* env, jobject cb, jclass cbCls, jmethodID midOnToken) {
    //extern std::string g_pending_utf8; // 你原来的全局变量
    if (g_pending_utf8.empty()) return;

    size_t cut = utf8_last_complete(g_pending_utf8);
    if (cut == 0) return;

    std::string out = g_pending_utf8.substr(0, cut);
    g_pending_utf8.erase(0, cut);

    if (!out.empty()) {
        jstring jtok = env->NewStringUTF(out.c_str());
        if (!jtok) return;
        env->CallVoidMethod(cb, midOnToken, jtok);
        env->DeleteLocalRef(jtok);
    }
}

// ===== logits / 词表大小（不同提交可能有不同 API 名称，这里做个轻量封装）=====
static inline const float* get_logits(llama_context* ctx) {
    // 如你的头文件是 llama_get_logits_ith(ctx, -1)，改这里即可
    return llama_get_logits(ctx);
}
static inline int32_t vocab_size(const llama_vocab* vocab, llama_context* ctx) {
    // 如果你的头文件有 llama_vocab_n_tokens(vocab)，可改成它
    return llama_vocab_n_tokens(vocab);
}
static inline llama_token greedy_argmax(const float* logits, int32_t n_vocab) {
    int32_t best = 0; float bestv = -std::numeric_limits<float>::infinity();
    for (int32_t i = 0; i < n_vocab; ++i) { if (logits[i] > bestv) { bestv = logits[i]; best = i; } }
    return (llama_token)best;
}

// ===== 构造 llama_batch（单序列 0）=====
struct BatchBuf {
    std::vector<llama_token>    token;
    std::vector<llama_pos>      pos;
    std::vector<int32_t>        n_seq_id;
    std::vector<llama_seq_id>   seq_id_store;
    std::vector<llama_seq_id*>  seq_id_ptrs;
    std::vector<int8_t>         logits;

    void resize(int32_t n) {
        token.resize(n);
        pos.resize(n);
        n_seq_id.assign(n, 1);
        seq_id_store.assign(n, 0);
        seq_id_ptrs.resize(n);
        logits.assign(n, 0);
        for (int32_t i = 0; i < n; ++i) seq_id_ptrs[i] = &seq_id_store[i];
    }
    llama_batch as_batch() {
        llama_batch b{};
        b.n_tokens = (int32_t)token.size();
        b.token    = token.data();
        b.embd     = nullptr;
        b.pos      = pos.data();
        b.n_seq_id = n_seq_id.data();
        b.seq_id   = seq_id_ptrs.data();
        b.logits   = logits.data();
        return b;
    }
};

// ===== 分词（vocab 版，自动扩容）=====
static std::vector<llama_token> tokenize_text(const std::string& text, bool add_special, bool parse_special) {
    std::vector<llama_token> out;
    int32_t need = llama_tokenize(
            g_vocab, text.c_str(), (int32_t)text.size(),
            /*tokens*/ nullptr, /*max*/ 0, add_special, parse_special);
    if (need < 0) need = -need;
    out.resize(need);
    int32_t n = llama_tokenize(
            g_vocab, text.c_str(), (int32_t)text.size(),
            out.data(), (int32_t)out.size(), add_special, parse_special);
    if (n < 0) out.clear(); else out.resize(n);
    return out;
}

// ===== 反词元（vocab 版）=====
static std::string detok_piece(llama_token t) {
    char buf[256];
    int32_t m = llama_token_to_piece(g_vocab, t, buf, (int32_t)sizeof(buf),
            /*lstrip*/ 0, /*special*/ false);
    if (m <= 0) return std::string();
    return std::string(buf, buf + m);
}

// ===== JNI: init(modelPath, nCtx) =====
extern "C" JNIEXPORT jboolean JNICALL
Java_com_kingsun_plugins_llm_LLMPlugin_nativeInit(JNIEnv* env, jclass, jstring modelPath_, jint nCtx) {
    const char* p = env->GetStringUTFChars(modelPath_, nullptr);

    llama_backend_init();

    llama_model_params mparams = llama_model_default_params();
    g_model = llama_load_model_from_file(p, mparams);

    env->ReleaseStringUTFChars(modelPath_, p);
    if (!g_model) return JNI_FALSE;

    g_vocab = llama_model_get_vocab(g_model);
    if (!g_vocab) {
        llama_free_model(g_model); g_model = nullptr;
        return JNI_FALSE;
    }

    llama_context_params cparams = llama_context_default_params();
    cparams.n_ctx = nCtx > 0 ? nCtx : 1024;
    cparams.type_k = GGML_TYPE_Q8_0;
    cparams.type_v = GGML_TYPE_Q8_0;

    g_ctx = llama_new_context_with_model(g_model, cparams);
    if (!g_ctx) {
        llama_free_model(g_model); g_model = nullptr; g_vocab = nullptr;
        return JNI_FALSE;
    }
    return JNI_TRUE;
}

// ===== JNI: free() =====
extern "C" JNIEXPORT void JNICALL
Java_com_kingsun_plugins_llm_LLMPlugin_nativeFree(JNIEnv*, jclass) {
    if (g_ctx)   { llama_free(g_ctx); g_ctx = nullptr; }
    if (g_model) { llama_free_model(g_model); g_model = nullptr; }
    g_pending_utf8.clear();
    g_vocab = nullptr;
    llama_backend_free();
}

// ===== JNI: stop() =====
extern "C" JNIEXPORT void JNICALL
Java_com_kingsun_plugins_llm_LLMPlugin_nativeStop(JNIEnv*, jclass) {
    g_stop = true;
    g_pending_utf8.clear();
}

// ===== JNI: 流式对话 =====
extern "C" JNIEXPORT void JNICALL
Java_com_kingsun_plugins_llm_LLMPlugin_nativeChatStream(JNIEnv* env, jobject thiz, jstring userText_) {
    LOGI("Enter nativeChatStream");
    if (!g_ctx || !g_model || !g_vocab) return;
    LOGI("Process nativeChatStream Step-1");
    // ① 缓存类与方法 ID（本次调用内复用）
    jclass cbCls = env->GetObjectClass(thiz);
    if (!cbCls) return;
    LOGI("Process nativeChatStream Step-2");
    jmethodID midOnToken = env->GetMethodID(cbCls, "onNativeToken", "(Ljava/lang/String;)V");
    jmethodID midOnDone  = env->GetMethodID(cbCls, "onNativeDone",  "()V");
    if (!midOnToken || !midOnDone) { env->DeleteLocalRef(cbCls); return; }
    LOGI("Process nativeChatStream Step-3");
    const char* ut = env->GetStringUTFChars(userText_, nullptr);
    std::string prompt = build_chatml_prompt(ut ? std::string(ut) : std::string());
    env->ReleaseStringUTFChars(userText_, ut);

    // Prefill
    std::vector<llama_token> ptok = tokenize_text(prompt, /*add_special*/true, /*parse_special*/true);
    if (ptok.empty()) return;
    LOGI("Process nativeChatStream Step-4");
    g_stop = false;

    // 预填充
    {
        BatchBuf pre; pre.resize((int32_t)ptok.size());
        for (int32_t i = 0; i < (int32_t)ptok.size(); ++i) {
            pre.token[i]  = ptok[i];
            pre.pos[i]    = i;
            pre.logits[i] = 0; // 不取 logits
        }
        llama_batch b = pre.as_batch();
        if (llama_decode(g_ctx, b) != 0) {
            return;
        }
    }
    LOGI("Process nativeChatStream Step-5");
    const llama_token tok_eos = llama_vocab_eos(g_vocab);
    const int32_t n_vocab = vocab_size(g_vocab, g_ctx);

    // 逐 token 生成
    BatchBuf step; step.resize(1);
    int32_t cur_pos = (int32_t)ptok.size();
    const int32_t max_new = 512;

    for (int32_t i = 0; i < max_new && !g_stop.load(); ++i, ++cur_pos) {
        const float* logits = get_logits(g_ctx);
        if (!logits) break;

        llama_token next = greedy_argmax(logits, n_vocab);
        if (next == tok_eos) break;

        // ② 使用缓存的 midOnToken 发送（不会产生类引用泄漏）
        std::string piece = detok_piece(next);
        if (!piece.empty()) emit_utf8_safely(env, thiz, cbCls, midOnToken, piece);

        // 送回模型并请求输出 logits
        step.token[0]  = next;
        step.pos[0]    = cur_pos;
        step.logits[0] = 1;

        llama_batch b = step.as_batch();
        if (llama_decode(g_ctx, b) != 0) break;
    }
    flush_pending(env, thiz, cbCls, midOnToken);
    env->CallVoidMethod(thiz, midOnDone);
    env->DeleteLocalRef(cbCls);
    LOGI("Process nativeChatStream Step-6");
}

// ===== JNI: 一次性生成（作文）=====
extern "C" JNIEXPORT jstring JNICALL
Java_com_kingsun_plugins_llm_LLMPlugin_nativeGenerateOnce(JNIEnv* env, jobject /*thiz*/,
                                                          jstring prompt_, jint maxNew_) {
    if (!g_ctx || !g_model || !g_vocab) return env->NewStringUTF("");

    const char* p = env->GetStringUTFChars(prompt_, nullptr);
    std::string prompt = p ? std::string(p) : std::string();
    env->ReleaseStringUTFChars(prompt_, p);

    // Prefill
    std::vector<llama_token> ptok = tokenize_text(prompt, /*add_special*/true, /*parse_special*/true);
    if (ptok.empty()) return env->NewStringUTF("");

    {
        BatchBuf pre; pre.resize((int32_t)ptok.size());
        for (int32_t i = 0; i < (int32_t)ptok.size(); ++i) {
            pre.token[i]  = ptok[i];
            pre.pos[i]    = i;
            pre.logits[i] = 0;
        }
        llama_batch b = pre.as_batch();
        if (llama_decode(g_ctx, b) != 0) {
            return env->NewStringUTF("");
        }
    }

    const llama_token tok_eos = llama_vocab_eos(g_vocab);
    const int32_t n_vocab = vocab_size(g_vocab, g_ctx);
    const int32_t max_new = std::max(32, (int32_t)maxNew_);

    std::string out;
    BatchBuf step; step.resize(1);
    int32_t cur_pos = (int32_t)ptok.size();

    for (int32_t i = 0; i < max_new; ++i, ++cur_pos) {
        const float* logits = get_logits(g_ctx);
        if (!logits) break;

        llama_token next = greedy_argmax(logits, n_vocab);
        if (next == tok_eos) break;

        std::string piece = detok_piece(next);
        if (!piece.empty()) out.append(piece);

        step.token[0]  = next;
        step.pos[0]    = cur_pos;
        step.logits[0] = 1;
        llama_batch b = step.as_batch();
        if (llama_decode(g_ctx, b) != 0) break;
    }

    return env->NewStringUTF(out.c_str());
}
