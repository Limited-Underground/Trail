package io.github.nbjelanovic.otclient

import io.github.nbjelanovic.trailbuild.AppBuildLabel
import org.junit.Assert.*
import org.junit.Test

class AppBuildLabelTest {
    @Test fun releaseIncludesVersionAndCleanOrDirtySourceWithoutBuildType() {
        assertEquals("1.0.0 · f3aeb77", AppBuildLabel.format("1.0.0", "f3aeb77", false, "release"))
        assertEquals("1.0.0 · f3aeb77-dirty", AppBuildLabel.format("1.0.0", "f3aeb77", true, "release"))
    }

    @Test fun dirtyDebugIncludesSourceDirtyMarkerAndType() {
        assertEquals("1.0.0-dev · f3aeb77-dirty · debug",
            AppBuildLabel.format("1.0.0-dev", "f3aeb77", true, "debug"))
    }

    @Test fun cleanDebugAndOtherNonReleaseTypesRemainDistinct() {
        assertEquals("1.0.0-dev · f3aeb77 · debug",
            AppBuildLabel.format("1.0.0-dev", "f3aeb77", false, "debug"))
        assertEquals("1.0.0-v1test · f3aeb77 · v1Test",
            AppBuildLabel.format("1.0.0-v1test", "f3aeb77", false, "v1Test"))
    }

    @Test fun unknownOrInvalidCommitNeverPretendsToBeKnownOrClean() {
        for (commit in listOf(null, "", "unknown", "fatal: not a git repository", "123")) {
            assertEquals("1.0.0-dev · unknown · debug",
                AppBuildLabel.format("1.0.0-dev", commit, true, "debug"))
            assertEquals("1.0.0 · unknown", AppBuildLabel.format("1.0.0", commit, false, "release"))
        }
    }

    @Test fun generatedFieldMatchesTheActualVariantAndHasASourceIdentity() {
        assertTrue(BuildConfig.APP_BUILD_LABEL.startsWith(BuildConfig.VERSION_NAME + " · "))
        if (BuildConfig.BUILD_TYPE == "release") assertFalse(BuildConfig.APP_BUILD_LABEL.endsWith(" · release"))
        else assertTrue(BuildConfig.APP_BUILD_LABEL.endsWith(" · " + BuildConfig.BUILD_TYPE))
        assertTrue(BuildConfig.APP_BUILD_LABEL.contains(Regex(" · ([0-9a-fA-F]{7,40}(-dirty)?|unknown)( · |$)")))
    }
}
