// android/src/main/java/com/kingsun/plugins/llm/LLMPlugin.java
package com.kingsun.plugins.llm;

import android.content.Context;
import android.content.res.AssetManager;
import android.util.Log;
import com.getcapacitor.*;
import com.getcapacitor.annotation.CapacitorPlugin;
import java.io.*;
import java.net.HttpURLConnection;
import java.net.URL;
import java.security.DigestInputStream;
import java.security.MessageDigest;
import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import org.json.JSONArray;

@CapacitorPlugin(name = "LLM")
public class LLMPlugin extends Plugin {

    private static final String TAG = "LLMPlugin";

    private final ExecutorService worker = Executors.newSingleThreadExecutor();
    private volatile PluginCall streamingCall;

    /** 新：JNI 壳实例（用于接收 Native token 回调） */
    private LlamaNative core = new LlamaNative(
        new LlamaNative.Listener() {
            @Override
            public void onToken(String token) {
                JSObject ev = new JSObject().put("token", token);
                notifyListeners("llmToken", ev);
            }

            @Override
            public void onDone() {
                notifyListeners("llmDone", new JSObject());
                finishStreamingOk();
            }
        }
    );

    // ---------- @PluginMethod: init ----------
    @PluginMethod
    public void init(PluginCall call) {
        try {
            Context ctx = getContext();
            final String assetPath = call.getString("assetPath");
            final String expectedSha = call.getString("expectedSha256");
            final String explicitPath = call.getString("modelPath");
            final String remoteUrl = call.getString("remoteUrl");
            final int nCtx = call.getInt("nCtx", 1024);

            String modelPath = null;
            if (assetPath != null && !assetPath.isEmpty()) {
                String destName = assetPath.contains("/") ? assetPath.substring(assetPath.lastIndexOf('/') + 1) : assetPath;
                modelPath = ensureBundledModel(ctx, assetPath, destName, expectedSha);
            }
            if (modelPath == null && explicitPath != null && !explicitPath.isEmpty()) {
                File f = new File(explicitPath);
                if (f.exists() && f.isFile()) modelPath = f.getAbsolutePath();
            }
            if (modelPath == null && remoteUrl != null && !remoteUrl.isEmpty()) {
                File out = new File(getModelsDir(ctx), fileNameFromUrl(remoteUrl));
                if (!out.exists()) downloadTo(remoteUrl, out);
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
                call.reject("No model available.");
                return;
            }

            boolean ok = LlamaNative.nativeInit(modelPath, nCtx);
            if (ok) call.resolve();
            else call.reject("nativeInit failed");
        } catch (Exception e) {
            call.reject("init error: " + e.getMessage());
        }
    }

    // ---------- @PluginMethod: setSampling ----------
    @PluginMethod
    public void setSampling(PluginCall call) {
        try {
            float temp = (float) call.getFloat("temp", 0.8f);
            float topP = (float) call.getFloat("topP", 0.95f);
            int topK = call.getInt("topK", 40);
            float repeatPenalty = (float) call.getFloat("repeatPenalty", 1.10f);
            int repeatLastN = call.getInt("repeatLastN", 256);
            float minP = (float) call.getFloat("minP", 0.05f);
            LlamaNative.nativeSetSampling(temp, topP, topK, repeatPenalty, repeatLastN, minP);
            call.resolve();
        } catch (Throwable t) {
            call.reject("setSampling error: " + t.getMessage());
        }
    }

    // ---------- @PluginMethod: chat ----------
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
                core.nativeChatStream(prompt);
            } catch (Throwable t) {
                JSObject ev = new JSObject().put("message", "nativeChatStream error: " + t.getMessage());
                notifyListeners("llmError", ev);
                finishStreamingErr("native error: " + t.getMessage());
            }
        });
    }

    // ---------- @PluginMethod: stop/free ----------
    @PluginMethod
    public void stop(PluginCall call) {
        try {
            LlamaNative.nativeStop();
            call.resolve();
        } catch (Throwable t) {
            call.reject("stop error: " + t.getMessage());
        }
    }

    @PluginMethod
    public void free(PluginCall call) {
        try {
            LlamaNative.nativeFree();
            call.resolve();
        } catch (Throwable t) {
            call.reject("free error: " + t.getMessage());
        }
    }

    // ---------- @PluginMethod: generateEssay ----------
    @PluginMethod
    public void generateEssay(PluginCall call) {
        try {
            String title = call.getString("title", "An Essay");
            int wordLimit = call.getInt("word_limit", 200);
            String lang = call.getString("lang", "en");
            JSObject cons = call.getObject("constraints");

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
                    String text = core.nativeGenerateOnce(prompt, maxNew);
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

    // ---------- 生成中统一收尾 ----------
    private synchronized void finishStreamingOk() {
        if (streamingCall != null) {
            streamingCall.resolve();
            streamingCall = null;
        }
    }

    private synchronized void finishStreamingErr(String msg) {
        if (streamingCall != null) {
            streamingCall.reject(msg);
            streamingCall = null;
        }
    }

    // ---------- 工具：作文 prompt ----------
    private static String buildEssayPrompt(
        String title,
        int limit,
        String lang,
        java.util.List<String> hiErr,
        java.util.List<String> hiFreq
    ) {
        StringBuilder sb = new StringBuilder();
        sb.append("Write a ").append(lang).append(" essay.\n");
        sb.append("Title: ").append(title).append("\n");
        sb.append("Length: ~").append(Math.max(50, limit)).append(" words.\n");
        sb.append("Requirements:\n");
        sb.append("- Clear structure with introduction, body, and conclusion.\n");
        sb.append("- Use simple sentences suitable for ESL learners.\n");
        if (hiErr != null && !hiErr.isEmpty()) {
            sb.append("- Pay attention to commonly mistaken words: ");
            for (int i = 0; i < hiErr.size(); i++) sb.append(hiErr.get(i)).append(i + 1 == hiErr.size() ? ".\n" : ", ");
        }
        if (hiFreq != null && !hiFreq.isEmpty()) {
            sb.append("- Try to include high-frequency vocabulary: ");
            for (int i = 0; i < hiFreq.size(); i++) sb.append(hiFreq.get(i)).append(i + 1 == hiFreq.size() ? ".\n" : ", ");
        }
        sb.append("- Avoid overly complex grammar. Keep the vocabulary practical.\n");
        sb.append("Now produce only the final essay content.\n");
        return sb.toString();
    }

    // ---------- 资源/下载/校验（与你现有一致） ----------
    private static String ensureBundledModel(Context ctx, String assetRelativePath, String destFileName, String expectedSha256)
        throws Exception {
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
        try (
            InputStream in = new BufferedInputStream(ctx.getAssets().open(assetRelativePath, AssetManager.ACCESS_STREAMING));
            OutputStream out = new BufferedOutputStream(new FileOutputStream(tmp))
        ) {
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
            /* ignore */
        }
        if (!tmp.renameTo(dst)) {
            /*noinspection ResultOfMethodCallIgnored*/ tmp.delete();
            throw new IOException("rename tmp failed");
        }
        return dst.getAbsolutePath();
    }

    private static File getModelsDir(Context ctx) {
        File d = new File(ctx.getFilesDir(), "models");
        if (!d.exists()) d.mkdirs();
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
        try (InputStream in = new BufferedInputStream(new FileInputStream(f)); DigestInputStream din = new DigestInputStream(in, md)) {
            byte[] buf = new byte[1024 * 1024];
            while (din.read(buf) >= 0) {
                /* consume */
            }
        }
        byte[] d = md.digest();
        StringBuilder sb = new StringBuilder(d.length * 2);
        for (byte b : d) sb.append(String.format(Locale.ROOT, "%02x", b));
        return sb.toString();
    }
}
