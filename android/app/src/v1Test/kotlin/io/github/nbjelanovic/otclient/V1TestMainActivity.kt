package io.github.nbjelanovic.otclient

import android.content.Intent
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable

/** Field logging is compiled only into the separate test application. */
class V1TestMainActivity : MainActivity() {
    private val recorder get() = V1TestLogRuntime.get(applicationContext)
    @Composable
    override fun AdditionalTools() {
        TextButton(onClick = { startActivity(Intent(this, V1TestLogActivity::class.java)) }) {
            Text("V1-Test · Connection log")
        }
    }
    override fun onStart() {
        super.onStart()
        recorder.lifecycle(V1TestAppLifecycleState.FOREGROUND)
    }
    override fun onStop() {
        recorder.lifecycle(V1TestAppLifecycleState.BACKGROUND)
        super.onStop()
    }
    override fun onDestroy() {
        recorder.lifecycle(V1TestAppLifecycleState.STOPPED)
        super.onDestroy()
    }
}
