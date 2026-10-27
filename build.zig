const std = @import("std");
const flags = &.{ "-std=c++20", "-Wall", "-Wextra", "-Wpedantic", "-Werror" };
const ui_sources = &.{ "libs/ui/src/marquee.cpp", "libs/ui/src/font.cpp", "libs/ui/src/surface.cpp", "libs/ui/src/tile.cpp", "libs/ui/src/ribbon.cpp" };
const app_sources = &.{
    "apps/dashboard/src/app/config.cpp",
    "apps/dashboard/src/graphics/surface.cpp",
    "apps/dashboard/src/dashboard/dashboard_view.cpp",
    "apps/dashboard/src/dashboard/clock_painter.cpp",
    "apps/dashboard/src/dashboard/service_card.cpp",
    "apps/dashboard/src/dashboard/async_compiler.cpp",
    "apps/dashboard/src/input/touch_decoder.cpp",
    "apps/dashboard/src/system/system_metrics.cpp",
};
fn addUi(b: *std.Build, module: *std.Build.Module) void {
    module.link_libcpp = true;
    module.addIncludePath(b.path("libs/ui/include"));
    module.addIncludePath(b.path("libs/json/include"));
    module.addIncludePath(b.path("vendor"));
    module.addCSourceFiles(.{ .files = ui_sources, .flags = flags });
    module.addCSourceFile(.{ .file = b.path("libs/ui/src/truetype.cpp"), .flags = &.{"-std=c++20"} });
}
fn addProtocol(b: *std.Build, module: *std.Build.Module) void {
    module.link_libcpp = true;
    module.addIncludePath(b.path("libs/dash-protocol/include"));
    module.addIncludePath(b.path("libs/json/include"));
    module.addIncludePath(b.path("vendor/picojson"));
    module.addCSourceFiles(.{ .files = &.{ "libs/dash-protocol/src/dash5.cpp", "libs/dash-protocol/src/dash5_unix.cpp", "libs/dash-protocol/src/dash5_bundle.cpp" }, .flags = flags });
}
fn addConsolePolicy(b: *std.Build, module: *std.Build.Module) void {
    module.link_libcpp = true;
    module.addIncludePath(b.path("apps/dashboard/src"));
    module.addIncludePath(b.path("libs/json/include"));
    module.addIncludePath(b.path("vendor/picojson"));
    module.addCSourceFiles(.{ .files = &.{ "apps/dashboard/src/platform/linux/console/isolation.cpp", "apps/dashboard/src/platform/linux/console/state.cpp" }, .flags = flags });
}
fn addAssets(b: *std.Build, module: *std.Build.Module, app: []const u8) void {
    const object = b.addObject(.{ .name = b.fmt("{s}-assets", .{app}), .root_module = b.createModule(.{ .root_source_file = b.path(b.fmt("apps/{s}/bundled_assets.zig", .{app})), .target = module.resolved_target.?, .optimize = module.optimize.? }) });
    const check = b.addSystemCommand(&.{"python3", "scripts/prepare_service_assets.py", "--check", "--app", app});
    check.setCwd(b.path("."));check.has_side_effects = true;
    object.step.dependOn(&check.step);
    module.addObject(object);
}
fn addDashboard(b: *std.Build, module: *std.Build.Module) void {
    addUi(b, module);
    addProtocol(b, module);
    module.addCSourceFiles(.{ .files = &.{ "libs/ui/src/card.cpp", "libs/ui/src/composition.cpp", "libs/ui/src/composition_json.cpp", "apps/dashboard/src/dashboard/service_feed.cpp" }, .flags = flags });
    module.addCSourceFile(.{ .file = b.path("libs/ui/src/svg.cpp"), .flags = &.{"-std=c++20"} });
    const sans = b.addObject(.{ .name = "ui-sans", .root_module = b.createModule(.{ .root_source_file = b.path("libs/ui/bundled_sans.zig"), .target = module.resolved_target.?, .optimize = module.optimize.? }) });
    module.addObject(sans);
    module.addIncludePath(b.path("apps/dashboard/src"));
    module.addIncludePath(b.path("libs/dash-protocol/include"));
    module.addCSourceFiles(.{ .files = app_sources, .flags = flags });

}
fn addWeather(b: *std.Build, module: *std.Build.Module) void {
    addProtocol(b,module); addAssets(b,module,"weather");
    module.link_libcpp = true;
    module.addIncludePath(b.path("apps/weather/src"));
    module.addIncludePath(b.path("vendor/picojson"));
    module.addIncludePath(b.path("libs/json/include"));
    module.addIncludePath(b.path("libs/http-client/include"));
    module.addIncludePath(b.path("libs/dash-protocol/include"));
    module.addCSourceFiles(.{ .files = &.{ "apps/weather/src/weather.cpp", "apps/weather/src/appearance.cpp", "apps/weather/src/packet.cpp" }, .flags = flags });
}
fn addWaste(b: *std.Build, module: *std.Build.Module) void {
    addProtocol(b,module); addAssets(b,module,"waste");
    module.link_libcpp = true;
    module.addIncludePath(b.path("apps/waste/src"));
    module.addIncludePath(b.path("vendor/picojson"));
    module.addIncludePath(b.path("libs/json/include"));
    module.addIncludePath(b.path("libs/dash-protocol/include"));
    module.addCSourceFiles(.{ .files = &.{ "apps/waste/src/schedule.cpp", "apps/waste/src/packet.cpp" }, .flags = flags });
}
fn linkHttp(b: *std.Build, module: *std.Build.Module) void {
    const http = @import("libs/http-client/build.zig");
    module.linkLibrary(http.addLibrary(b, module.resolved_target.?, module.optimize.?, b.path("libs/http-client")));
    module.addCSourceFile(.{ .file = b.path("apps/weather/src/source.cpp"), .flags = flags });
    if (module.resolved_target.?.result.os.tag == .macos) {
        module.linkFramework("CoreFoundation", .{});
        module.linkFramework("CFNetwork", .{});
        module.linkFramework("Security", .{});
    }
}
pub fn build(b: *std.Build) void {
    const identity = b.addSystemCommand(&.{"python3"});
    identity.addFileArg(b.path("scripts/build_revision.py"));
    identity.addArg("--source");
    identity.addDirectoryArg(b.path("."));
    identity.has_side_effects = true; // Git state must be read even with a cached build graph.
    const identity_source = identity.captureStdOut(.{ .basename = "kafel_build_info.cpp" });
    const optimize = b.standardOptimizeOption(.{});
    const target = b.standardTargetOptions(.{});
    const weather_native = b.addExecutable(.{ .name = "kafel-weather", .root_module = b.createModule(.{ .target = b.graph.host, .optimize = optimize }) });
    addWeather(b, weather_native.root_module);
    linkHttp(b, weather_native.root_module);
    weather_native.root_module.addCSourceFile(.{ .file = b.path("apps/weather/src/main.cpp"), .flags = flags });
    const weather_install = b.addInstallArtifact(weather_native, .{});
    b.step("weather-native", "Build kafel-weather for local development").dependOn(&weather_install.step);
    const weather_tests = b.addExecutable(.{ .name = "weather-tests", .root_module = b.createModule(.{ .target = b.graph.host, .optimize = optimize }) });
    addWeather(b, weather_tests.root_module);
    weather_tests.root_module.addCSourceFiles(.{ .files = &.{ "apps/weather/tests/weather_test.cpp" }, .flags = flags });
    const weather_check = b.step("weather-test", "Run weather domain, Unix transport and HTTP integration tests");
    weather_check.dependOn(&b.addRunArtifact(weather_tests).step);
    const fetch_probe = b.addExecutable(.{ .name = "weather-fetch-probe", .root_module = b.createModule(.{ .target = b.graph.host, .optimize = optimize }) });
    addWeather(b, fetch_probe.root_module);
    linkHttp(b, fetch_probe.root_module);
    fetch_probe.root_module.addCSourceFiles(.{ .files = &.{ "apps/weather/tests/fetch_probe.cpp" }, .flags = flags });
    const daemon_probe = b.addExecutable(.{ .name = "weather-daemon-test", .root_module = b.createModule(.{ .target = b.graph.host, .optimize = optimize }) });
    addWeather(b, daemon_probe.root_module);
    linkHttp(b, daemon_probe.root_module);
    daemon_probe.root_module.addCSourceFile(.{ .file = b.path("apps/weather/src/main.cpp"), .flags = flags });
    const weather_integration = b.addSystemCommand(&.{"python3"});
    weather_integration.addFileArg(b.path("apps/weather/tests/integration.py"));
    weather_integration.addArtifactArg(fetch_probe);
    weather_integration.addArtifactArg(daemon_probe);
    weather_check.dependOn(&weather_integration.step);
    const dashboard = b.addExecutable(.{ .name = "kafel-dashboard", .root_module = b.createModule(.{ .target = b.resolveTargetQuery(.{ .cpu_arch = .aarch64, .os_tag = .linux, .abi = .musl }), .optimize = .ReleaseSmall }) });
    addDashboard(b, dashboard.root_module);
    dashboard.root_module.addCSourceFiles(.{ .files = &.{ "apps/dashboard/src/app/main.cpp", "apps/dashboard/src/platform/linux/framebuffer.cpp", "apps/dashboard/src/platform/linux/evdev_touch.cpp", "apps/dashboard/src/platform/linux/linux_system_probe.cpp" }, .flags = flags });
    const install = b.addInstallArtifact(dashboard, .{ .dest_dir = .{ .override = .{ .custom = "linux-arm64" } } });
    b.getInstallStep().dependOn(&install.step);
    b.step("dashboard", "Build kafel-dashboard for CM4Stack").dependOn(&install.step);
    const weather_arm = b.addExecutable(.{ .name = "kafel-weather", .root_module = b.createModule(.{ .target = dashboard.root_module.resolved_target.?, .optimize = .ReleaseSmall }) });
    addWeather(b, weather_arm.root_module);
    linkHttp(b, weather_arm.root_module);
    weather_arm.root_module.addCSourceFile(.{ .file = b.path("apps/weather/src/main.cpp"), .flags = flags });
    const weather_arm_install = b.addInstallArtifact(weather_arm, .{ .dest_dir = .{ .override = .{ .custom = "linux-arm64" } } });
    b.step("weather", "Build kafel-weather for CM4Stack").dependOn(&weather_arm_install.step);
    const waste_native = b.addExecutable(.{ .name = "kafel-waste", .root_module = b.createModule(.{ .target = b.graph.host, .optimize = optimize }) });
    addWaste(b, waste_native.root_module);
    waste_native.root_module.addCSourceFile(.{ .file = b.path("apps/waste/src/main.cpp"), .flags = flags });
    const waste_install = b.addInstallArtifact(waste_native, .{});
    b.step("waste-native", "Build kafel-waste for local development").dependOn(&waste_install.step);
    const waste_arm = b.addExecutable(.{ .name = "kafel-waste", .root_module = b.createModule(.{ .target = dashboard.root_module.resolved_target.?, .optimize = .ReleaseSmall }) });
    addWaste(b, waste_arm.root_module);
    waste_arm.root_module.addCSourceFile(.{ .file = b.path("apps/waste/src/main.cpp"), .flags = flags });
    const waste_arm_install = b.addInstallArtifact(waste_arm, .{ .dest_dir = .{ .override = .{ .custom = "linux-arm64" } } });
    b.step("waste", "Build kafel-waste for CM4Stack").dependOn(&waste_arm_install.step);
    b.getInstallStep().dependOn(&weather_arm_install.step);
    b.getInstallStep().dependOn(&waste_arm_install.step);
    const console_helper = b.addExecutable(.{ .name = "kafel-display-console", .root_module = b.createModule(.{ .target = dashboard.root_module.resolved_target.?, .optimize = .ReleaseSmall }) });
    addConsolePolicy(b, console_helper.root_module);
    console_helper.root_module.addCSourceFiles(.{ .files = &.{ "apps/dashboard/src/platform/linux/console/system.cpp", "apps/dashboard/src/platform/linux/console/main.cpp", "apps/dashboard/src/platform/linux/framebuffer.cpp" }, .flags = flags });
    const console_install = b.addInstallArtifact(console_helper, .{ .dest_dir = .{ .override = .{ .custom = "linux-arm64" } } });
    install.step.dependOn(&console_install.step);
    const waste_tests = b.addExecutable(.{ .name = "waste-tests", .root_module = b.createModule(.{ .target = b.graph.host, .optimize = optimize }) });
    addWaste(b, waste_tests.root_module);
    waste_tests.root_module.addCSourceFiles(.{ .files = &.{ "apps/waste/tests/waste_test.cpp" }, .flags = flags });
    const waste_check = b.step("waste-test", "Test waste schedule, timezone, DASH/5 and daemon");
    waste_check.dependOn(&b.addRunArtifact(waste_tests).step);
    const waste_daemon = b.addExecutable(.{ .name = "waste-daemon-test", .root_module = b.createModule(.{ .target = b.graph.host, .optimize = optimize }) });
    addWaste(b, waste_daemon.root_module);
    waste_daemon.root_module.addCSourceFile(.{ .file = b.path("apps/waste/src/main.cpp"), .flags = flags });
    const waste_integration = b.addSystemCommand(&.{"python3"});
    waste_integration.addFileArg(b.path("apps/waste/tests/integration.py"));
    waste_integration.addArtifactArg(waste_daemon);
    waste_check.dependOn(&waste_integration.step);
    const formatting = b.addSystemCommand(&.{ "python3", "scripts/check_format.py" });
    formatting.setCwd(b.path("."));
    formatting.has_side_effects = true;
    b.step("format-check", "Check owned C/C++ formatting with clang-format 22").dependOn(&formatting.step);
    const deployment_tests = b.addSystemCommand(&.{ "python3", "-B", "-m", "unittest", "discover", "-s", "scripts/tests" });
    deployment_tests.setCwd(b.path("."));
    const deploy_host = b.option([]const u8, "host", "SSH host") orelse "";
    const deploy_user = b.option([]const u8, "user", "SSH and service user") orelse "";
    const deploy_socket = b.option([]const u8, "ssh-socket", "Optional existing SSH control socket");
    for ([_][]const u8{ "all", "dashboard", "weather", "waste" }) |app| {
        const deploy = b.addSystemCommand(&.{ "python3", "scripts/deploy.py", "--build", "--only", app, "--host", deploy_host, "--user", deploy_user });
        deploy.setCwd(b.path("."));
        if (deploy_socket) |socket| deploy.addArgs(&.{ "--socket", socket });
        deploy.stdio = .inherit;
        deploy.has_side_effects = true;
        const name = if (std.mem.eql(u8, app, "all")) "deploy" else b.fmt("deploy-{s}", .{app});
        b.step(name, "Test, build and deploy selected applications; skip unchanged services").dependOn(&deploy.step);
    }
    b.step("deploy-test", "Run offline deployment tests").dependOn(&deployment_tests.step);
    const tests = b.addExecutable(.{ .name = "kafel-tests", .root_module = b.createModule(.{ .target = b.graph.host, .optimize = optimize }) });
    tests.root_module.addCMacro("KAFEL_JSON_TEST_STATS", "1");
    tests.root_module.addCMacro("KAFEL_ASYNC_TEST_HOOKS", "1");
    addDashboard(b, tests.root_module);
    addConsolePolicy(b, tests.root_module);
    tests.root_module.addCSourceFiles(.{ .files = &.{
        "apps/dashboard/tests/main.cpp", "apps/dashboard/tests/async_compiler_test.cpp", "apps/dashboard/tests/console_test.cpp", "apps/dashboard/tests/config_test.cpp", "apps/dashboard/tests/readable_typography_test.cpp",
        "apps/dashboard/tests/dashboard_test.cpp", "apps/dashboard/tests/frame_schedule_test.cpp", "apps/dashboard/tests/card_test.cpp", "apps/dashboard/tests/dash5_test.cpp", "apps/dashboard/tests/unix_protocol_test.cpp",
        "apps/dashboard/tests/allocation_test.cpp", "apps/dashboard/tests/surface_test.cpp", "apps/dashboard/tests/system_metrics_test.cpp", "apps/dashboard/tests/touch_test.cpp", "apps/dashboard/tests/marquee_test.cpp",
    }, .flags = flags });
    const run = b.addRunArtifact(tests);
    run.setCwd(b.path("."));
    const check = b.step("test", "Run current dashboard, protocol and UI tests");
    check.dependOn(&run.step);
    check.dependOn(&formatting.step);
    check.dependOn(weather_check);
    check.dependOn(waste_check);
    check.dependOn(&deployment_tests.step);
    const dashboard_check = b.step("dashboard-test", "Run dashboard and UI tests");
    dashboard_check.dependOn(&run.step);
    const ui = b.addExecutable(.{ .name = "ui-tests", .root_module = b.createModule(.{ .target = b.graph.host, .optimize = optimize }) });
    addUi(b, ui.root_module);
    ui.root_module.addIncludePath(b.path("libs/ui/tests"));
    ui.root_module.addIncludePath(b.path("apps/dashboard/src"));
    ui.root_module.addCSourceFiles(.{ .files = &.{ "apps/dashboard/tests/tiles_test.cpp", "apps/dashboard/src/dashboard/clock_painter.cpp", "libs/ui/tests/memory.cpp" }, .flags = flags });
    const ui_run = b.addRunArtifact(ui);
    ui_run.addFileArg(b.path("libs/ui/assets/ibm-plex-sans/IBMPlexSans-Regular.ttf"));
    ui_run.addFileArg(b.path("libs/ui/assets/ibm-plex-sans/IBMPlexSans-Bold.ttf"));
    check.dependOn(&ui_run.step);
    dashboard_check.dependOn(&ui_run.step);
    const weather_catalog = b.addExecutable(.{ .name = "kafel-weather-catalog", .root_module = b.createModule(.{ .target = b.graph.host, .optimize = optimize }) });
    weather_catalog.root_module.link_libcpp = true;
    weather_catalog.root_module.addIncludePath(b.path("apps/weather/src"));
    weather_catalog.root_module.addCSourceFiles(.{ .files = &.{ "apps/weather/src/appearance.cpp", "apps/weather/tools/catalog.cpp" }, .flags = flags });
    const weather_catalog_install = b.addInstallArtifact(weather_catalog, .{});
    b.step("weather-catalog", "Build weather-owned offline WMO catalog tool").dependOn(&weather_catalog_install.step);
    const weather_catalog_run = b.addRunArtifact(weather_catalog);
    const weather_catalog_check = b.addSystemCommand(&.{ "python3", "scripts/prepare_weather_icons.py", "--check-catalog" });
    weather_catalog_check.setCwd(b.path("."));
    weather_catalog_check.addFileArg(weather_catalog_run.captureStdOut(.{ .basename = "weather-catalog.tsv" }));
    check.dependOn(&weather_catalog_check.step);
    const weather_card = b.addExecutable(.{ .name = "weather-card-example", .root_module = b.createModule(.{ .target = b.graph.host, .optimize = optimize }) });
    addUi(b, weather_card.root_module);
    weather_card.root_module.addCSourceFiles(.{ .files = &.{ "libs/ui/src/composition.cpp", "libs/ui/src/composition_json.cpp", "libs/ui/examples/weather-card/weather_card.cpp", "libs/ui/examples/weather-card/main.cpp" }, .flags = flags });
    const weather_card_install = b.addInstallArtifact(weather_card, .{});
    b.step("weather-card-bin", "Build standalone weather card renderer without running it").dependOn(&weather_card_install.step);
    const weather_card_run = b.addRunArtifact(weather_card);
    weather_card_run.addFileArg(b.path("libs/ui/assets/ibm-plex-sans/IBMPlexSans-Regular.ttf"));
    weather_card_run.addFileArg(b.path("libs/ui/assets/ibm-plex-sans/IBMPlexSans-Bold.ttf"));
    weather_card_run.addFileArg(b.path("libs/ui/examples/weather-card/weather.json"));
    const weather_svg_scene = b.option([]const u8, "weather-card-scene", "Weather study: sun, clouds, rain or snow") orelse "clouds";
    if (!std.mem.eql(u8, weather_svg_scene, "sun") and !std.mem.eql(u8, weather_svg_scene, "clouds") and !std.mem.eql(u8, weather_svg_scene, "rain") and !std.mem.eql(u8, weather_svg_scene, "snow")) @panic("unknown weather scene");
    weather_card_run.addFileArg(b.path(b.fmt("libs/ui/examples/weather-card/svg/{s}.svg", .{weather_svg_scene})));
    weather_card_run.addArg(b.option([]const u8, "weather-card-output", "240x240 weather PPM output") orelse "/tmp/kafel-weather-240.ppm");
    weather_card_run.addArg(weather_svg_scene);
    b.step("weather-card", "Render standalone 240x240 weather study").dependOn(&weather_card_run.step);
    const weather_card_tests = b.addExecutable(.{ .name = "weather-card-tests", .root_module = b.createModule(.{ .target = b.graph.host, .optimize = optimize }) });
    addUi(b, weather_card_tests.root_module);
    weather_card_tests.root_module.addIncludePath(b.path("libs/ui/tests"));
    weather_card_tests.root_module.addCSourceFiles(.{ .files = &.{ "libs/ui/src/composition.cpp", "libs/ui/src/composition_json.cpp", "libs/ui/examples/weather-card/weather_card.cpp", "libs/ui/examples/weather-card/tests.cpp", "libs/ui/examples/weather-card/composition_test.cpp", "libs/ui/examples/weather-card/composition_json_test.cpp", "libs/ui/examples/weather-card/svg_test.cpp", "libs/ui/tests/memory.cpp" }, .flags = flags });
    const weather_card_test_run = b.addRunArtifact(weather_card_tests);
    weather_card_test_run.addFileArg(b.path("libs/ui/assets/ibm-plex-sans/IBMPlexSans-Regular.ttf"));
    weather_card_test_run.addFileArg(b.path("libs/ui/assets/ibm-plex-sans/IBMPlexSans-Bold.ttf"));
    weather_card_test_run.addFileArg(b.path("libs/ui/examples/weather-card/weather.json"));
    weather_card_test_run.addFileArg(b.path("libs/ui/examples/weather-card/svg/clouds.svg"));
    weather_card_test_run.addDirectoryArg(b.path("apps/weather/assets/icons"));
    b.step("weather-card-test", "Test standalone weather rendering").dependOn(&weather_card_test_run.step);
    check.dependOn(&weather_card_test_run.step);
    const weather_card_linux = b.addExecutable(.{ .name = "weather-card-preview", .root_module = b.createModule(.{ .target = dashboard.root_module.resolved_target.?, .optimize = .ReleaseSmall }) });
    addUi(b, weather_card_linux.root_module);
    weather_card_linux.root_module.addIncludePath(b.path("apps/dashboard/src"));
    weather_card_linux.root_module.addCSourceFiles(.{ .files = &.{ "libs/ui/src/composition.cpp", "libs/ui/src/composition_json.cpp", "libs/ui/examples/weather-card/weather_card.cpp", "libs/ui/examples/weather-card/framebuffer_main.cpp", "apps/dashboard/src/platform/linux/framebuffer.cpp" }, .flags = flags });
    const weather_card_linux_install = b.addInstallArtifact(weather_card_linux, .{ .dest_dir = .{ .override = .{ .custom = "linux-arm64" } } });
    b.step("weather-card-cm4", "Build temporary ARM64 weather preview").dependOn(&weather_card_linux_install.step);
    const screen_preview = b.addSystemCommand(&.{ "python3", "scripts/preview_weather.py", "--build" });
    screen_preview.setCwd(b.path("."));screen_preview.stdio = .inherit;screen_preview.has_side_effects = true;
    b.step("preview-weather", "Temporarily show weather study on CM4, then restore dashboard").dependOn(&screen_preview.step);
    for ([_]*std.Build.Step.Compile{ weather_card, weather_card_tests, weather_card_linux }) |example| {
        example.root_module.addCSourceFile(.{ .file = b.path("libs/ui/src/svg.cpp"), .flags = &.{"-std=c++20"} });
        example.root_module.addIncludePath(b.path("apps/weather/src"));
        example.root_module.addCSourceFile(.{ .file = b.path("apps/weather/src/appearance.cpp"), .flags = flags });
        example.root_module.addIncludePath(b.path("libs/json/include"));
        example.root_module.addIncludePath(b.path("vendor/picojson"));
    }
    const svg_bench = b.addExecutable(.{ .name = "weather-svg-bench", .root_module = b.createModule(.{ .target = b.graph.host, .optimize = optimize }) });
    addUi(b, svg_bench.root_module);
    svg_bench.root_module.addIncludePath(b.path("apps/weather/src"));
    svg_bench.root_module.addIncludePath(b.path("libs/ui/src"));
    svg_bench.root_module.addIncludePath(b.path("libs/json/include"));
    svg_bench.root_module.addIncludePath(b.path("vendor/picojson"));
    svg_bench.root_module.addCSourceFiles(.{ .files = &.{ "libs/ui/src/composition.cpp", "libs/ui/src/composition_json.cpp", "libs/ui/examples/weather-card/weather_card.cpp", "apps/weather/src/appearance.cpp", "libs/ui/examples/weather-card/svg_bench.cpp" }, .flags = flags });
    svg_bench.root_module.addCSourceFile(.{ .file = b.path("libs/ui/src/svg.cpp"), .flags = &.{ "-std=c++20", "-DKAFEL_SVG_MEMORY_STATS=1" } });
    const svg_bench_install = b.addInstallArtifact(svg_bench, .{});
    b.step("weather-svg-bench", "Build local SVG and cached-card benchmark").dependOn(&svg_bench_install.step);
    const preview = b.addExecutable(.{ .name = "kafel-preview", .root_module = b.createModule(.{ .target = target, .optimize = optimize }) });
    addDashboard(b, preview.root_module);
    preview.root_module.addIncludePath(b.path("."));
    addAssets(b,preview.root_module,"weather");addAssets(b,preview.root_module,"waste");
    preview.root_module.addCSourceFiles(.{ .files = &.{ "apps/weather/src/packet.cpp", "apps/weather/src/appearance.cpp", "apps/waste/src/packet.cpp", "apps/waste/src/schedule.cpp" }, .flags = flags });
    preview.root_module.addCSourceFile(.{ .file = b.path("apps/dashboard/examples/preview.cpp"), .flags = flags });
    const preview_run = b.addRunArtifact(preview);
    preview_run.addArg(b.option([]const u8, "output", "Preview PPM path") orelse "/tmp/kafel.ppm");
    const scene = b.option([]const u8, "scene", "Preview scene: system, weather, waste, waste-next, waste-tomorrow, waste-many, limits, error, offline or warning");
    const show_weather = b.option(bool, "weather", "Show weather tile") orelse false;
    if (scene) |name| preview_run.addArg(name) else if (show_weather) preview_run.addArg("weather");
    b.step("preview", "Render current dashboard at 240x320").dependOn(&preview_run.step);
    const clock_preview = b.addRunArtifact(preview);
    clock_preview.addArgs(&.{"--clock-frames", b.option([]const u8,"clock-preview-dir","Existing directory for clock animation frames") orelse "/tmp"});
    b.step("preview-clock","Render TrueType rolling clock frames").dependOn(&clock_preview.step);
    const assets = b.addRunArtifact(preview);
    assets.addArgs(&.{ "--assets", b.option([]const u8, "preview-dir", "Existing directory for gallery PPMs") orelse "/tmp" });
    b.step("preview-assets", "Render gallery scenes and ribbon frames with synthetic data").dependOn(&assets.step);
    const visual_check=b.addSystemCommand(&.{"python3","scripts/check_visual_baseline.py"});
    visual_check.setCwd(b.path("."));
    visual_check.addArtifactArg(preview);
    visual_check.has_side_effects=true;
    check.dependOn(&visual_check.step);
    dashboard_check.dependOn(&visual_check.step);
    b.step("visual-test","Compare dashboard pixels with the approved baseline").dependOn(&visual_check.step);
    for ([_]*std.Build.Step.Compile{ dashboard, weather_native, weather_arm, daemon_probe, waste_native, waste_arm, waste_daemon, console_helper }) |app| {
        app.root_module.addIncludePath(b.path("packaging"));
        app.root_module.addCSourceFile(.{ .file = identity_source, .flags = &.{"-std=c++20"} });
    }

}
