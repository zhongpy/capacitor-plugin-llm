export interface LLMPlugin {
  echo(options: { value: string }): Promise<{ value: string }>;
}
