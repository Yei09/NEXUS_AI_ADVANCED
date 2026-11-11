package com.nexusai.advanced

import android.app.Activity
import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.view.View
import android.widget.*
import java.io.File
import java.util.concurrent.Executors

class MainActivity : Activity() {
 private val executor = Executors.newSingleThreadExecutor()
 private lateinit var engine: LlamaEngine
 private lateinit var transcript: TextView
 private lateinit var prompt: EditText
 private lateinit var modelLabel: TextView
 private var modelFile: File? = null
 private val picker = 4102
 override fun onCreate(savedInstanceState: Bundle?) {
  super.onCreate(savedInstanceState)
  engine = LlamaEngine()
  val root = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(20,24,20,16); setBackgroundColor(0xff08111f.toInt()) }
  val title = TextView(this).apply { text = "NEXUS AI ADVANCED 0.1.3\nMOTOR LOCAL ARM64"; textSize = 20f; setTextColor(0xff55d9e8.toInt()) }
  root.addView(title)
  modelLabel = TextView(this).apply { text = "Modelo: ninguno cargado"; setTextColor(-1); setPadding(0,16,0,12) }
  root.addView(modelLabel)
  val import = Button(this).apply { text = "Importar modelo GGUF"; setOnClickListener { chooseModel() } }
  root.addView(import)
  val manage = Button(this).apply { text = "Eliminar modelo"; setOnClickListener { deleteModel() } }
  root.addView(manage)
  transcript = TextView(this).apply { text = "Importa un archivo GGUF Q4 para iniciar inferencia local. El modelo permanece en el almacenamiento privado de la app. No se conecta a servidores."; textSize = 15f; setTextColor(-1); setPadding(4,16,4,16) }
  val scroll = ScrollView(this); scroll.addView(transcript); root.addView(scroll, LinearLayout.LayoutParams(-1,0,1f))
  prompt = EditText(this).apply { hint = "Escribe tu mensaje"; setTextColor(-1); setHintTextColor(0xff9aabba.toInt()) }
  root.addView(prompt)
  root.addView(Button(this).apply { text = "Enviar al modelo local"; setOnClickListener { sendPrompt() } })
  setContentView(root)
  val existing = getPreferences(0).getString("model_path", null)
  if (existing != null && File(existing).exists()) { modelFile = File(existing); loadModel(modelFile!!) }
 }
 private fun chooseModel() {
  val i = Intent(Intent.ACTION_OPEN_DOCUMENT).apply { type = "application/octet-stream"; addCategory(Intent.CATEGORY_OPENABLE) }
  startActivityForResult(i, picker)
 }
 @Deprecated("Use Activity Result API in a later UI refactor")
 override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
  super.onActivityResult(requestCode,resultCode,data)
  if (requestCode != picker || resultCode != RESULT_OK) return
  val uri: Uri = data?.data ?: return
  executor.execute {
   try {
    val dir = File(filesDir,"models").apply { mkdirs() }
    val name = queryName(uri).substringAfterLast('/').ifBlank { "imported-model.gguf" }
    val target = File(dir, if (name.endsWith(".gguf",true)) name else "$name.gguf")
    contentResolver.openInputStream(uri).use { input -> target.outputStream().use { output -> input?.copyTo(output) ?: error("No se pudo leer el archivo") } }
    runOnUiThread { modelFile = target; getPreferences(0).edit().putString("model_path",target.absolutePath).apply(); loadModel(target) }
   } catch (e: Exception) { runOnUiThread { append("Error al importar: ${e.message}") } }
  }
 }
 private fun queryName(uri: Uri): String = uri.lastPathSegment ?: "modelo.gguf"
 private fun loadModel(file: File) {
  modelLabel.text = "Cargando ${file.name}…"
  executor.execute {
   val ok = runCatching { engine.nativeLoad(file.absolutePath) }.getOrDefault(false)
   runOnUiThread { modelLabel.text = if (ok) "Modelo activo: ${file.name}" else "No se pudo cargar: ${file.name}"; append(if(ok) "Modelo cargado por llama.cpp" else "Falló la carga. Comprueba que sea un GGUF compatible y que haya RAM suficiente.") }
  }
 }
 private fun sendPrompt() {
  val text = prompt.text.toString().trim(); if (text.isEmpty()) return
  prompt.setText(""); append("TÚ: $text")
  executor.execute {
   val answer = runCatching { engine.nativeGenerate(text,160) }.getOrElse { "Error de inferencia: ${it.message}" }
   runOnUiThread { append("NEXUS: $answer") }
  }
 }
 private fun deleteModel() {
  executor.execute {
   engine.nativeUnload()
   modelFile?.delete(); modelFile = null
   getPreferences(0).edit().remove("model_path").apply()
   runOnUiThread { modelLabel.text = "Modelo: ninguno cargado"; append("Modelo local eliminado del almacenamiento privado.") }
  }
 }
 private fun append(s: String) { transcript.append("\n\n$s") }
 override fun onDestroy() { executor.execute { runCatching { engine.nativeUnload() } }; executor.shutdown(); super.onDestroy() }
}
