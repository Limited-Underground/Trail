workspace "Limited Underground Trail Android App components" "Source-backed checked-in foundation. Private diagnostic overlays and protected product acceptance are separate." {
    model {
        participant = person "Trail participant" "User intent; no radio authority."
        platform = softwareSystem "Android platform" "GATT, permissions and OS facilities."
        trail = softwareSystem "Limited Underground Trail" "Intended two-pair direct V1; complete protected acceptance remains open." {
            app = container "Limited Underground Trail Android App" "One APK; checked-in public foundation, not the accepted private diagnostic source composition." "Kotlin / Compose" {
                ui = component "UI / lifecycle" "Activity and Compose bindings." "Compose / Activity"
                service = component "Service ownership" "One generation-bound controller graph." "Service / Binder"
                controller = component "App controller" "Current intent and typed presentation." "Kotlin"
                runtime = component "BLE runtime" "Profile, authorization, Snapshot and close." "Kotlin"
                facade = component "GATT adapter" "Owned callbacks; one native operation." "Android GATT"
                protocol = component "Companion codecs" "Custom records; no platform or RF ownership." "Kotlin :protocol"
                local = component "Local UI models" "Drafts/templates and deliberate support export." "Preferences / FileProvider"
            }
            device = container "Firmware runtime" "Device authority; product composition incomplete." "ESP-IDF / NimBLE"
        }
        participant -> ui "User intent / observed state"
        ui -> service "Explicit start/bind; current state" "Android Binder"
        service -> controller "Owns and closes graph"
        controller -> runtime "Current intent / typed state"
        runtime -> facade "Owned requests / callbacks"
        runtime -> protocol "Versioned codec calls"
        facade -> platform "GATT / permission APIs"
        facade -> device "Companion profile records" "BLE / ATT / GATT"
        ui -> local "Local edits / reviewed export"
        local -> platform "App storage / explicit sharing"
    }
    views {
        component app AndroidComponents "Checked-in Android foundation. One application container; classes are grouped by responsibility. Private diagnostic code, unwired plaintext inbox storage and complete radio/product acceptance are not asserted." {
            include *
            autoLayout lr 100 70
        }
        styles {
            relationship "Relationship" {
                fontSize 28
            }
            element "Person" {
                shape Person
                width 300
                height 480
                fontSize 28
                background #16476A
                color #FFFFFF
            }
            element "Software System" {
                width 350
                height 260
                fontSize 28
                background #46586A
                color #FFFFFF
            }
            element "Container" {
                fontSize 28
                background #16476A
                color #FFFFFF
            }
            element "Component" {
                width 350
                height 360
                fontSize 28
                background #D7EAF4
                color #102F42
            }
        }
    }
}
