package com.kingsun.plugins.llm;

import android.content.Context;
import android.content.res.AssetManager;

import com.getcapacitor.*;

import com.getcapacitor.annotation.CapacitorPlugin;
import org.json.JSONArray;
import org.json.JSONObject;

import java.io.*;
import java.net.HttpURLConnection;
import java.net.URL;
import java.security.DigestInputStream;
import java.security.MessageDigest;
import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

@CapacitorPlugin(name = "LLM")
public class LLMPlugin extends Plugin {

  // ===== JNI 装载 & native 方法 =====
  static { System.loadLibrary("llama_jni"); }
  private static native boolean nativeInit(String modelPath, int nCtx);
  private static native void    nativeFree();
  private static native void    nativeStop();
  private native  void          nativeChatStream(String prompt);
  private native  String        nativeGenerateOnce(String prompt, int maxNewTokens);

  private final ExecutorService worker = Executors.newSingleThreadExecutor();
  private volatile PluginCall streamingCall;

  // ====== 对外：init ======
  // 参数示例：
  // {
  //   assetPath: "models/Qwen3-0.6B-Instruct-q4_k_m.gguf",  // 可选——优先从 assets 复制
  //   expectedSha256: "<hex>",                               // 可选，校验文件
  //   modelPath: "/abs/path/to/model.gguf",                  // 可选——已在本地
  //   remoteUrl: "https://.../model.gguf",                   // 可选——本地无则下载
  //   nCtx: 1024                                             // 可选
  // }
  @PluginMethod
  public void init(PluginCall call) {
    try {
      Context ctx = getContext();

      final String assetPath     = call.getString("assetPath");      // e.g. models/xxx.gguf
      final String expectedSha   = call.getString("expectedSha256");
      final String explicitPath  = call.getString("modelPath");
      final String remoteUrl     = call.getString("remoteUrl");
      final int    nCtx          = call.getInt("nCtx", 1024);

      // 1) 先尝试从 assets 复制（自带模型）
      String modelPath = null;
      if (assetPath != null && !assetPath.isEmpty()) {
        String destName = assetPath.contains("/") ? assetPath.substring(assetPath.lastIndexOf('/') + 1) : assetPath;
        modelPath = ensureBundledModel(ctx, assetPath, destName, expectedSha);
      }

      // 2) 其次，如果显式给了路径且存在，就用它
      if (modelPath == null && explicitPath != null && !explicitPath.isEmpty()) {
        File f = new File(explicitPath);
        if (f.exists() && f.isFile()) {
          modelPath = f.getAbsolutePath();
        }
      }

      // 3) 再次，如果还没有且提供了远程 URL，就下载到私有目录
      if (modelPath == null && remoteUrl != null && !remoteUrl.isEmpty()) {
        File out = new File(getModelsDir(ctx), fileNameFromUrl(remoteUrl));
        if (!out.exists()) {
          downloadTo(remoteUrl, out);
        }
        if (expectedSha != null && !expectedSha.isEmpty()) {
          String got = sha256File(out);
          if (!expectedSha.equalsIgnoreCase(got)) {
            //noinspection ResultOfMethodCallIgnored
            out.delete();
            throw new IOException("SHA256 mismatch: expected=" + expectedSha + " got=" + got);
          }
        }
        modelPath = out.getAbsolutePath();
      }

      if (modelPath == null) {
        call.reject("No model available. Provide assetPath or modelPath or remoteUrl.");
        return;
      }

      boolean ok = nativeInit(modelPath, nCtx);
      if (ok) call.resolve();
      else call.reject("nativeInit failed");

    } catch (Exception e) {
      call.reject("init error: " + e.getMessage());
    }
  }

  // ====== 对外：chat（流式）=====
  // 入参：{ prompt: "..." }
  // 事件：
  //   - llmToken: { token }
  //   - llmDone:  {}
  @PluginMethod
  public synchronized void chat(PluginCall call) {
    String prompt = call.getString("prompt", "");
    if (prompt.trim().isEmpty()) {
      call.reject("prompt required");
      return;
    }
    if (streamingCall != null) {
      call.reject("another chat is running");
      return;
    }
    streamingCall = call;
    worker.execute(() -> {
      try {
        nativeChatStream(prompt);
      } catch (Throwable t) {
        JSObject ev = new JSObject().put("message", "nativeChatStream error: " + t.getMessage());
        notifyListeners("llmError", ev);
        finishStreamingWithError("native error: " + t.getMessage());
      }
    });
  }

  // JNI 回调：逐 token
  public void onNativeToken(String token) {
    JSObject ev = new JSObject().put("token", token);
    notifyListeners("llmToken", ev);
  }

  // JNI 回调：完成
  public synchronized void onNativeDone() {
    notifyListeners("llmDone", new JSObject());
    if (streamingCall != null) {
      streamingCall.resolve();
      streamingCall = null;
    }
  }

  private synchronized void finishStreamingWithError(String msg) {
    if (streamingCall != null) {
      streamingCall.reject(msg);
      streamingCall = null;
    }
  }

  // ====== 对外：stop ======
  @PluginMethod
  public void stop(PluginCall call) {
    try {
      nativeStop();
      call.resolve();
    } catch (Throwable t) {
      call.reject("stop error: " + t.getMessage());
    }
  }

  // ====== 对外：free ======
  @PluginMethod
  public void free(PluginCall call) {
    try {
      nativeFree();
      call.resolve();
    } catch (Throwable t) {
      call.reject("free error: " + t.getMessage());
    }
  }

  // ====== 对外：作文生成（一次性）=====
  // 入参：
  // {
  //   title: "My Weekend",
  //   word_limit: 200,
  //   lang: "en",
  //   constraints: { high_error_words: ["its","it's"], high_freq_words: ["go","have","do"] },
  //   max_new_tokens: 512
  // }
  @PluginMethod
  public void generateEssay(PluginCall call) {
    try {
      String title = call.getString("title", "An Essay");
      int wordLimit = call.getInt("word_limit", 200);
      String lang = call.getString("lang", "en");
      JSObject cons = call.getObject("constraints");

      // 收集词表限制
      java.util.List<String> hiErr = new java.util.ArrayList<>();
      java.util.List<String> hiFreq = new java.util.ArrayList<>();
      if (cons != null) {
        JSONArray a = cons.getJSONArray("high_error_words");
        if (a != null) for (int i = 0; i < a.length(); i++) hiErr.add(a.getString(i));
        a = cons.getJSONArray("high_freq_words");
        if (a != null) for (int i = 0; i < a.length(); i++) hiFreq.add(a.getString(i));
      }

      String prompt = buildEssayPrompt(title, wordLimit, lang, hiErr, hiFreq);
      int maxNew = call.getInt("max_new_tokens", Math.max(256, wordLimit * 3));

      worker.execute(() -> {
        try {
          String text = nativeGenerateOnce(prompt, maxNew);
          JSObject ret = new JSObject().put("text", text);
          call.resolve(ret);
        } catch (Throwable t) {
          call.reject("generateEssay error: " + t.getMessage());
        }
      });

    } catch (Exception e) {
      call.reject("generateEssay error: " + e.getMessage());
    }
  }

  // ===== 作文 prompt（与 JNI 一致）=====
  private static String buildEssayPrompt(String title, int limit, String lang,
                                         java.util.List<String> hiErr,
                                         java.util.List<String> hiFreq) {
    StringBuilder sb = new StringBuilder();
    sb.append("Write a ").append(lang).append(" essay.\n");
    sb.append("Title: ").append(title).append("\n");
    sb.append("Length: ~").append(Math.max(50, limit)).append(" words.\n");
    sb.append("Requirements:\n");
    sb.append("- Clear structure with introduction, body, and conclusion.\n");
    sb.append("- Use simple sentences suitable for ESL learners.\n");
    if (hiErr != null && !hiErr.isEmpty()) {
      sb.append("- Pay attention to commonly mistaken words: ");
      for (int i = 0; i < hiErr.size(); i++) {
        sb.append(hiErr.get(i)).append(i + 1 == hiErr.size() ? ".\n" : ", ");
      }
    }
    if (hiFreq != null && !hiFreq.isEmpty()) {
      sb.append("- Try to include high-frequency vocabulary: ");
      for (int i = 0; i < hiFreq.size(); i++) {
        sb.append(hiFreq.get(i)).append(i + 1 == hiFreq.size() ? ".\n" : ", ");
      }
    }
    sb.append("- Avoid overly complex grammar. Keep the vocabulary practical.\n");
    sb.append("Now produce only the final essay content.\n");
    return sb.toString();
  }

  // ====== assets → filesDir/models 复制（只复制一次，带可选 SHA256 校验）=====
  private static String ensureBundledModel(Context ctx, String assetRelativePath,
                                           String destFileName, String expectedSha256) throws Exception {
    File modelsDir = getModelsDir(ctx);
    File dst = new File(modelsDir, destFileName);

    if (dst.exists()) {
      if (expectedSha256 == null || expectedSha256.isEmpty()) return dst.getAbsolutePath();
      String got = sha256File(dst);
      if (expectedSha256.equalsIgnoreCase(got)) return dst.getAbsolutePath();
      //noinspection ResultOfMethodCallIgnored
      dst.delete();
    }

    File tmp = File.createTempFile(destFileName + ".", ".part", modelsDir);
    try (InputStream in = new BufferedInputStream(ctx.getAssets().open(assetRelativePath, AssetManager.ACCESS_STREAMING));
         OutputStream out = new BufferedOutputStream(new FileOutputStream(tmp))) {
      byte[] buf = new byte[1024 * 1024];
      int n;
      while ((n = in.read(buf)) >= 0) out.write(buf, 0, n);
      out.flush();
    }

    if (expectedSha256 != null && !expectedSha256.isEmpty()) {
      String got = sha256File(tmp);
      if (!expectedSha256.equalsIgnoreCase(got)) {
        //noinspection ResultOfMethodCallIgnored
        tmp.delete();
        throw new IOException("SHA256 mismatch for " + destFileName + ", expected=" + expectedSha256 + ", got=" + got);
      }
    }

    if (dst.exists() && !dst.delete()) {
      //noinspection ResultOfMethodCallIgnored
      tmp.delete();
      throw new IOException("replace existing failed: " + dst);
    }
    if (!tmp.renameTo(dst)) {
      //noinspection ResultOfMethodCallIgnored
      tmp.delete();
      throw new IOException("rename tmp failed");
    }
    return dst.getAbsolutePath();
  }

  private static File getModelsDir(Context ctx) {
    File d = new File(ctx.getFilesDir(), "models");
    if (!d.exists()) //noinspection ResultOfMethodCallIgnored
      d.mkdirs();
    return d;
  }

  private static String fileNameFromUrl(String url) {
    int i = url.lastIndexOf('/');
    return (i >= 0 && i + 1 < url.length()) ? url.substring(i + 1) : "model.gguf";
  }

  private static void downloadTo(String urlStr, File out) throws Exception {
    out.getParentFile().mkdirs();
    HttpURLConnection conn = null;
    try (BufferedOutputStream bos = new BufferedOutputStream(new FileOutputStream(out))) {
      URL url = new URL(urlStr);
      conn = (HttpURLConnection) url.openConnection();
      conn.setConnectTimeout(20000);
      conn.setReadTimeout(600000);
      conn.connect();
      if (conn.getResponseCode() != 200) throw new IOException("HTTP " + conn.getResponseCode());
      try (BufferedInputStream in = new BufferedInputStream(conn.getInputStream())) {
        byte[] buf = new byte[1024 * 1024];
        int n;
        while ((n = in.read(buf)) >= 0) bos.write(buf, 0, n);
      }
      bos.flush();
    } finally {
      if (conn != null) conn.disconnect();
    }
  }

  private static String sha256File(File f) throws Exception {
    MessageDigest md = MessageDigest.getInstance("SHA-256");
    try (InputStream in = new BufferedInputStream(new FileInputStream(f));
         DigestInputStream din = new DigestInputStream(in, md)) {
      byte[] buf = new byte[1024 * 1024];
      while (din.read(buf) >= 0) { /* consume */ }
    }
    byte[] d = md.digest();
    StringBuilder sb = new StringBuilder(d.length * 2);
    for (byte b : d) sb.append(String.format(Locale.ROOT, "%02x", b));
    return sb.toString();
  }
}
