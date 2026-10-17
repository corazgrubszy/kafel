const std = @import("std");
fn configure(b: *std.Build, module: *std.Build.Module, root: std.Build.LazyPath) void {
    module.link_libcpp = true;
    module.addIncludePath(root.path(b, "include"));
    module.addIncludePath(root.path(b, "../../vendor/cpp-httplib"));
    for ([_][]const u8{ "../../vendor/mbedtls/include", "../../vendor/mbedtls/library", "../../vendor/mbedtls/3rdparty/everest/include", "../../vendor/mbedtls/3rdparty/everest/include/everest", "../../vendor/mbedtls/3rdparty/everest/include/everest/kremlib", "../../vendor/mbedtls/3rdparty/p256-m", "../../vendor/mbedtls/3rdparty/p256-m/p256-m" }) |path| module.addIncludePath(root.path(b, path));
    for (mbed_sources) |source| module.addCSourceFile(.{ .file = root.path(b, source), .flags = &.{"-std=c99"} });
    module.addCSourceFile(.{ .file = root.path(b, "src/http_client.cpp"), .flags = &.{ "-std=c++20", "-Wall", "-Wextra", "-Werror", "-DCPPHTTPLIB_MBEDTLS_SUPPORT", "-DCPPHTTPLIB_USE_NON_BLOCKING_GETADDRINFO" } });
}
pub fn addLibrary(b: *std.Build, target: std.Build.ResolvedTarget, optimize: std.builtin.OptimizeMode, root: std.Build.LazyPath) *std.Build.Step.Compile {
    const library = b.addLibrary(.{ .name = "http-client", .linkage = .static, .root_module = b.createModule(.{ .target = target, .optimize = optimize }) });
    configure(b, library.root_module, root);
    return library;
}
pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});
    const library = addLibrary(b, target, optimize, b.path("."));
    b.installArtifact(library);
    const probe = b.addExecutable(.{ .name = "http-client-probe", .root_module = b.createModule(.{ .target = target, .optimize = optimize }) });
    probe.root_module.link_libcpp = true;
    if (target.result.os.tag == .macos) {
        probe.root_module.linkFramework("CoreFoundation", .{});
        probe.root_module.linkFramework("CFNetwork", .{});
        probe.root_module.linkFramework("Security", .{});
    }
    probe.root_module.addIncludePath(b.path("include"));
    probe.root_module.linkLibrary(library);
    probe.root_module.addCSourceFile(.{ .file = b.path("probe.cpp"), .flags = &.{ "-std=c++20", "-Wall", "-Wextra", "-Werror" } });
    b.installArtifact(probe);
    const python = b.option([]const u8, "python", "Python with cryptography (test fixtures)") orelse "python3";
    const tests = b.addSystemCommand(&.{ python, "tests/run.py" });
    tests.setCwd(b.path("."));
    tests.addArtifactArg(probe);
    b.step("test", "Run local HTTP and HTTPS regression tests").dependOn(&tests.step);
}
const mbed_sources = &[_][]const u8{
    "../../vendor/mbedtls/library/aes.c",
    "../../vendor/mbedtls/library/aesce.c",
    "../../vendor/mbedtls/library/aesni.c",
    "../../vendor/mbedtls/library/aria.c",
    "../../vendor/mbedtls/library/asn1parse.c",
    "../../vendor/mbedtls/library/asn1write.c",
    "../../vendor/mbedtls/library/base64.c",
    "../../vendor/mbedtls/library/bignum.c",
    "../../vendor/mbedtls/library/bignum_core.c",
    "../../vendor/mbedtls/library/bignum_mod.c",
    "../../vendor/mbedtls/library/bignum_mod_raw.c",
    "../../vendor/mbedtls/library/block_cipher.c",
    "../../vendor/mbedtls/library/camellia.c",
    "../../vendor/mbedtls/library/ccm.c",
    "../../vendor/mbedtls/library/chacha20.c",
    "../../vendor/mbedtls/library/chachapoly.c",
    "../../vendor/mbedtls/library/cipher.c",
    "../../vendor/mbedtls/library/cipher_wrap.c",
    "../../vendor/mbedtls/library/cmac.c",
    "../../vendor/mbedtls/library/constant_time.c",
    "../../vendor/mbedtls/library/ctr_drbg.c",
    "../../vendor/mbedtls/library/debug.c",
    "../../vendor/mbedtls/library/des.c",
    "../../vendor/mbedtls/library/dhm.c",
    "../../vendor/mbedtls/library/ecdh.c",
    "../../vendor/mbedtls/library/ecdsa.c",
    "../../vendor/mbedtls/library/ecjpake.c",
    "../../vendor/mbedtls/library/ecp.c",
    "../../vendor/mbedtls/library/ecp_curves.c",
    "../../vendor/mbedtls/library/ecp_curves_new.c",
    "../../vendor/mbedtls/library/entropy.c",
    "../../vendor/mbedtls/library/entropy_poll.c",
    "../../vendor/mbedtls/library/error.c",
    "../../vendor/mbedtls/library/gcm.c",
    "../../vendor/mbedtls/library/hkdf.c",
    "../../vendor/mbedtls/library/hmac_drbg.c",
    "../../vendor/mbedtls/library/lmots.c",
    "../../vendor/mbedtls/library/lms.c",
    "../../vendor/mbedtls/library/md.c",
    "../../vendor/mbedtls/library/md5.c",
    "../../vendor/mbedtls/library/memory_buffer_alloc.c",
    "../../vendor/mbedtls/library/mps_reader.c",
    "../../vendor/mbedtls/library/mps_trace.c",
    "../../vendor/mbedtls/library/net_sockets.c",
    "../../vendor/mbedtls/library/nist_kw.c",
    "../../vendor/mbedtls/library/oid.c",
    "../../vendor/mbedtls/library/padlock.c",
    "../../vendor/mbedtls/library/pem.c",
    "../../vendor/mbedtls/library/pk.c",
    "../../vendor/mbedtls/library/pk_ecc.c",
    "../../vendor/mbedtls/library/pk_wrap.c",
    "../../vendor/mbedtls/library/pkcs12.c",
    "../../vendor/mbedtls/library/pkcs5.c",
    "../../vendor/mbedtls/library/pkcs7.c",
    "../../vendor/mbedtls/library/pkparse.c",
    "../../vendor/mbedtls/library/pkwrite.c",
    "../../vendor/mbedtls/library/platform.c",
    "../../vendor/mbedtls/library/platform_util.c",
    "../../vendor/mbedtls/library/poly1305.c",
    "../../vendor/mbedtls/library/psa_crypto.c",
    "../../vendor/mbedtls/library/psa_crypto_aead.c",
    "../../vendor/mbedtls/library/psa_crypto_cipher.c",
    "../../vendor/mbedtls/library/psa_crypto_client.c",
    "../../vendor/mbedtls/library/psa_crypto_driver_wrappers_no_static.c",
    "../../vendor/mbedtls/library/psa_crypto_ecp.c",
    "../../vendor/mbedtls/library/psa_crypto_ffdh.c",
    "../../vendor/mbedtls/library/psa_crypto_hash.c",
    "../../vendor/mbedtls/library/psa_crypto_mac.c",
    "../../vendor/mbedtls/library/psa_crypto_pake.c",
    "../../vendor/mbedtls/library/psa_crypto_random.c",
    "../../vendor/mbedtls/library/psa_crypto_rsa.c",
    "../../vendor/mbedtls/library/psa_crypto_se.c",
    "../../vendor/mbedtls/library/psa_crypto_slot_management.c",
    "../../vendor/mbedtls/library/psa_crypto_storage.c",
    "../../vendor/mbedtls/library/psa_its_file.c",
    "../../vendor/mbedtls/library/psa_util.c",
    "../../vendor/mbedtls/library/ripemd160.c",
    "../../vendor/mbedtls/library/rsa.c",
    "../../vendor/mbedtls/library/rsa_alt_helpers.c",
    "../../vendor/mbedtls/library/sha1.c",
    "../../vendor/mbedtls/library/sha256.c",
    "../../vendor/mbedtls/library/sha3.c",
    "../../vendor/mbedtls/library/sha512.c",
    "../../vendor/mbedtls/library/ssl_cache.c",
    "../../vendor/mbedtls/library/ssl_ciphersuites.c",
    "../../vendor/mbedtls/library/ssl_client.c",
    "../../vendor/mbedtls/library/ssl_cookie.c",
    "../../vendor/mbedtls/library/ssl_debug_helpers_generated.c",
    "../../vendor/mbedtls/library/ssl_msg.c",
    "../../vendor/mbedtls/library/ssl_ticket.c",
    "../../vendor/mbedtls/library/ssl_tls.c",
    "../../vendor/mbedtls/library/ssl_tls12_client.c",
    "../../vendor/mbedtls/library/ssl_tls12_server.c",
    "../../vendor/mbedtls/library/ssl_tls13_client.c",
    "../../vendor/mbedtls/library/ssl_tls13_generic.c",
    "../../vendor/mbedtls/library/ssl_tls13_keys.c",
    "../../vendor/mbedtls/library/ssl_tls13_server.c",
    "../../vendor/mbedtls/library/threading.c",
    "../../vendor/mbedtls/library/timing.c",
    "../../vendor/mbedtls/library/version.c",
    "../../vendor/mbedtls/library/version_features.c",
    "../../vendor/mbedtls/library/x509.c",
    "../../vendor/mbedtls/library/x509_create.c",
    "../../vendor/mbedtls/library/x509_crl.c",
    "../../vendor/mbedtls/library/x509_crt.c",
    "../../vendor/mbedtls/library/x509_csr.c",
    "../../vendor/mbedtls/library/x509write.c",
    "../../vendor/mbedtls/library/x509write_crt.c",
    "../../vendor/mbedtls/library/x509write_csr.c",
    "../../vendor/mbedtls/3rdparty/everest/library/Hacl_Curve25519.c",
    "../../vendor/mbedtls/3rdparty/everest/library/everest.c",
    "../../vendor/mbedtls/3rdparty/everest/library/x25519.c",
    "../../vendor/mbedtls/3rdparty/everest/library/Hacl_Curve25519_joined.c",
    "../../vendor/mbedtls/3rdparty/p256-m/p256-m_driver_entrypoints.c",
    "../../vendor/mbedtls/3rdparty/p256-m/p256-m/p256-m.c",
};
