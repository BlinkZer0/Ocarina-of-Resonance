plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "com.blinkzer0.ocarina"
    compileSdk = 36
    ndkVersion = "29.0.14206865"

    defaultConfig {
        applicationId = "com.blinkzer0.ocarina"
        minSdk = 26
        targetSdk = 36
        versionCode = 5
        versionName = rootProject.file("../VERSION").readText().trim()
        ndk { abiFilters += "arm64-v8a" }
        externalNativeBuild { cmake { arguments += "-DANDROID_STL=c++_shared" } }
    }

    buildTypes {
        debug { applicationIdSuffix = ".debug" }
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
        include("index.html", "assets/main.js", "assets/presets.js", "assets/style.css", "assets/parameters.json", "assets/ocarina.webp")
    }
    into(panelDirectory)
}
android.sourceSets.named("main") { assets.srcDir(panelDirectory) }
tasks.named("preBuild") { dependsOn(syncPanel) }
