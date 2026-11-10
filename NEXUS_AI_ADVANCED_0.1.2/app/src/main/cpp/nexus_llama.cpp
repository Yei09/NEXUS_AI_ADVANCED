#include <jni.h>
#include <string>
#include <vector>
#include <mutex>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include "llama.h"

static llama_model* g_model = nullptr;
static llama_context* g_context = nullptr;
static std::mutex g_mutex;
static bool g_backend_ready = false;

static void release_model() {
    if (g_context) {
        llama_free(g_context);
        g_context = nullptr;
    }
    if (g_model) {
        llama_model_free(g_model);
        g_model = nullptr;
    }
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_nexusai_advanced_LlamaEngine_nativeLoad(JNIEnv* env, jobject, jstring file) {
    std::lock_guard<std::mutex> lock(g_mutex);
    release_model();

    if (!g_backend_ready) {
        llama_backend_init();
        g_backend_ready = true;
    }

    const char* path = env->GetStringUTFChars(file, nullptr);
    if (path == nullptr) return JNI_FALSE;

    llama_model_params model_params = llama_model_default_params();
    model_params.use_mmap = true;
    g_model = llama_model_load_from_file(path, model_params);
    env->ReleaseStringUTFChars(file, path);

    if (g_model == nullptr) return JNI_FALSE;

    llama_context_params context_params = llama_context_default_params();
    context_params.n_ctx = 2048;
    context_params.n_batch = 128;
    context_params.n_ubatch = 128;
    context_params.n_threads = 2;
    context_params.n_threads_batch = 2;

    g_context = llama_init_from_model(g_model, context_params);
    if (g_context == nullptr) {
        release_model();
        return JNI_FALSE;
    }
    return JNI_TRUE;
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_nexusai_advanced_LlamaEngine_nativeGenerate(JNIEnv* env, jobject, jstring user_text, jint max_tokens) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_model == nullptr || g_context == nullptr) {
        return env->NewStringUTF("Primero carga un modelo GGUF.");
    }

    const char* raw = env->GetStringUTFChars(user_text, nullptr);
    if (raw == nullptr) return env->NewStringUTF("No se pudo leer el mensaje.");
    std::string user(raw);
    env->ReleaseStringUTFChars(user_text, raw);

    const llama_chat_message messages[] = {
        {"system", "Eres NEXUS AI. Responde siempre en español claro, útil y respetuoso. Si no sabes algo, dilo con honestidad."},
        {"user", user.c_str()}
    };

    // Lee la plantilla de chat desde los metadatos GGUF; Qwen2.5 usa ChatML.
    const char* chat_template = llama_model_chat_template(g_model);
    if (chat_template == nullptr) {
        chat_template = "{% for message in messages %}{{ '<|im_start|>' + message['role'] + '\\n' + message['content'] + '<|im_end|>\\n' }}{% endfor %}{% if add_generation_prompt %}{{ '<|im_start|>assistant\\n' }}{% endif %}";
    }
    std::vector<char> formatted(8192, '\\0');
    int32_t formatted_size = llama_chat_apply_template(
        chat_template, messages, 2, true, formatted.data(), static_cast<int32_t>(formatted.size()));
    if (formatted_size < 0) {
        return env->NewStringUTF("El modelo no proporciona una plantilla de chat compatible.");
    }
    if (formatted_size >= static_cast<int32_t>(formatted.size())) {
        formatted.resize(static_cast<size_t>(formatted_size) + 1, '\\0');
        formatted_size = llama_chat_apply_template(
            chat_template, messages, 2, true, formatted.data(), static_cast<int32_t>(formatted.size()));
        if (formatted_size < 0 || formatted_size >= static_cast<int32_t>(formatted.size())) {
            return env->NewStringUTF("No se pudo preparar el prompt del modelo.");
        }
    }

    const llama_vocab* vocab = llama_model_get_vocab(g_model);
    std::vector<llama_token> tokens(static_cast<size_t>(formatted_size) * 2 + 32);
    int32_t token_count = llama_tokenize(
        vocab, formatted.data(), formatted_size, tokens.data(),
        static_cast<int32_t>(tokens.size()), true, true);
    if (token_count < 0) {
        tokens.resize(static_cast<size_t>(-token_count));
        token_count = llama_tokenize(
            vocab, formatted.data(), formatted_size, tokens.data(),
            static_cast<int32_t>(tokens.size()), true, true);
    }
    if (token_count <= 0) return env->NewStringUTF("No se pudo tokenizar el mensaje.");
    tokens.resize(static_cast<size_t>(token_count));

    const int32_t output_limit = std::min(std::max(static_cast<int32_t>(max_tokens), 1), 192);
    const uint32_t context_limit = llama_n_ctx(g_context);
    if (tokens.size() + static_cast<size_t>(output_limit) >= context_limit) {
        return env->NewStringUTF("El mensaje es demasiado largo para el contexto local de 2048 tokens.");
    }

    llama_kv_cache_clear(g_context);
    const size_t chunk_size = std::max<size_t>(1, llama_n_batch(g_context));
    for (size_t offset = 0; offset < tokens.size(); offset += chunk_size) {
        const size_t count = std::min(chunk_size, tokens.size() - offset);
        llama_batch batch = llama_batch_get_one(tokens.data() + offset, static_cast<int32_t>(count));
        if (llama_decode(g_context, batch) != 0) {
            return env->NewStringUTF("Falló la evaluación del prompt en llama.cpp.");
        }
    }

    llama_sampler_chain_params sampler_params = llama_sampler_chain_default_params();
    llama_sampler* sampler = llama_sampler_chain_init(sampler_params);
    if (sampler == nullptr) return env->NewStringUTF("No se pudo iniciar el muestreador.");
    llama_sampler_chain_add(sampler, llama_sampler_init_top_k(40));
    llama_sampler_chain_add(sampler, llama_sampler_init_top_p(0.9f, 1));
    llama_sampler_chain_add(sampler, llama_sampler_init_temp(0.7f));
    llama_sampler_chain_add(sampler, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));

    std::string answer;
    char piece[1024];
    for (int32_t i = 0; i < output_limit; ++i) {
        const llama_token token = llama_sampler_sample(sampler, g_context, -1);
        llama_sampler_accept(sampler, token);
        if (llama_vocab_is_eog(vocab, token)) break;

        int32_t piece_size = llama_token_to_piece(vocab, token, piece, sizeof(piece), 0, true);
        if (piece_size < 0) {
            std::vector<char> larger(static_cast<size_t>(-piece_size));
            piece_size = llama_token_to_piece(vocab, token, larger.data(),
                                               static_cast<int32_t>(larger.size()), 0, true);
            if (piece_size > 0) answer.append(larger.data(), static_cast<size_t>(piece_size));
        } else if (piece_size > 0) {
            answer.append(piece, static_cast<size_t>(piece_size));
        }

        llama_batch next = llama_batch_get_one(const_cast<llama_token*>(&token), 1);
        if (llama_decode(g_context, next) != 0) break;
    }
    llama_sampler_free(sampler);

    if (answer.empty()) return env->NewStringUTF("El modelo terminó sin generar texto. Intenta otra pregunta.");
    return env->NewStringUTF(answer.c_str());
}

extern "C" JNIEXPORT void JNICALL
Java_com_nexusai_advanced_LlamaEngine_nativeUnload(JNIEnv*, jobject) {
    std::lock_guard<std::mutex> lock(g_mutex);
    release_model();
}
