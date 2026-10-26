// Versions pinned deliberately: AGP 8.5.2 is the last that works with Gradle 8.7
// and Kotlin 1.9.x, which keeps the toolchain small and the build reproducible.
plugins {
    id("com.android.application") version "8.5.2" apply false
    id("org.jetbrains.kotlin.android") version "1.9.24" apply false
}
