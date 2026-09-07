package io.github.nbjelanovic.otclient
import kotlin.test.*
class BleDeviceNameEditorTest {
    @Test fun emptyFieldUsesSuggestionButUserEditsAndDraftsWin() {
        val proposed=BleDeviceNameEditor().suggest("Trail-23ABCD")
        assertEquals("Trail-23ABCD",proposed.text)
        assertEquals("Camp",proposed.edit("Camp").suggest("Trail-45EFGH").text)
        assertEquals("",proposed.edit("").suggest("Trail-23ABCD").text)
        assertEquals("Draft",BleDeviceNameEditor("Draft").suggest("Trail-23ABCD").text)
    }
    @Test fun legacyOrMalformedSavedValueStartsFresh() {
        listOf<Any?>(null,"old string",emptyList<Any>(),listOf("a","b",false),
            listOf(1,"name",true,""),listOf("scope","name","true",""),
            listOf("scope","name",true,1)).forEach {
            assertEquals("Draft",BleDeviceNameEditor.restore(it,"scope","Draft").text)
        }
        assertEquals("Camp",BleDeviceNameEditor.restore(listOf("scope","Camp",true,""),"scope","").text)
    }
    @Test fun restorePreservesEditsOnlyForExactConfigurationScope() {
        val same=BleDeviceNameEditor.restore("processA:5","processA:5","",true,"Trail-23ABCD","")
        assertEquals("",same.suggest("Trail-23ABCD").text)
        assertEquals("Camp",BleDeviceNameEditor.restore("processA:5","processA:5","Camp",true,"Trail-23ABCD","").text)
        assertEquals("Draft",BleDeviceNameEditor.restore("processA:5","processA:6","Old",true,"Trail-23ABCD","Draft").text)
        assertEquals("",BleDeviceNameEditor.restore("processA:5","processA:6","Old",true,"Trail-23ABCD","").text)
        assertEquals("",BleDeviceNameEditor.restore("processA:5","processB:5","Old",true,null,"").text)
    }
    @Test fun revokedSuggestionClearsOnlyUntouchedPrefillAndNewEditorHasNoOldValue() {
        val proposed=BleDeviceNameEditor().suggest("Trail-23ABCD")
        assertEquals("Trail-23ABCD",proposed.suggest(null,"Trail-23ABCD").text)
        assertEquals("",proposed.suggest(null).text)
        assertEquals("Camp",proposed.edit("Camp").suggest(null).text)
        assertEquals("",BleDeviceNameEditor().text)
        assertEquals("",BleDeviceNameEditor().suggest("invalid").text)
    }
}
