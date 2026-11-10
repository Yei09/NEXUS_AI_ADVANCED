#include <jni.h>
#include <string>
#include <vector>
#include <mutex>
#include <algorithm>
#include "llama.h"

static llama_model* model = nullptr;
static llama_context* context = nullptr;
static std::mutex guard;

extern "C" JNIEXPORT jboolean JNICALL Java_com_nexusai_advanced_LlamaEngine_nativeLoad(JNIEnv* env, jobject, jstring file) {
 std::lock_guard<std::mutex> lock(guard);
 if (context) { llama_free(context); context = nullptr; }
 if (model) { llama_model_free(model); model = nullptr; }
 const char* path = env->GetStringUTFChars(file, nullptr);
 auto mp = llama_model_default_params();
 model = llama_model_load_from_file(path, mp);
 env->ReleaseStringUTFChars(file, path);
 if (!model) return JNI_FALSE;
 auto cp = llama_context_default_params();
 cp.n_ctx = 2048; cp.n_batch = 256; cp.n_threads = 4; cp.n_threads_batch = 4;
 context = llama_init_from_model(model, cp);
 if (!context) { llama_model_free(model); model = nullptr; return JNI_FALSE; }
 return JNI_TRUE;
}

extern "C" JNIEXPORT jstring JNICALL Java_com_nexusai_advanced_LlamaEngine_nativeGenerate(JNIEnv* env, jobject, jstring prompt, jint maximum) {
 std::lock_guard<std::mutex> lock(guard);
 if (!model || !context) return env->NewStringUTF("Carga primero un modelo GGUF.");
 const char* raw = env->GetStringUTFChars(prompt, nullptr);
 std::string input(raw); env->ReleaseStringUTFChars(prompt, raw);
 const llama_vocab* vocab = llama_model_get_vocab(model);
 std::vector<llama_token> tokens(input.size() + 32);
 int count = llama_tokenize(vocab, input.c_str(), (int)input.size(), tokens.data(), (int)tokens.size(), true, true);
 if (count < 0) { tokens.resize(-count); count = llama_tokenize(vocab, input.c_str(), (int)input.size(), tokens.data(), (int)tokens.size(), true, true); }
 if (count <= 0) return env->NewStringUTF("No se pudo tokenizar el mensaje.");
 tokens.resize(count);
 llama_kv_cache_clear(context);
 llama_batch batch = llama_batch_get_one(tokens.data(), (int)tokens.size());
 if (llama_decode(context, batch) != 0) return env->NewStringUTF("Falló el procesamiento del prompt.");
 auto params = llama_sampler_chain_default_params();
 llama_sampler* sampler = llama_sampler_chain_init(params);
 llama_sampler_chain_add(sampler, llama_sampler_init_temp(0.7f));
 llama_sampler_chain_add(sampler, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));
 std::string result; char piece[512];
 int limit = std::min(std::max((int)maximum, 1), 192);
 for (int i = 0; i < limit; ++i) {
  llama_token token = llama_sampler_sample(sampler, context, -1);
  if (token == llama_vocab_eos(vocab)) break;
  int n = llama_token_to_piece(vocab, token, piece, sizeof(piece), 0, true);
  if (n > 0) result.append(piece, n);
  llama_batch next = llama_batch_get_one(&token, 1);
  if (llama_decode(context, next) != 0) break;
 }
 llama_sampler_free(sampler);
 return env->NewStringUTF(result.c_str());
}

extern "C" JNIEXPORT void JNICALL Java_com_nexusai_advanced_LlamaEngine_nativeUnload(JNIEnv*, jobject) {
 std::lock_guard<std::mutex> lock(guard);
 if (context) { llama_free(context); context = nullptr; }
 if (model) { llama_model_free(model); model = nullptr; }
}
