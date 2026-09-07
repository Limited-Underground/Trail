package io.github.nbjelanovic.otclient

/** An unsaved form value; never a device readback or a source of connection authority. */
internal data class BleDeviceNameEditor(
    val text: String = "",
    val edited: Boolean = false,
    val suggestion: String? = null,
) {
    companion object {
        fun restore(saved: Any?, currentScope: String, draft: String): BleDeviceNameEditor {
            val values=saved as? List<*> ?: return BleDeviceNameEditor(draft)
            if(values.size!=4) return BleDeviceNameEditor(draft)
            val scope=values[0] as? String ?: return BleDeviceNameEditor(draft)
            val text=values[1] as? String ?: return BleDeviceNameEditor(draft)
            val edited=values[2] as? Boolean ?: return BleDeviceNameEditor(draft)
            val suggestion=values[3] as? String ?: return BleDeviceNameEditor(draft)
            return restore(scope,currentScope,text,edited,suggestion.ifEmpty { null },draft)
        }
        fun restore(savedScope: String, currentScope: String, text: String, edited: Boolean,
            suggestion: String?, draft: String): BleDeviceNameEditor =
            if (savedScope==currentScope && currentScope.isNotBlank())
                BleDeviceNameEditor(text,edited,suggestion?.let { V1SetupLabel.create(it)?.value })
            else BleDeviceNameEditor(draft)
    }
    fun edit(value: String) = copy(text=value, edited=true)
    fun suggest(value: String?, confirmedName: String? = null): BleDeviceNameEditor {
        val next = value?.let { V1SetupLabel.create(it)?.value }
        return copy(text=if (!edited && text!=confirmedName && (text.isEmpty() || text==suggestion)) next.orEmpty() else text,
            suggestion=next)
    }
}
