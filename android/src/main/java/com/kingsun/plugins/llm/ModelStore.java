package com.kingsun.plugins.llm;
import android.content.Context;
import android.content.res.AssetFileDescriptor;
import android.content.res.AssetManager;

import java.io.*;
import java.security.DigestInputStream;
import java.security.MessageDigest;
import java.util.Locale;

public final class ModelStore {

    private ModelStore() {}

    /**
     * 确保 assets/models/<assetName> 已复制到 filesDir/models/<destFileName>
     * @param expectedSha256 可为 null；若提供则校验不一致会强制重拷
     * @return 目标文件的绝对路径
     */
    public static String ensureBundledModel(Context ctx,
                                            String assetRelativePath,   // e.g. "models/Qwen3-0.6B-Instruct-q4_k_m.gguf"
                                            String destFileName,        // e.g. "Qwen3-0.6B-Instruct-q4_k_m.gguf"
                                            String expectedSha256) throws Exception {
        File modelsDir = new File(ctx.getFilesDir(), "models");
        if (!modelsDir.exists() && !modelsDir.mkdirs()) {
            throw new IOException("Failed to create models dir: " + modelsDir);
        }
        File dst = new File(modelsDir, destFileName);

        // 若已存在且（无校验要求或校验通过）直接返回
        if (dst.exists()) {
            if (expectedSha256 == null || expectedSha256.isEmpty()) {
                return dst.getAbsolutePath();
            }
            String got = sha256File(dst);
            if (equalsHex(expectedSha256, got)) return dst.getAbsolutePath();
            // 不一致则删除重拷
            //noinspection ResultOfMethodCallIgnored
            dst.delete();
        }

        // 原子写入：先写临时文件
        File tmp = File.createTempFile(destFileName + ".", ".part", modelsDir);

        AssetManager am = ctx.getAssets();
        try (InputStream in = openAsset(am, assetRelativePath);
             OutputStream out = new BufferedOutputStream(new FileOutputStream(tmp))) {
            byte[] buf = new byte[1024 * 1024];
            int n;
            while ((n = in.read(buf)) >= 0) {
                out.write(buf, 0, n);
            }
            out.flush();
        }

        // 可选校验
        if (expectedSha256 != null && !expectedSha256.isEmpty()) {
            String got = sha256File(tmp);
            if (!equalsHex(expectedSha256, got)) {
                //noinspection ResultOfMethodCallIgnored
                tmp.delete();
                throw new IOException("SHA256 mismatch for " + destFileName +
                        ", expected=" + expectedSha256 + ", got=" + got);
            }
        }

        // 原子替换
        if (dst.exists() && !dst.delete()) {
            //noinspection ResultOfMethodCallIgnored
            tmp.delete();
            throw new IOException("Failed to replace existing file: " + dst);
        }
        if (!tmp.renameTo(dst)) {
            //noinspection ResultOfMethodCallIgnored
            tmp.delete();
            throw new IOException("Failed to move tmp to dest");
        }

        return dst.getAbsolutePath();
    }

    /** 打开 asset，并尽量获取未压缩的长度（仅用于日志/可选优化，不必须） */
    private static InputStream openAsset(AssetManager am, String relPath) throws IOException {
        // 如果资源没被压缩（配了 noCompress），能拿到 length；否则 length 可能为 UNKNOWN
        // AssetFileDescriptor afd = am.openFd(relPath); // 压缩的 asset 调这个会抛异常
        return new BufferedInputStream(am.open(relPath, AssetManager.ACCESS_STREAMING));
    }

    private static String sha256File(File f) throws Exception {
        MessageDigest md = MessageDigest.getInstance("SHA-256");
        try (InputStream in = new BufferedInputStream(new FileInputStream(f));
             DigestInputStream din = new DigestInputStream(in, md)) {
            byte[] buf = new byte[1024 * 1024];
            while (din.read(buf) >= 0) { /* consume */ }
        }
        byte[] dig = md.digest();
        StringBuilder sb = new StringBuilder(dig.length * 2);
        for (byte b : dig) sb.append(String.format(Locale.ROOT, "%02x", b));
        return sb.toString();
    }

    private static boolean equalsHex(String a, String b) {
        return a != null && b != null && a.equalsIgnoreCase(b);
    }
}