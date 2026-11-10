package com.nexusai.advanced

class LlamaEngine {
 companion object { init { System.loadLibrary("nexus_llama") } }
 external fun nativeLoad(path: String): Boolean
 external fun nativeGenerate(prompt: String, maxTokens: Int): String
 external fun nativeUnload()
}
