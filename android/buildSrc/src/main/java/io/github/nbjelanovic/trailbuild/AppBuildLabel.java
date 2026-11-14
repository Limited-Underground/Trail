package io.github.nbjelanovic.trailbuild;

/** Shared by Gradle generation and the app's JVM tests; no runtime Git access. */
public final class AppBuildLabel {
    private AppBuildLabel() {}

    public static String format(String versionName, String commit, boolean dirty, String buildType) {
        String source = commit != null && commit.matches("[0-9a-fA-F]{7,40}")
                ? commit + (dirty ? "-dirty" : "") : "unknown";
        return versionName + " · " + source + ("release".equals(buildType) ? "" : " · " + buildType);
    }
}
