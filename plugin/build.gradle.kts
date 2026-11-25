import java.util.Properties

plugins {
    id("com.android.application")
}

// The native build runs Python scripts that need numpy, scipy and colour-science.
// Set python.executable in local.properties to choose the interpreter.
val pythonExecutable: String? = Properties().apply {
    rootProject.file("local.properties").takeIf { it.isFile }?.reader()?.use { load(it) }
}.getProperty("python.executable")?.takeIf { it.isNotBlank() }?.replace('\\', '/')

// Signing key for distributed builds. Set storeFile, storePassword, keyAlias and keyPassword
// in keystore.properties; without that file, staging builds are left unsigned.
val signingProperties: Properties? = rootProject.file("keystore.properties").takeIf { it.isFile }
    ?.let { file -> Properties().apply { file.reader().use { load(it) } } }

android {
    namespace = "spektrafilm.ufx"
    compileSdk = 36
    ndkVersion = "28.2.13676358"

    defaultConfig {
        applicationId = "spektrafilm.ufx"
        minSdk = 26
        targetSdk = 36
        versionCode = 1
        versionName = "1.0.0"

        ndk {
            abiFilters += listOf("arm64-v8a")
        }

        externalNativeBuild {
            cmake {
                arguments += listOf("-DANDROID_STL=c++_static")
                pythonExecutable?.let { arguments += "-DPython3_EXECUTABLE=$it" }
            }
        }
    }

    signingConfigs {
        signingProperties?.let { props ->
            create("distribution") {
                storeFile = file(props.getProperty("storeFile"))
                storePassword = props.getProperty("storePassword")
                keyAlias = props.getProperty("keyAlias")
                keyPassword = props.getProperty("keyPassword")
            }
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
        }
        create("staging") {
            initWith(getByName("release"))
            signingConfig = signingConfigs.findByName("distribution")
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }
}
