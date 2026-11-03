workspace "Limited Underground Trail architecture mapping" "Documentation candidate: existing responsibilities and intended direct V1 path. Diagnostic success does not accept protected production delivery." {
    model {
        participant = person "Trail participant" "Uses one authorized phone with one radio device; the intended V1 system has two participants and two pairs."
        maintainer = person "Trail maintainer" "Reads source, exact artifact identities and recovery evidence; product development is frozen."

        trail = softwareSystem "Limited Underground Trail" "Intended offline Android/direct-LoRa communication system. Partial implementation and bounded diagnostic evidence exist; coherent protected V1 acceptance remains open." {
            android = container "Android application" "UI, user intent and connection/service lifecycle. Existing Kotlin implementation; diagnostic V1-Test and production release acceptance are distinct." "Kotlin / Jetpack Compose / Android service" {
                tags "ImplementedFoundation"
            }
            firmware = container "Embedded firmware runtime" "Device authority for protected BLE control, radio, membership and delivery state. Existing portable components and experimental Heltec bindings; full protected product composition remains open." "C++ / ESP-IDF / NimBLE / radio adapter" {
                tags "ImplementedFoundation"
            }
            durable = container "Device-owned durable state" "NVS/persistence adapters exist. Complete production membership, queue, counter/replay and recovery composition remains an acceptance gate." "NVS adapters / portable persistence" {
                tags "PartialPersistence"
            }
        }

        participant -> trail "Intends to exchange compact messages, status and consented location"
        maintainer -> trail "Inspects architecture, source and evidence under separate operation authority"
        participant -> android "Submits intent and reads evidence-bound UI"
        android -> firmware "Protected owner-scoped commands and configuration" "Bluetooth LE / custom versioned GATT records" {
            tags "ExistingBoundary"
        }
        firmware -> android "Authoritative state and typed results within admitted profile" "Bluetooth LE / custom versioned GATT records" {
            tags "ExistingBoundary"
        }
        firmware -> durable "Persists and reads device-owned state; production closure incomplete" "Target storage adapters" {
            tags "IncompleteAcceptance"
        }
        firmware -> firmware "Intended authenticated peer-instance data and ACKs; not yet production accepted" "Direct LoRa / separately admitted Trail wire" {
            tags "IntendedOnly"
        }
    }

    views {
        systemContext trail TrailContext "Intended two-pair product boundary; no required server, internet, map, repeater or maintenance laptop." {
            include *
            autoLayout lr
        }
        container trail TrailContainers "Deployable types, not two deployment instances. Peer-to-peer self-edge describes the intended two firmware instances. Diagnostic DATA/ACK is unauthenticated and does not validate this protected edge." {
            include *
            autoLayout lr
        }
        styles {
            element "Person" {
                shape Person
                background #16476A
                color #FFFFFF
            }
            element "Software System" {
                background #16476A
                color #FFFFFF
            }
            element "ImplementedFoundation" {
                background #C7E4F2
                color #102F42
            }
            element "PartialPersistence" {
                shape Cylinder
                background #FFE3A1
                color #49320B
            }
            relationship "IntendedOnly" {
                color #9A5C00
                dashed true
            }
            relationship "IncompleteAcceptance" {
                color #9A5C00
                dashed true
            }
        }
    }
}
