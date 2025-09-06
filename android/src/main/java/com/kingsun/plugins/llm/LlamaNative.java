// android/src/main/java/com/kingsun/plugins/llm/LlamaNative.java
package com.kingsun.plugins.llm;

public class LlamaNative {

    static {
        System.loadLibrary("llama_jni");
    }

    // ---- 静态 native（与上下文/模型相关） ----
    public static native boolean nativeInit(String modelPath, int nCtx);

    public static native void nativeFree();

    public static native void nativeStop();

    public static native void nativeSetSampling(float temp, float topP, int topK, float repeatPenalty, int repeatLastN, float minP);

    // 可选：构作文 prompt 的 native 辅助（若在 C++ 里实现了）
    public static native String nativeBuildEssayPrompt(String title, int wordLimit, String lang, String[] hiErr, String[] hiFreq);

    // ---- 实例 native（需要回调到该实例的 onNativeToken/onNativeDone） ----
    public native void nativeChatStream(String prompt);

    public native String nativeGenerateOnce(String prompt, int maxNewTokens);

    // ---- 回调桥 ----
    public interface Listener {
        void onToken(String token);
        void onDone();
    }

    private Listener listener;

    public LlamaNative(Listener l) {
        this.listener = l;
    }

    // 供 JNI 回调（名字与签名必须与 C++ 里的 GetMethodID 一致）
    public void onNativeToken(String token) {
        if (listener != null) listener.onToken(token);
    }

    public void onNativeDone() {
        if (listener != null) listener.onDone();
    }
}
