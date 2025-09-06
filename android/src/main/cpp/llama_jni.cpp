#include <jni.h>
#include <atomic>
#include <string>
#include <vector>
#include <cstdint>
#include <algorithm>
#include <limits>
#include <memory>
#include <mutex>
#include <unistd.h> // sysconf

#include "llama.h"
#include <android/log.h>

#define LOG_TAG "MyNativeModule"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  LOG_TAG, __VA_ARGS__)

// ===== 全局 =====
static llama_model*       g_model   = nullptr;
static llama_context*     g_ctx     = nullptr;
static const llama_vocab* g_vocab   = nullptr;
static std::atomic<bool>  g_stop{false};
static std::mutex         g_mutex;

static llama_context_params g_cparams{};  // 记住最近一次 init 的 cparams，便于重建上下文

struct SamplerParams {
    float temp           = 0.8f;
    float top_p          = 0.95f;
    int   top_k          = 40;
    float repeat_penalty = 1.10f;
    int   repeat_last_n  = 256;
    float min_p          = 0.05f;
    size_t min_keep      = 1;   // 给 top_p/min_p 用
};
static SamplerParams g_samp;     // 由 nativeSetSampling() 动态修改

// ===== ChatML（Qwen3 风格）=====
static std::string build_chatml_prompt(const std::string& user) {
    std::string s;
    s += "<|im_start|>system\nYou are a helpful assistant.<|im_end|>\n";
    s += "<|im_start|>user\n" + user + "<|im_end|>\n";
    s += "<|im_start|>assistant\n";
    return s;
}

// ===== 作文 prompt（保留）=====
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
    return s;
}

// ===== UTF-8 安全拼接 =====
static std::string g_pending_utf8;

// ========== 新增：UTF-8 -> UTF-16 安全解码器（只解到“完整码点”为止”） ==========
static size_t utf8_decode_to_utf16_partial(const std::string &in,
                                           std::u16string &out_u16) {
    out_u16.clear();
    const uint8_t *p = (const uint8_t *)in.data();
    size_t i = 0, n = in.size();

    auto emit_u16 = [&](uint32_t cp) {
        if (cp <= 0xFFFF) {
            // 避免直接落入代理区
            if (cp >= 0xD800 && cp <= 0xDFFF) cp = 0xFFFD;
            out_u16.push_back((char16_t)cp);
        } else if (cp <= 0x10FFFF) {
            cp -= 0x10000;
            char16_t hi = (char16_t)(0xD800 + (cp >> 10));
            char16_t lo = (char16_t)(0xDC00 + (cp & 0x3FF));
            out_u16.push_back(hi);
            out_u16.push_back(lo);
        } else {
            out_u16.push_back((char16_t)0xFFFD);
        }
    };

    while (i < n) {
        uint8_t b0 = p[i];

        if (b0 < 0x80) {
            // ASCII
            emit_u16(b0);
            ++i;
        } else if ((b0 & 0xE0) == 0xC0) {
            if (i + 1 >= n) break; // 不完整
            uint8_t b1 = p[i + 1];
            if ((b1 & 0xC0) != 0x80) { emit_u16(0xFFFD); ++i; continue; }
            uint32_t cp = ((b0 & 0x1F) << 6) | (b1 & 0x3F);
            // 过短（Overlong）修正
            if (cp < 0x80) cp = 0xFFFD;
            emit_u16(cp);
            i += 2;
        } else if ((b0 & 0xF0) == 0xE0) {
            if (i + 2 >= n) break;
            uint8_t b1 = p[i + 1], b2 = p[i + 2];
            if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80) { emit_u16(0xFFFD); ++i; continue; }
            uint32_t cp = ((b0 & 0x0F) << 12) | ((b1 & 0x3F) << 6) | (b2 & 0x3F);
            // 过短或代理区
            if (cp < 0x800) cp = 0xFFFD;
            emit_u16(cp);
            i += 3;
        } else if ((b0 & 0xF8) == 0xF0) {
            if (i + 3 >= n) break;
            uint8_t b1 = p[i + 1], b2 = p[i + 2], b3 = p[i + 3];
            if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80 || (b3 & 0xC0) != 0x80) { emit_u16(0xFFFD); ++i; continue; }
            uint32_t cp = ((b0 & 0x07) << 18) | ((b1 & 0x3F) << 12) | ((b2 & 0x3F) << 6) | (b3 & 0x3F);
            // 合法范围
            if (cp < 0x10000 || cp > 0x10FFFF) cp = 0xFFFD;
            emit_u16(cp);
            i += 4;
        } else {
            // 非法起始字节，跳过 1
            emit_u16(0xFFFD);
            ++i;
        }
    }
    // 返回“已安全消费”的字节数（剩余的是不完整尾巴，留给下一次）
    return i;
}

// ========== 修改：用 UTF-16 发给 Java（避免 NewStringUTF 的 Modified-UTF8 限制） ==========
static void emit_utf8_safely(JNIEnv* env, jobject cb, jclass /*cbCls*/, jmethodID midOnToken,
                             const std::string& chunk) {
    // 累积 UTF-8
    g_pending_utf8.append(chunk);

    // 尝试解码尽可能多的“完整码点”
    std::u16string u16;
    size_t consumed = utf8_decode_to_utf16_partial(g_pending_utf8, u16);
    if (consumed == 0 || u16.empty()) {
        // 还没有完整码点，等下次
        return;
    }

    // 从 pending 中移除已消费字节
    g_pending_utf8.erase(0, consumed);

    // 通过 UTF-16 构建 Java 字符串
    jstring jtok = env->NewString(reinterpret_cast<const jchar*>(u16.data()),
                                  static_cast<jsize>(u16.size()));
    if (!jtok) return;
    env->CallVoidMethod(cb, midOnToken, jtok);
    env->DeleteLocalRef(jtok);
}

static void flush_pending(JNIEnv* env, jobject cb, jclass /*cbCls*/, jmethodID midOnToken) {
    if (g_pending_utf8.empty()) return;

    std::u16string u16;
    size_t consumed = utf8_decode_to_utf16_partial(g_pending_utf8, u16);

    if (consumed > 0 && !u16.empty()) {
        jstring jtok = env->NewString(reinterpret_cast<const jchar*>(u16.data()),
                                      static_cast<jsize>(u16.size()));
        if (jtok) {
            env->CallVoidMethod(cb, midOnToken, jtok);
            env->DeleteLocalRef(jtok);
        }
        g_pending_utf8.erase(0, consumed);
    }

    // 若仍有残留（不完整尾巴），为了输出对齐，可以在结束时丢弃或补 U+FFFD：
    if (!g_pending_utf8.empty()) {
        // 方案 A：丢弃
        // g_pending_utf8.clear();

        // 方案 B：输出一个 U+FFFD 表示替代字符
        std::u16string repl(1, (char16_t)0xFFFD);
        jstring jtok2 = env->NewString(reinterpret_cast<const jchar*>(repl.data()), 1);
        if (jtok2) {
            env->CallVoidMethod(cb, midOnToken, jtok2);
            env->DeleteLocalRef(jtok2);
        }
        g_pending_utf8.clear();
    }
}

// ===== API 轻封装 =====
static inline const float* get_logits(llama_context* ctx) {
    return llama_get_logits(ctx);             // 兼容面最广
}
static inline int32_t vocab_size(const llama_vocab* vocab) {
    return llama_vocab_n_tokens(vocab);
}
static inline llama_token tok_eos(const llama_vocab* vocab) {
    return llama_vocab_eos(vocab);
}
static inline int token_to_piece(const llama_vocab* vocab, llama_token t, char* buf, int n) {
    return llama_token_to_piece(vocab, t, buf, n, 0, false);
}
static inline llama_token greedy_argmax(const float* logits, int32_t n_vocab) {
    int32_t best = 0;
    float bests = -std::numeric_limits<float>::infinity();
    for (int32_t i = 0; i < n_vocab; ++i) {
        if (logits[i] > bests) {
            bests = logits[i]; best = i;
        }
    }
    return (llama_token)best;
}

static std::vector<llama_token> tokenize_text(const std::string& text, bool add_special, bool parse_special) {
    int32_t need = llama_tokenize(g_vocab, text.c_str(), (int32_t)text.size(), nullptr, 0, add_special, parse_special);
    if (need < 0) need = -need;
    std::vector<llama_token> out(need);
    int32_t n = llama_tokenize(g_vocab, text.c_str(), (int32_t)text.size(), out.data(), (int32_t)out.size(), add_special, parse_special);
    if (n < 0) out.clear(); else out.resize(n);
    return out;
}
static std::string detok_piece(llama_token t) {
    char buf[256];
    int m = token_to_piece(g_vocab, t, buf, (int)sizeof(buf));
    if (m <= 0) return {};
    return std::string(buf, buf + m);
}

// ===== 上下文保护/重置 =====
static void rebuild_context_if_needed() {
    // 重建上下文（当无法清 KV 时的兜底）
    if (g_ctx) { llama_free(g_ctx); g_ctx = nullptr; }
    g_ctx = llama_init_from_model(g_model, g_cparams);
}

static void reset_session() {
#if defined(LLAMA_SUPPORTS_KV_CACHE_CLEAR) || defined(LLAMA_KV_CACHE_CLEAR)
    llama_kv_cache_clear(g_ctx);
#else
    // 没有 kv_clear/seq_rm，就重建上下文
    rebuild_context_if_needed();
#endif
}

// 适配上下文长度，预留余量
static std::vector<llama_token> fit_to_context(const std::vector<llama_token>& in, int32_t n_ctx_capacity) {
    const int32_t reserve = 32;
    if ((int32_t)in.size() <= n_ctx_capacity - reserve) return in;
    int32_t keep = std::max(0, n_ctx_capacity - reserve);
    std::vector<llama_token> out;
    if (keep > 0) out.insert(out.end(), in.end() - keep, in.end());
    return out;
}
static std::unique_ptr<llama_sampler, void(*)(llama_sampler*)> g_sampler{nullptr, llama_sampler_free};
// ===== 采样器链（新版签名）=====
struct SamplerDeleter { void operator()(llama_sampler* s) const { if (s) llama_sampler_free(s); } };

static void rebuild_sampler_chain() {
    if (!g_model) return;
    llama_sampler *chain = llama_sampler_chain_init(llama_sampler_chain_default_params());

    llama_sampler_chain_add(chain, llama_sampler_init_penalties(g_samp.repeat_last_n, g_samp.repeat_penalty, 0.0f, 0.0f));

    if (g_samp.top_k > 0) {
        llama_sampler_chain_add(chain, llama_sampler_init_top_k(g_samp.top_k));
    }
    if (g_samp.min_p > 0.0f) {
        llama_sampler_chain_add(chain, llama_sampler_init_min_p(g_samp.min_p, g_samp.min_keep));
    }
    if (g_samp.top_p > 0.0f && g_samp.top_p < 1.0f) {
        llama_sampler_chain_add(chain, llama_sampler_init_top_p(g_samp.top_p, g_samp.min_keep));
    }
    if (g_samp.temp > 0.0f) {
        llama_sampler_chain_add(chain, llama_sampler_init_temp(g_samp.temp));
    } else {
        llama_sampler_chain_add(chain, llama_sampler_init_greedy());
    }
    llama_sampler_chain_add(chain, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));
    g_sampler.reset(chain);
}

static llama_token sample_next_token(llama_context* ctx, llama_sampler* smpl) {
    if (!ctx || !smpl) return LLAMA_TOKEN_NULL;

    // 直接用“最后一步 logits”：idx = -1（符合最新版头文件示例）
    // 该调用内部会执行：apply + select（并不会自动 accept）
    llama_token id = llama_sampler_sample(smpl, ctx, -1);  // <-- 关键

    // 选择成功后要 accept，更新内部状态（比如重复惩罚、grammar 等）
    if (id != LLAMA_TOKEN_NULL) {
        llama_sampler_accept(smpl, id);
    }
    return id;
}

// ===== 通用 prefill =====
struct BatchBuf {
    std::vector<llama_token>   token;
    std::vector<llama_pos>     pos;
    std::vector<int32_t>       n_seq_id;
    std::vector<llama_seq_id>  seq_id_store;
    std::vector<llama_seq_id*> seq_id_ptrs;
    std::vector<int8_t>        logits;

    void resize(int n) {
        token.resize(n); pos.resize(n);
        n_seq_id.assign(n, 1);
        seq_id_store.assign(n, 0);
        seq_id_ptrs.resize(n);
        logits.assign(n, 0);
        for (int i = 0; i < n; ++i) seq_id_ptrs[i] = &seq_id_store[i];
    }
    llama_batch as_batch() {
        llama_batch b{};
        b.n_tokens = (int)token.size();
        b.token    = token.data();
        b.embd     = nullptr;
        b.pos      = pos.data();
        b.n_seq_id = n_seq_id.data();
        b.seq_id   = seq_id_ptrs.data();
        b.logits   = logits.data();
        return b;
    }
};

static bool prefill_tokens(const std::vector<llama_token>& ptok, int32_t& cur_pos, bool want_logits) {
    if (ptok.empty()) return false;
    BatchBuf pre; pre.resize((int)ptok.size());
    for (int i = 0; i < (int)ptok.size(); ++i) {
        pre.token[i]  = ptok[i];
        pre.pos[i]    = i;
        pre.logits[i] = (i + 1 == (int)ptok.size()) ? (want_logits ? 1 : 0) : 0;
    }
    if (llama_decode(g_ctx, pre.as_batch()) != 0) {
        LOGE("prefill decode failed");
        return false;
    }
    cur_pos = (int)ptok.size();
    return true;
}

// ===== JNI: init =====
extern "C" JNIEXPORT jboolean JNICALL
Java_com_kingsun_plugins_llm_LlamaNative_nativeInit(JNIEnv* env, jclass, jstring modelPath_, jint nCtx) {
    std::lock_guard<std::mutex> lk(g_mutex);

    const char* p = env->GetStringUTFChars(modelPath_, nullptr);
    std::string path = p ? p : "";
    env->ReleaseStringUTFChars(modelPath_, p);

    if (g_ctx) { llama_free(g_ctx); g_ctx = nullptr; }
    if (g_model) { llama_model_free(g_model); g_model = nullptr; }
    g_vocab = nullptr;
    g_pending_utf8.clear();

    llama_backend_init();

    llama_model_params mparams = llama_model_default_params();
    mparams.use_mmap  = true;
    mparams.use_mlock = false;

    g_model = llama_model_load_from_file(path.c_str(), mparams);
    if (!g_model) { LOGE("load model failed"); llama_backend_free(); return JNI_FALSE; }

    g_vocab = llama_model_get_vocab(g_model);
    if (!g_vocab) { LOGE("get vocab failed"); llama_model_free(g_model); g_model=nullptr; llama_backend_free(); return JNI_FALSE; }

    g_cparams = llama_context_default_params();
    g_cparams.n_ctx    = (nCtx > 0 ? nCtx : 2048);
    g_cparams.type_k   = GGML_TYPE_Q8_0;
    g_cparams.type_v   = GGML_TYPE_Q8_0;
    int ncpu = std::max(2, (int)sysconf(_SC_NPROCESSORS_ONLN) - 1);
    g_cparams.n_threads = ncpu;

    g_ctx = llama_init_from_model(g_model, g_cparams);
    if (!g_ctx) {
        LOGE("new context failed");
        llama_model_free(g_model); g_model=nullptr; g_vocab=nullptr;
        llama_backend_free();
        return JNI_FALSE;
    }

    LOGI("nativeInit OK n_ctx=%d threads=%d", g_cparams.n_ctx, g_cparams.n_threads);
    return JNI_TRUE;
}

// ===== JNI: free =====
extern "C" JNIEXPORT void JNICALL
Java_com_kingsun_plugins_llm_LlamaNative_nativeFree(JNIEnv*, jclass) {
    std::lock_guard<std::mutex> lk(g_mutex);
    if (g_ctx)   { llama_free(g_ctx); g_ctx = nullptr; }
    if (g_model) { llama_model_free(g_model); g_model = nullptr; }
    g_vocab = nullptr;
    g_pending_utf8.clear();
    llama_backend_free();
}

// ===== JNI: stop =====
extern "C" JNIEXPORT void JNICALL
Java_com_kingsun_plugins_llm_LlamaNative_nativeStop(JNIEnv*, jclass) {
    g_stop.store(true, std::memory_order_relaxed);
}

// ===== JNI: set sampling =====
extern "C" JNIEXPORT void JNICALL
Java_com_kingsun_plugins_llm_LlamaNative_nativeSetSampling(JNIEnv*, jclass,
                                                         jfloat temp, jfloat topP, jint topK, jfloat repeatPenalty, jint repeatLastN, jfloat minP) {
    std::lock_guard<std::mutex> lk(g_mutex);
    g_samp.temp           = std::max(0.f, (float)temp);
    g_samp.top_p          = std::clamp((float)topP, 0.f, 1.f);
    g_samp.top_k          = std::max(0, (int)topK);
    g_samp.repeat_penalty = std::max(0.0f, (float)repeatPenalty);
    g_samp.repeat_last_n  = std::max(0, (int)repeatLastN);
    g_samp.min_p          = std::clamp((float)minP, 0.f, 1.f);
    // min_keep 保持 1，避免采样坍缩
    g_samp.min_keep       = 1;
}

// ===== JNI: chat 流式 =====
extern "C" JNIEXPORT void JNICALL
Java_com_kingsun_plugins_llm_LlamaNative_nativeChatStream(JNIEnv* env, jobject thiz, jstring userText_) {
    std::lock_guard<std::mutex> lk(g_mutex);
    if (!g_ctx || !g_model || !g_vocab) { LOGE("not initialized"); return; }

    jclass cbCls = env->GetObjectClass(thiz);
    if (!cbCls) return;
    jmethodID midOnToken = env->GetMethodID(cbCls, "onNativeToken", "(Ljava/lang/String;)V");
    jmethodID midOnDone  = env->GetMethodID(cbCls, "onNativeDone",  "()V");
    if (!midOnToken || !midOnDone) { env->DeleteLocalRef(cbCls); return; }

    const char* ut = env->GetStringUTFChars(userText_, nullptr);
    std::string user = ut ? ut : "";
    env->ReleaseStringUTFChars(userText_, ut);

    std::string prompt = build_chatml_prompt(user);

    // 清 session（没有 KV 清理 API 就重建上下文）
    reset_session();
    g_pending_utf8.clear();
    g_stop.store(false, std::memory_order_relaxed);

    auto ptok = tokenize_text(prompt, true, true);
    ptok = fit_to_context(ptok, llama_n_ctx(g_ctx));

    int32_t cur_pos = 0;
    if (!prefill_tokens(ptok, cur_pos, true)) {
        env->CallVoidMethod(thiz, midOnDone);
        env->DeleteLocalRef(cbCls);
        return;
    }

    if (!g_sampler) {
        rebuild_sampler_chain();
    }
    BatchBuf step; step.resize(1);
    const int32_t max_new = 512;

    for (int i = 0; i < max_new && !g_stop.load(std::memory_order_relaxed); ++i, ++cur_pos) {
        llama_token next = sample_next_token(g_ctx, g_sampler.get());
        if (next == LLAMA_TOKEN_NULL) {
            LOGW("sampler returned NULL token, fallback to greedy or stop");
            // 可选：fallback
            const float *logits = get_logits(g_ctx);
            if (logits) {
                next = greedy_argmax(logits, vocab_size(g_vocab));
            }
            if (next == LLAMA_TOKEN_NULL) break;
        }
        if (next == tok_eos(g_vocab)) break;
        std::string piece = detok_piece(next);
        if (!piece.empty()) emit_utf8_safely(env, thiz, cbCls, midOnToken, piece);
        step.token[0]  = next;
        step.pos[0]    = cur_pos;
        step.logits[0] = 1;
        if (llama_decode(g_ctx, step.as_batch()) != 0) break;
    }

    flush_pending(env, thiz, cbCls, midOnToken);
    env->CallVoidMethod(thiz, midOnDone);
    env->DeleteLocalRef(cbCls);
}

// ===== JNI: 一次性生成 =====
extern "C" JNIEXPORT jstring JNICALL
Java_com_kingsun_plugins_llm_LlamaNative_nativeGenerateOnce(JNIEnv* env, jobject,
                                                          jstring prompt_, jint maxNew_) {
    std::lock_guard<std::mutex> lk(g_mutex);
    if (!g_ctx || !g_model || !g_vocab) return env->NewStringUTF("");

    const char* p = env->GetStringUTFChars(prompt_, nullptr);
    std::string prompt = p ? p : "";
    env->ReleaseStringUTFChars(prompt_, p);

    reset_session();
    g_pending_utf8.clear();
    g_stop.store(false, std::memory_order_relaxed);

    auto ptok = tokenize_text(prompt, true, true);
    ptok = fit_to_context(ptok, llama_n_ctx(g_ctx));

    int32_t cur_pos = 0;
    if (!prefill_tokens(ptok, cur_pos, true)) return env->NewStringUTF("");

    rebuild_sampler_chain();
    std::string out;
    BatchBuf step; step.resize(1);
    int32_t max_new = std::max(32, (int32_t)maxNew_);

    for (int i = 0; i < max_new && !g_stop.load(std::memory_order_relaxed); ++i, ++cur_pos) {
        llama_token next = sample_next_token(g_ctx, g_sampler.get());
        if (next == LLAMA_TOKEN_NULL) {
            LOGW("sampler returned NULL token, fallback to greedy or stop");
            // 可选：fallback
            const float *logits = get_logits(g_ctx);
            if (logits) {
                next = greedy_argmax(logits, vocab_size(g_vocab));
            }
            if (next == LLAMA_TOKEN_NULL) break;
        }
        if (next == tok_eos(g_vocab)) break;

        std::string piece = detok_piece(next);
        if (!piece.empty()) out.append(piece);

        step.token[0]  = next;
        step.pos[0]    = cur_pos;
        step.logits[0] = 1;
        if (llama_decode(g_ctx, step.as_batch()) != 0) break;
    }
    return env->NewStringUTF(out.c_str());
}

// ===== （可选）构造作文 Prompt =====
extern "C" JNIEXPORT jstring JNICALL
Java_com_kingsun_plugins_llm_LlamaNative_nativeBuildEssayPrompt(JNIEnv* env, jclass,
                                                              jstring jTitle, jint jWordLimit,
                                                              jstring jLang, jobjectArray jHiErr,
                                                              jobjectArray jHiFreq) {
    const char* ctitle = env->GetStringUTFChars(jTitle, nullptr);
    const char* clang  = env->GetStringUTFChars(jLang,  nullptr);
    std::string title = ctitle ? ctitle : "";
    std::string lang  = clang  ? clang  : "English";
    env->ReleaseStringUTFChars(jTitle, ctitle);
    env->ReleaseStringUTFChars(jLang,  clang);

    auto to_vec = [&](jobjectArray arr)->std::vector<std::string>{
        std::vector<std::string> v;
        if (!arr) return v;
        jsize n = env->GetArrayLength(arr);
        v.reserve(std::max<jsize>(n, 0));
        for (jsize i = 0; i < n; ++i) {
            jstring s = (jstring)env->GetObjectArrayElement(arr, i);
            const char* cs = env->GetStringUTFChars(s, nullptr);
            v.emplace_back(cs ? cs : "");
            env->ReleaseStringUTFChars(s, cs);
            env->DeleteLocalRef(s);
        }
        return v;
    };
    auto hi_err  = to_vec(jHiErr);
    auto hi_freq = to_vec(jHiFreq);

    std::string prompt = build_essay_prompt(title, (int)jWordLimit, lang, hi_err, hi_freq);
    return env->NewStringUTF(prompt.c_str());
}
