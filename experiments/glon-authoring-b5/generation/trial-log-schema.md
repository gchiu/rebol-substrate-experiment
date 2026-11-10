# Trial log fields

Each trial should record one row per task/representation:

`task, representation, model, model_version, configuration, temperature,
token_budget, prompt_bytes, docs_bytes, initial_correct, repair_attempts,
final_correct, input_tokens, output_tokens, cost, generation_seconds,
compile_seconds, execution_seconds, authoring_bytes, binary_bytes, binary_words, final_tokens,
compile_failures, runtime_failures, failure_category, notes`

`final_tokens` means representation lexical/payload tokens, not model tokens.
Model tokens must come from the model API/tokenizer; they may be blank if the
API does not expose them.
