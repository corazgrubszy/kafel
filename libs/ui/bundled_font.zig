// Embed the controlled OFL font in the executable: deploy remains one binary.
const font = @embedFile("assets/ibm-plex-mono/IBMPlexMono-Regular.ttf");
export fn ui_bundled_font_data() [*]const u8 {
    return font.ptr;
}
export fn ui_bundled_font_size() usize {
    return font.len;
}
