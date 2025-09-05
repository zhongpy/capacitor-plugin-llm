// android/app/src/main/java/com/yourapp/plugins/llm/LlamaNative.java
package com.kingsun.plugins.llm;

public class LlamaNative {
  static { System.loadLibrary("llama_jni"); } // 这会间接加载 libllama.so

  public interface StreamCallback {
    void onToken(String token);
    void onDone();
  }

  public static native boolean init(String modelPath, int nCtx);
  public static native void free();
  public static native void stop();
  public static native void chatStream(String userText, StreamCallback cb);
}
