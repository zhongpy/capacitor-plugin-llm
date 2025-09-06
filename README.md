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
* [`setSampling(...)`](#setsampling)
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

| Param         | Type                                                |
| ------------- | --------------------------------------------------- |
| **`options`** | <code><a href="#initoptions">InitOptions</a></code> |

**Returns:** <code>any</code>

--------------------


### chat(...)

```typescript
chat(options: ChatOptions) => any
```

| Param         | Type                                                |
| ------------- | --------------------------------------------------- |
| **`options`** | <code><a href="#chatoptions">ChatOptions</a></code> |

**Returns:** <code>any</code>

--------------------


### stop()

```typescript
stop() => any
```

**Returns:** <code>any</code>

--------------------


### free()

```typescript
free() => any
```

**Returns:** <code>any</code>

--------------------


### generateEssay(...)

```typescript
generateEssay(options: GenerateEssayOptions) => any
```

| Param         | Type                                                                  |
| ------------- | --------------------------------------------------------------------- |
| **`options`** | <code><a href="#generateessayoptions">GenerateEssayOptions</a></code> |

**Returns:** <code>any</code>

--------------------


### setSampling(...)

```typescript
setSampling(options: SetSamplingOptions) => any
```

新增：动态调采样参数（映射到 nativeSetSampling）

| Param         | Type                                                              |
| ------------- | ----------------------------------------------------------------- |
| **`options`** | <code><a href="#setsamplingoptions">SetSamplingOptions</a></code> |

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

| Prop                 | Type                |
| -------------------- | ------------------- |
| **`assetPath`**      | <code>string</code> |
| **`expectedSha256`** | <code>string</code> |
| **`modelPath`**      | <code>string</code> |
| **`remoteUrl`**      | <code>string</code> |
| **`nCtx`**           | <code>number</code> |


#### ChatOptions

| Prop         | Type                |
| ------------ | ------------------- |
| **`prompt`** | <code>string</code> |


#### GenerateEssayOptions

| Prop                 | Type                                                          |
| -------------------- | ------------------------------------------------------------- |
| **`title`**          | <code>string</code>                                           |
| **`word_limit`**     | <code>number</code>                                           |
| **`lang`**           | <code>string</code>                                           |
| **`constraints`**    | <code>{ high_error_words?: {}; high_freq_words?: {}; }</code> |
| **`max_new_tokens`** | <code>number</code>                                           |


#### SetSamplingOptions

| Prop                | Type                |
| ------------------- | ------------------- |
| **`temp`**          | <code>number</code> |
| **`topP`**          | <code>number</code> |
| **`topK`**          | <code>number</code> |
| **`repeatPenalty`** | <code>number</code> |
| **`repeatLastN`**   | <code>number</code> |
| **`minP`**          | <code>number</code> |


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
