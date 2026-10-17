const std = @import("std");
const flags = &.{ "-std=c++20", "-Wall", "-Wextra", "-Wpedantic", "-Werror" };
const ui_sources = &.{ "libs/ui/src/font.cpp", "libs/ui/src/surface.cpp", "libs/ui/src/tile.cpp", "libs/ui/src/ribbon.cpp" };
const app_sources = &.{
    "apps/dashboard/src/app/config.cpp",
    "apps/dashboard/src/graphics/surface.cpp",
    "apps/dashboard/src/dashboard/dashboard_view.cpp",
    "apps/dashboard/src/input/touch_decoder.cpp",
    "apps/dashboard/src/system/system_metrics.cpp",
    "apps/dashboard/src/text/marquee_text.cpp",
    "libs/dash-protocol/src/service_registry.cpp",
};
fn addUi(b: *std.Build, module: *std.Build.Module) void {
    module.link_libcpp = true;
    module.addIncludePath(b.path("libs/ui/include"));
    module.addIncludePath(b.path("vendor"));
    module.addCSourceFiles(.{ .files = ui_sources, .flags = flags });
    module.addCSourceFile(.{ .file = b.path("libs/ui/src/truetype.cpp"), .flags = &.{"-std=c++20"} });
}
fn addDashboard(b: *std.Build, module: *std.Build.Module) void {
    addUi(b, module);
    module.addIncludePath(b.path("apps/dashboard/src"));
    module.addIncludePath(b.path("libs/dash-protocol/include"));
    module.addCSourceFiles(.{ .files = app_sources, .flags = flags });
    const font = b.addObject(.{ .name = "ui-font", .root_module = b.createModule(.{ .root_source_file = b.path("libs/ui/bundled_font.zig"), .target = module.resolved_target.?, .optimize = module.optimize.? }) });
    module.addObject(font);
}
fn addWeather(b: *std.Build, module: *std.Build.Module) void {
    module.link_libcpp = true;
    module.addIncludePath(b.path("apps/weather/src"));
    module.addIncludePath(b.path("vendor/picojson"));
    module.addIncludePath(b.path("libs/json/include"));
    module.addIncludePath(b.path("libs/http-client/include"));
    module.addIncludePath(b.path("libs/dash-protocol/include"));
    module.addCSourceFiles(.{ .files = &.{ "apps/weather/src/weather.cpp", "apps/weather/src/packet.cpp", "libs/dash-protocol/src/unix_publisher.cpp" }, .flags = flags });
}
fn addWaste(b: *std.Build, module: *std.Build.Module) void {
    module.link_libcpp = true;
    module.addIncludePath(b.path("apps/waste/src"));
    module.addIncludePath(b.path("vendor/picojson"));
    module.addIncludePath(b.path("libs/json/include"));
    module.addIncludePath(b.path("libs/dash-protocol/include"));
    module.addCSourceFiles(.{ .files = &.{ "apps/waste/src/schedule.cpp", "apps/waste/src/packet.cpp", "libs/dash-protocol/src/unix_publisher.cpp" }, .flags = flags });
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
    const optimize = b.standardOptimizeOption(.{});
    const target = b.standardTargetOptions(.{});
    const weather = b.addExecutable(.{ .name = "kafel-weather", .root_module = b.createModule(.{ .target = target, .optimize = optimize }) });
    addWeather(b, weather.root_module);
    linkHttp(b, weather.root_module);
    weather.root_module.addCSourceFile(.{ .file = b.path("apps/weather/src/main.cpp"), .flags = flags });
    const weather_install = b.addInstallArtifact(weather, .{});
    b.step("weather", "Build standalone kafel-weather for selected target").dependOn(&weather_install.step);
    const weather_tests = b.addExecutable(.{ .name = "weather-tests", .root_module = b.createModule(.{ .target = b.graph.host, .optimize = optimize }) });
    addWeather(b, weather_tests.root_module);
    weather_tests.root_module.addCSourceFiles(.{ .files = &.{ "apps/weather/tests/weather_test.cpp", "libs/dash-protocol/src/service_registry.cpp" }, .flags = flags });
    const weather_check = b.step("weather-test", "Run weather domain, Unix transport and HTTP integration tests");
    weather_check.dependOn(&b.addRunArtifact(weather_tests).step);
    const fetch_probe = b.addExecutable(.{ .name = "weather-fetch-probe", .root_module = b.createModule(.{ .target = b.graph.host, .optimize = optimize }) });
    addWeather(b, fetch_probe.root_module);
    linkHttp(b, fetch_probe.root_module);
    fetch_probe.root_module.addCSourceFiles(.{ .files = &.{ "apps/weather/tests/fetch_probe.cpp", "libs/dash-protocol/src/service_registry.cpp" }, .flags = flags });
    const daemon_probe = b.addExecutable(.{ .name = "weather-daemon-test", .root_module = b.createModule(.{ .target = b.graph.host, .optimize = optimize }) });
    addWeather(b, daemon_probe.root_module);
    linkHttp(b, daemon_probe.root_module);
    daemon_probe.root_module.addCSourceFile(.{ .file = b.path("apps/weather/src/main.cpp"), .flags = flags });
    const weather_integration = b.addSystemCommand(&.{"python3"});
    weather_integration.addFileArg(b.path("apps/weather/tests/integration.py"));
    weather_integration.addArtifactArg(fetch_probe);
    weather_integration.addArtifactArg(daemon_probe);
    weather_check.dependOn(&weather_integration.step);
    const linux = b.addExecutable(.{ .name = "kafel-dashboard", .root_module = b.createModule(.{ .target = b.resolveTargetQuery(.{ .cpu_arch = .aarch64, .os_tag = .linux, .abi = .musl }), .optimize = .ReleaseSmall }) });
    addDashboard(b, linux.root_module);
    linux.root_module.addCSourceFiles(.{ .files = &.{ "apps/dashboard/src/app/main.cpp", "apps/dashboard/src/platform/linux/framebuffer.cpp", "apps/dashboard/src/platform/linux/evdev_touch.cpp", "apps/dashboard/src/platform/linux/linux_system_probe.cpp", "apps/dashboard/src/platform/linux/service_socket.cpp" }, .flags = flags });
    const install = b.addInstallArtifact(linux, .{ .dest_dir = .{ .override = .{ .custom = "linux-arm64" } } });
    b.getInstallStep().dependOn(&install.step);
    b.step("linux", "Build static ARM64 kafel-dashboard").dependOn(&install.step);
    const weather_arm = b.addExecutable(.{ .name = "kafel-weather", .root_module = b.createModule(.{ .target = linux.root_module.resolved_target.?, .optimize = .ReleaseSmall }) });
    addWeather(b, weather_arm.root_module);
    linkHttp(b, weather_arm.root_module);
    weather_arm.root_module.addCSourceFile(.{ .file = b.path("apps/weather/src/main.cpp"), .flags = flags });
    const weather_arm_install = b.addInstallArtifact(weather_arm, .{ .dest_dir = .{ .override = .{ .custom = "linux-arm64" } } });
    b.step("weather-linux", "Build static ARM64 kafel-weather").dependOn(&weather_arm_install.step);
    const waste = b.addExecutable(.{ .name = "kafel-waste", .root_module = b.createModule(.{ .target = target, .optimize = optimize }) });
    addWaste(b, waste.root_module);
    waste.root_module.addCSourceFile(.{ .file = b.path("apps/waste/src/main.cpp"), .flags = flags });
    const waste_install = b.addInstallArtifact(waste, .{});
    b.step("waste", "Build standalone kafel-waste").dependOn(&waste_install.step);
    const waste_arm = b.addExecutable(.{ .name = "kafel-waste", .root_module = b.createModule(.{ .target = linux.root_module.resolved_target.?, .optimize = .ReleaseSmall }) });
    addWaste(b, waste_arm.root_module);
    waste_arm.root_module.addCSourceFile(.{ .file = b.path("apps/waste/src/main.cpp"), .flags = flags });
    const waste_arm_install = b.addInstallArtifact(waste_arm, .{ .dest_dir = .{ .override = .{ .custom = "linux-arm64" } } });
    b.step("waste-linux", "Build static ARM64 kafel-waste").dependOn(&waste_arm_install.step);
    const waste_tests = b.addExecutable(.{ .name = "waste-tests", .root_module = b.createModule(.{ .target = b.graph.host, .optimize = optimize }) });
    addWaste(b, waste_tests.root_module);
    waste_tests.root_module.addCSourceFiles(.{ .files = &.{ "apps/waste/tests/waste_test.cpp", "libs/dash-protocol/src/service_registry.cpp" }, .flags = flags });
    const waste_check = b.step("waste-test", "Test waste schedule, timezone, DASH/4 and daemon");
    waste_check.dependOn(&b.addRunArtifact(waste_tests).step);
    const waste_daemon = b.addExecutable(.{ .name = "waste-daemon-test", .root_module = b.createModule(.{ .target = b.graph.host, .optimize = optimize }) });
    addWaste(b, waste_daemon.root_module);
    waste_daemon.root_module.addCSourceFile(.{ .file = b.path("apps/waste/src/main.cpp"), .flags = flags });
    const waste_integration = b.addSystemCommand(&.{"python3"});
    waste_integration.addFileArg(b.path("apps/waste/tests/integration.py"));
    waste_integration.addArtifactArg(waste_daemon);
    waste_check.dependOn(&waste_integration.step);
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
    addDashboard(b, tests.root_module);
    tests.root_module.addCSourceFiles(.{ .files = &.{
        "apps/dashboard/tests/config_test.cpp", "apps/dashboard/tests/main.cpp",                  "apps/dashboard/tests/readable_typography_test.cpp",
        "apps/dashboard/tests/dashboard_test.cpp",        "apps/dashboard/tests/frame_schedule_test.cpp",
        "apps/dashboard/tests/service_registry_test.cpp", "apps/dashboard/tests/dash4_test.cpp",
        "apps/dashboard/tests/waste_tile_test.cpp",       "apps/dashboard/tests/meter_tile_test.cpp",
        "apps/dashboard/tests/metric_tile_test.cpp",      "apps/dashboard/tests/surface_test.cpp",
        "apps/dashboard/tests/system_metrics_test.cpp",   "apps/dashboard/tests/touch_test.cpp",
        "apps/dashboard/tests/marquee_test.cpp",          "apps/dashboard/tests/allocation_test.cpp",
    }, .flags = flags });
    const run = b.addRunArtifact(tests);
    run.setCwd(b.path("."));
    const check = b.step("test", "Run current dashboard, protocol and UI tests");
    check.dependOn(&run.step);
    check.dependOn(weather_check);
    check.dependOn(waste_check);
    check.dependOn(&deployment_tests.step);
    const dashboard_check = b.step("dashboard-test", "Run dashboard and UI tests");
    dashboard_check.dependOn(&run.step);
    const ui = b.addExecutable(.{ .name = "ui-tests", .root_module = b.createModule(.{ .target = b.graph.host, .optimize = optimize }) });
    addUi(b, ui.root_module);
    ui.root_module.addIncludePath(b.path("libs/ui/tests"));
    ui.root_module.addCSourceFiles(.{ .files = &.{ "libs/ui/tests/tiles_test.cpp", "libs/ui/tests/memory.cpp" }, .flags = flags });
    const ui_run = b.addRunArtifact(ui);
    ui_run.addFileArg(b.path("libs/ui/assets/ibm-plex-mono/IBMPlexMono-Regular.ttf"));
    check.dependOn(&ui_run.step);
    dashboard_check.dependOn(&ui_run.step);
    const preview = b.addExecutable(.{ .name = "kafel-preview", .root_module = b.createModule(.{ .target = target, .optimize = optimize }) });
    addDashboard(b, preview.root_module);
    preview.root_module.addCSourceFile(.{ .file = b.path("apps/dashboard/examples/preview.cpp"), .flags = flags });
    const preview_run = b.addRunArtifact(preview);
    preview_run.addArg(b.option([]const u8, "output", "Preview PPM path") orelse "/tmp/kafel.ppm");
    const scene = b.option([]const u8, "scene", "Preview scene: system, weather or waste");
    const show_weather = b.option(bool, "weather", "Show weather tile") orelse false;
    if (scene) |name| preview_run.addArg(name) else if (show_weather) preview_run.addArg("weather");
    b.step("preview", "Render current dashboard at 240x320").dependOn(&preview_run.step);
    const assets = b.addRunArtifact(preview);
    assets.addArgs(&.{ "--assets", b.option([]const u8, "preview-dir", "Existing directory for gallery PPMs") orelse "/tmp" });
    b.step("preview-assets", "Render gallery scenes and ribbon frames with synthetic data").dependOn(&assets.step);
    const socket = b.addExecutable(.{ .name = "kafel-socket-tests", .root_module = b.createModule(.{ .target = linux.root_module.resolved_target.?, .optimize = .ReleaseSmall }) });
    socket.root_module.link_libcpp = true;
    socket.root_module.addIncludePath(b.path("apps/dashboard/src"));
    socket.root_module.addIncludePath(b.path("libs/dash-protocol/include"));
    socket.root_module.addCSourceFiles(.{ .files = &.{ "apps/dashboard/tests/service_socket_linux_test.cpp", "apps/dashboard/src/platform/linux/service_socket.cpp", "libs/dash-protocol/src/service_registry.cpp" }, .flags = flags });
    const installed_socket = b.addInstallArtifact(socket, .{ .dest_dir = .{ .override = .{ .custom = "linux-arm64" } } });
    b.step("linux-tests", "Build ARM64 socket integration tests").dependOn(&installed_socket.step);
}
