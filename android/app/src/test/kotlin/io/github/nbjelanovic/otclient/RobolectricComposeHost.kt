package io.github.nbjelanovic.otclient

import android.content.ComponentName
import androidx.activity.ComponentActivity
import org.junit.rules.RuleChain
import org.junit.rules.TestRule
import org.junit.runners.model.Statement
import org.robolectric.RuntimeEnvironment
import org.robolectric.Shadows.shadowOf

internal fun withRobolectricComposeHost(compose: TestRule): RuleChain {
    val registerHost = TestRule { base, _ ->
        object : Statement() {
            override fun evaluate() {
                // createComposeRule launches ComponentActivity through the package manager.
                val app = RuntimeEnvironment.getApplication()
                shadowOf(app.packageManager).addActivityIfNotPresent(ComponentName(app, ComponentActivity::class.java))
                base.evaluate()
            }
        }
    }
    return RuleChain.outerRule(registerHost).around(compose)
}
