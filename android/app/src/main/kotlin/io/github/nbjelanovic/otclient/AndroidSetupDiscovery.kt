package io.github.nbjelanovic.otclient

import java.util.UUID

/** Display-only D1 label, parsed from the current advertisement rather than a cached device name. */
internal object AndroidSetupAdvertisementLabel {
    sealed interface Result {
        data object Absent : Result
        data object Invalid : Result
        data class Label(val value: String) : Result
    }

    fun decode(record: ByteArray?): Result {
        if (record == null || record.isEmpty()) return Result.Absent
        if (record.size > 512) return Result.Invalid
        var offset = 0
        var label: String? = null
        while (offset < record.size) {
            val length = record[offset].toInt() and 0xff
            if (length == 0) {
                if (record.copyOfRange(offset, record.size).any { it.toInt() != 0 }) return Result.Invalid
                break
            }
            if (offset + 1 + length > record.size) return Result.Invalid
            when (record[offset + 1].toInt() and 0xff) {
                0x08 -> return Result.Invalid // A shortened local name cannot identify a setup choice.
                0x09 -> {
                    if (label != null || length != V1_SETUP_CODE_LENGTH + 1) return Result.Invalid
                    val code = record.copyOfRange(offset + 2, offset + 1 + length)
                    if (code.any { (it.toInt() and 0xff) > 0x7f }) return Result.Invalid
                    label = V1SetupLabel.create("Trail-" + code.toString(Charsets.US_ASCII))?.value
                        ?: return Result.Invalid
                }
            }
            offset += length + 1
        }
        return label?.let { Result.Label(it) } ?: Result.Absent
    }
}

/** One bounded scan; a conflicting label invalidates every choice and requires a fresh scan. */
internal class AndroidSetupDiscovery<Endpoint>(private val maximumEndpoints: Int) {
    private val labels = LinkedHashMap<Endpoint, String>()
    private var closed = false
    data class Admission(val label: String? = null, val invalidated: Boolean = false,
        val issue: BleSetupLabelIssue? = null)

    fun observe(endpoint: Endpoint, record: ByteArray?, advertisedServiceUuids: Collection<UUID>): Admission {
        if (closed) return Admission(invalidated = true)
        // Response-only service data and D0 advertisements are not pairable primary records.
        if (!AndroidPairableAdvertisementPolicy.accepts(advertisedServiceUuids)) return Admission()
        val decoded = AndroidSetupAdvertisementLabel.decode(record)
        if (decoded == AndroidSetupAdvertisementLabel.Result.Absent || decoded == AndroidSetupAdvertisementLabel.Result.Invalid) {
            return if (labels.containsKey(endpoint)) invalidate(BleSetupLabelIssue.CHANGED) else Admission()
        }
        val label = (decoded as AndroidSetupAdvertisementLabel.Result.Label).value
        val previous = labels[endpoint]
        if (previous != null && previous != label) return invalidate(BleSetupLabelIssue.CHANGED)
        if (labels.any { it.key != endpoint && it.value == label }) return invalidate(BleSetupLabelIssue.AMBIGUOUS)
        if (previous == null && labels.size >= maximumEndpoints) return invalidate(null)
        labels[endpoint] = label
        return Admission(label)
    }

    private fun invalidate(issue: BleSetupLabelIssue?): Admission {
        closed = true
        labels.clear()
        return Admission(invalidated = true, issue = issue)
    }
}
