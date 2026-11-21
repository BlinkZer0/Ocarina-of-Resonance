import java.util.Properties

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

val signingProperties = Properties().apply {
    rootProject.file("signing.properties").takeIf { it.isFile }?.inputStream()?.use { load(it) }
}
val signingFields = mapOf(
    "storeFile" to "ANDROID_RELEASE_KEYSTORE",
    "storePassword" to "ANDROID_KEYSTORE_PASSWORD",
    "keyAlias" to "ANDROID_KEY_ALIAS",
    "keyPassword" to "ANDROID_KEY_PASSWORD",
)
val environmentSigning = signingFields.values.any { providers.environmentVariable(it).isPresent }
val releaseSigning = signingFields.mapValues { (field, variable) ->
    if (environmentSigning) providers.environmentVariable(variable).orNull else signingProperties.getProperty(field)
}
val releaseKeystore = releaseSigning["storeFile"]?.takeUnless(String::isBlank)?.let(rootProject::file)
val hasReleaseSigning = releaseKeystore?.isFile == true && releaseSigning.values.all { !it.isNullOrBlank() }

android {
    namespace = "com.blinkzer0.ocarina"
    compileSdk = 36
    ndkVersion = "29.0.14206865"

    defaultConfig {
        applicationId = "com.blinkzer0.ocarina"
        minSdk = 26
        targetSdk = 36
        versionCode = 7
        versionName = rootProject.file("../VERSION").readText().trim()
        ndk { abiFilters += "arm64-v8a" }
        externalNativeBuild { cmake { arguments += "-DANDROID_STL=c++_shared" } }
    }

    signingConfigs {
        if (hasReleaseSigning) create("release") {
            storeFile = releaseKeystore
            storePassword = releaseSigning["storePassword"]
            keyAlias = releaseSigning["keyAlias"]
            keyPassword = releaseSigning["keyPassword"]
        }
    }

    buildTypes {
        debug { applicationIdSuffix = ".debug" }
        release { signingConfig = signingConfigs.findByName("release") }
        create("benchmark") {
            initWith(getByName("release"))
            applicationIdSuffix = ".benchmark"
            isDebuggable = true
            signingConfig = signingConfigs.getByName("debug")
            matchingFallbacks += "release"
            externalNativeBuild.cmake.arguments += "-DCMAKE_BUILD_TYPE=Release"
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    buildFeatures { prefab = true }
    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.31.6"
        }
    }
}

kotlin {
    compilerOptions { jvmTarget.set(org.jetbrains.kotlin.gradle.dsl.JvmTarget.JVM_17) }
}

dependencies {
    implementation("com.google.oboe:oboe:1.11.0")
}

val panelDirectory = layout.buildDirectory.dir("generated/ocarinaPanel")
val syncPanel = tasks.register<Sync>("syncInstrumentPanel") {
    from(rootProject.file("../docs")) {
        include("index.html", "assets/main.js", "assets/presets.js", "assets/sequence.js", "assets/midi-file.js",
            "assets/style.css", "assets/parameters.json", "assets/ocarina.webp")
    }
    into(panelDirectory)
}
android.sourceSets.named("main") { assets.srcDir(panelDirectory) }
tasks.named("preBuild") { dependsOn(syncPanel) }

val checkReleaseSigning = tasks.register("checkReleaseSigning") {
    doLast {
        check(hasReleaseSigning) {
            "Release signing requires a keystore and its credentials. See design/PHASE_6.md."
        }
    }
}
tasks.matching { it.name == "preReleaseBuild" }.configureEach { dependsOn(checkReleaseSigning) }
