package io.github.nbjelanovic.otclient

import org.junit.Assert.assertEquals
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.annotation.Config

/** Serbian phones get the script they asked for (D-196); other phones get their language or English. */
@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35])
class AppLanguageResolutionTest {
    private fun messages(): String = RuntimeEnvironment.getApplication().getString(R.string.nav_messages)

    @Test @Config(qualifiers = "b+sr+Latn") fun serbianLatinGetsLatin() = assertEquals("Poruke", messages())
    @Test @Config(qualifiers = "b+sr+Cyrl") fun serbianCyrillicGetsCyrillic() = assertEquals("Поруке", messages())
    @Test @Config(qualifiers = "sr") fun plainSerbianGetsCyrillic() = assertEquals("Поруке", messages())
    @Test @Config(qualifiers = "sr-rRS") fun serbianSerbiaGetsCyrillic() = assertEquals("Поруке", messages())
    @Test @Config(qualifiers = "b+sr+Latn+RS") fun serbianLatinSerbiaGetsLatin() = assertEquals("Poruke", messages())
    @Test @Config(qualifiers = "hr") fun croatianGetsCroatian() = assertEquals("Poruke", messages())
    @Test @Config(qualifiers = "pt") fun unsupportedLanguageGetsEnglish() = assertEquals("Messages", messages())
}
