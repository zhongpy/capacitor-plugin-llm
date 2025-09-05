# capacitor-plugin-llm

This plugin is an engine of llm, It's used to generate article or chat with user.

## Install

```bash
npm install capacitor-plugin-llm
npx cap sync
```

## API

<docgen-index>

* [`init(...)`](#init)
* [`chat(...)`](#chat)
* [`stop()`](#stop)
* [`free()`](#free)
* [`generateEssay(...)`](#generateessay)
* [`addListener('llmToken', ...)`](#addlistenerllmtoken-)
* [`addListener('llmDone', ...)`](#addlistenerllmdone-)
* [`addListener('llmError', ...)`](#addlistenerllmerror-)
* [Interfaces](#interfaces)
* [Type Aliases](#type-aliases)

</docgen-index>

<docgen-api>
<!--Update the source file JSDoc comments and rerun docgen to update the docs below-->

### init(...)

```typescript
init(options: InitOptions) => any
```

初始化（复制/下载模型 + nativeInit）

| Param         | Type                                                |
| ------------- | --------------------------------------------------- |
| **`options`** | <code><a href="#initoptions">InitOptions</a></code> |

**Returns:** <code>any</code>

--------------------


### chat(...)

```typescript
chat(options: ChatOptions) => any
```

开始流式对话（逐 token 触发 llmToken，结束触发 llmDone）

| Param         | Type                                                |
| ------------- | --------------------------------------------------- |
| **`options`** | <code><a href="#chatoptions">ChatOptions</a></code> |

**Returns:** <code>any</code>

--------------------


### stop()

```typescript
stop() => any
```

尝试中断当前流式生成

**Returns:** <code>any</code>

--------------------


### free()

```typescript
free() => any
```

释放底层资源（模型/上下文）

**Returns:** <code>any</code>

--------------------


### generateEssay(...)

```typescript
generateEssay(options: GenerateEssayOptions) => any
```

一次性生成（作文）

| Param         | Type                                                                  |
| ------------- | --------------------------------------------------------------------- |
| **`options`** | <code><a href="#generateessayoptions">GenerateEssayOptions</a></code> |

**Returns:** <code>any</code>

--------------------


### addListener('llmToken', ...)

```typescript
addListener(eventName: 'llmToken', listenerFunc: (event: LLMTokenEvent) => void) => any
```

| Param              | Type                                                                        |
| ------------------ | --------------------------------------------------------------------------- |
| **`eventName`**    | <code>'llmToken'</code>                                                     |
| **`listenerFunc`** | <code>(event: <a href="#llmtokenevent">LLMTokenEvent</a>) =&gt; void</code> |

**Returns:** <code>any</code>

--------------------


### addListener('llmDone', ...)

```typescript
addListener(eventName: 'llmDone', listenerFunc: (event: any) => void) => any
```

| Param              | Type                                 |
| ------------------ | ------------------------------------ |
| **`eventName`**    | <code>'llmDone'</code>               |
| **`listenerFunc`** | <code>(event: any) =&gt; void</code> |

**Returns:** <code>any</code>

--------------------


### addListener('llmError', ...)

```typescript
addListener(eventName: 'llmError', listenerFunc: (event: LLMErrorEvent) => void) => any
```

| Param              | Type                                                                        |
| ------------------ | --------------------------------------------------------------------------- |
| **`eventName`**    | <code>'llmError'</code>                                                     |
| **`listenerFunc`** | <code>(event: <a href="#llmerrorevent">LLMErrorEvent</a>) =&gt; void</code> |

**Returns:** <code>any</code>

--------------------


### Interfaces


#### InitOptions

| Prop                 | Type                | Description                                             |
| -------------------- | ------------------- | ------------------------------------------------------- |
| **`assetPath`**      | <code>string</code> | 优先从 assets 复制（例：models/Qwen3-0.6B-Instruct-q4_k_m.gguf） |
| **`expectedSha256`** | <code>string</code> | 可选的 SHA256 校验（hex 小写/大写都可）                              |
| **`modelPath`**      | <code>string</code> | 已存在的绝对路径（若提供优先生效）                                       |
| **`remoteUrl`**      | <code>string</code> | 本地没有则从该 URL 下载到 filesDir/models/                        |
| **`nCtx`**           | <code>number</code> | 上下文长度（默认 1024）                                          |


#### ChatOptions

| Prop         | Type                | Description          |
| ------------ | ------------------- | -------------------- |
| **`prompt`** | <code>string</code> | 用户提示词（会包上 ChatML 模板） |


#### GenerateEssayOptions

| Prop                 | Type                                                          | Description                             |
| -------------------- | ------------------------------------------------------------- | --------------------------------------- |
| **`title`**          | <code>string</code>                                           | 作文标题                                    |
| **`word_limit`**     | <code>number</code>                                           | 目标字数（近似）                                |
| **`lang`**           | <code>string</code>                                           | 语言（默认 en）                               |
| **`constraints`**    | <code>{ high_error_words?: {}; high_freq_words?: {}; }</code> | 约束条件                                    |
| **`max_new_tokens`** | <code>number</code>                                           | 最大新生成 tokens（默认 max(256, word_limit*3)） |


#### PluginListenerHandle

| Prop         | Type                      |
| ------------ | ------------------------- |
| **`remove`** | <code>() =&gt; any</code> |


### Type Aliases


#### LLMTokenEvent

<code>{ token: string }</code>


#### LLMDoneEvent

<code>Record&lt;string, never&gt;</code>


#### LLMErrorEvent

<code>{ message: string }</code>

</docgen-api>
