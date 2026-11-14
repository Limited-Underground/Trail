import io.github.nbjelanovic.trailbuild.AppBuildLabel

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
    id("org.jetbrains.kotlin.plugin.compose")
}

// A failed Git command must never turn a known HEAD into a guessed clean build.
fun gitOutput(vararg arguments: String): String? = runCatching {
    val result = providers.exec {
        workingDir(rootDir.parentFile)
        commandLine("git", "--no-optional-locks", *arguments)
        isIgnoreExitValue = true
    }
    if (result.result.get().exitValue == 0) result.standardOutput.asText.get().trim() else null
}.getOrNull()

val gitCommit = gitOutput("rev-parse", "--short=7", "HEAD")
val gitChanges = if (gitCommit != null) gitOutput("status", "--porcelain", "--untracked-files=normal") else null

android {
    namespace = "io.github.nbjelanovic.otclient"
    compileSdk = 35
    buildToolsVersion = "35.0.0"

    defaultConfig {
        applicationId = "io.github.nbjelanovic.otclient"
        minSdk = 26
        targetSdk = 35
        versionCode = 1
        versionName = "1.0.0"

        testInstrumentationRunner =
            "io.github.nbjelanovic.otclient.PublicLinkProbeInstrumentation"
    }

    buildTypes {
        getByName("debug") {
            versionNameSuffix = "-dev"
        }
        create("v1Test") {
            initWith(getByName("debug"))
            applicationIdSuffix = ".v1test"
            versionNameSuffix = "-v1test"
            matchingFallbacks += listOf("debug")
            resValue("string", "app_name", "Trail V1-Test")
        }
        getByName("release") {
            isDebuggable = false
            isJniDebuggable = false
            isMinifyEnabled = false
            signingConfig = null
        }
    }

    buildFeatures {
        compose = true
        buildConfig = true
    }

    buildTypes.configureEach {
        val label = AppBuildLabel.format(
            defaultConfig.versionName + versionNameSuffix.orEmpty(),
            if (gitChanges != null) gitCommit else null,
            !gitChanges.isNullOrEmpty(),
            name,
        )
        buildConfigField("String", "APP_BUILD_LABEL", "\"$label\"")
    }

    // Exercise the exact formatter used above, rather than a second implementation.
    sourceSets.getByName("test").java.srcDir(rootProject.file("buildSrc/src/main/java"))

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    kotlinOptions {
        jvmTarget = "17"
    }

    testOptions {
        unitTests.isIncludeAndroidResources = true
        unitTests.all {
            it.systemProperty("robolectric.dependency.repo.url", "https://repo.maven.apache.org/maven2")
            it.systemProperty("user.home", System.getProperty("user.home"))
        }
    }

    packaging {
        resources.excludes += "/META-INF/{AL2.0,LGPL2.1}"
    }

    lint {
        warningsAsErrors = true
        // OT-036 deliberately pins the accepted SDK-35/AGP-8.7 toolchain.
        disable += "GradleDependency"
    }
}

dependencies {
    implementation(project(":protocol"))
    implementation(platform("androidx.compose:compose-bom:2024.12.01"))
    implementation("androidx.activity:activity-compose:1.10.0")
    implementation("androidx.compose.material3:material3")
    implementation("androidx.compose.ui:ui")
    implementation("androidx.compose.ui:ui-tooling-preview")
    implementation("androidx.lifecycle:lifecycle-runtime-compose:2.8.7")

    debugImplementation("androidx.compose.ui:ui-tooling")
    debugImplementation("androidx.compose.ui:ui-test-manifest")
    testImplementation("androidx.compose.ui:ui-test-junit4")
    testImplementation(kotlin("test"))
    testImplementation("junit:junit:4.13.2")
    testImplementation("org.robolectric:robolectric:4.14.1")
}
